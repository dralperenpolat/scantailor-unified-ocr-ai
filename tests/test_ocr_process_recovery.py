import errno
import queue
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from PIL import Image

from scantailor_ocr_unified import ocr_backend as ocr
from scantailor_ocr_unified.native_worker import Worker


class OcrProcessRecovery(unittest.TestCase):
    def tearDown(self):
        ocr.close_clients()

    def test_closed_windows_pipe_is_a_process_failure(self):
        client = ocr.PaddleClient.__new__(ocr.PaddleClient)
        client.proc = Mock()
        client.proc.poll.return_value = None
        client.proc.stdin.write.side_effect = OSError(errno.EINVAL, 'Invalid argument')
        client.recent = []
        with self.assertRaises(ocr.PaddleProcessError):
            client.recognize('page.png', 'de', 'gpu')

    def test_actual_child_exit_is_detected_without_waiting_for_timeout(self):
        spawn = subprocess.Popen
        def crash_worker(args, **kwargs):
            return spawn([sys.executable, '-u', '-c',
                          'import sys,os; sys.stdin.readline(); os._exit(23)'], **kwargs)
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(ocr, 'runtime_path', return_value=Path(sys.executable)), \
                patch.object(ocr, 'resource_path', return_value=Path(directory)), \
                patch.object(ocr.subprocess, 'Popen', side_effect=crash_worker):
            client = ocr.PaddleClient(directory, 'gpu')
            try:
                with self.assertRaises(ocr.PaddleProcessError):
                    client.recognize('page.png', 'de', 'gpu')
                client.proc.wait(timeout=5)
                with self.assertRaisesRegex(ocr.PaddleProcessError, '23'):
                    client.recognize('next.png', 'de', 'gpu')
            finally:
                client.close()

    def test_one_restart_retries_identical_page_and_keeps_settings(self):
        first, second = Mock(), Mock()
        first.recognize.side_effect = ocr.PaddleProcessError('closed')
        second.recognize.return_value = {'ok': True, 'lines': [], 'device': 'gpu:0'}
        conf = {'root': '.', 'ocr_device': 'gpu', 'paddle_language': 'de',
                'paddle_version': 'PP-OCRv5'}
        metadata = {}
        with tempfile.TemporaryDirectory() as work, \
                patch.object(ocr, 'PaddleClient', side_effect=[first, second]) as factory, \
                patch.object(ocr, 'lines_pdf', return_value=b'ocr-layer'):
            result = ocr.paddle_pdf(Image.new('L', (20, 20), 255), conf, work, metadata)
        self.assertEqual(result, b'ocr-layer')
        self.assertEqual(factory.call_count, 2)
        self.assertEqual(first.recognize.call_args, second.recognize.call_args)
        first.close.assert_called_once()
        self.assertEqual(metadata['process_restarts'], 1)
        self.assertIs(ocr._clients[('.', 'gpu')], second)

    def test_repeated_process_failure_is_bounded_and_not_cached(self):
        clients = [Mock(), Mock()]
        for client in clients:
            client.recognize.side_effect = ocr.PaddleProcessError('closed')
        with tempfile.TemporaryDirectory() as work, \
                patch.object(ocr, 'PaddleClient', side_effect=clients) as factory:
            with self.assertRaises(ocr.PaddleProcessError):
                ocr.paddle_pdf(Image.new('L', (20, 20)), {'root': '.', 'ocr_device': 'gpu'}, work)
        self.assertEqual(factory.call_count, 2)
        self.assertFalse(ocr._clients)
        for client in clients:
            client.close.assert_called_once()

    def test_model_error_is_not_mistaken_for_a_broken_process(self):
        client = Mock()
        client.recognize.side_effect = RuntimeError('Out of memory')
        with tempfile.TemporaryDirectory() as work, \
                patch.object(ocr, 'PaddleClient', return_value=client) as factory:
            with self.assertRaisesRegex(RuntimeError, 'Out of memory'):
                ocr.paddle_pdf(Image.new('L', (20, 20)), {'root': '.'}, work)
        self.assertEqual(factory.call_count, 1)
        client.close.assert_not_called()

    def test_repeated_ocr_crash_preserves_checkpoint_and_stops_before_next_book(self):
        worker = Worker.__new__(Worker)
        worker.request = {'mode': 'export'}
        worker.settings = {}
        first = {'id': 'first', 'title': 'First', 'completed_pages': 34}
        later = {'id': 'later', 'title': 'Later'}
        worker.data = {'jobs': [first, later], 'output_root': '.'}
        worker.queue_path = Path('unused-test-queue.json')
        worker.store = None
        worker.control_state = lambda: 'run'
        worker.checkpoint = lambda: None
        worker.emit = Mock()
        worker.save = Mock()
        visited = []
        def prepare():
            visited.append(worker.job['id'])
            return Path('.'), Path('unused-project.ScanTailor'), Path('.')
        worker.prepare = prepare
        worker.export = Mock(side_effect=ocr.PaddleProcessError('process failed twice'))
        with patch('scantailor_ocr_unified.revision_queue.accept_revisions', return_value=([], [])), \
                patch('scantailor_ocr_unified.native_worker.source_paths', return_value=[]):
            self.assertEqual(worker.run(), 4)
        self.assertEqual(visited, ['first'])
        self.assertEqual(first['completed_pages'], 34)
        self.assertTrue(first['resource_retry'])
        self.assertEqual(worker.session_state, 'stopped')
        self.assertNotIn('error', later)
        worker.save.assert_called()


if __name__ == '__main__':
    unittest.main()
