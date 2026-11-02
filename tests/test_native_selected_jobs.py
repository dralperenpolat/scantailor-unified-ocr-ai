from pathlib import Path
import unittest
from unittest.mock import patch

from scantailor_ocr_unified.native_worker import Worker


class SelectedRetry(unittest.TestCase):
    def test_retry_processes_only_selected_job_without_removing_others(self):
        worker = Worker.__new__(Worker)
        worker.request = {'mode': 'prepare', 'only_jobs': ['failed']}
        worker.settings = {}
        worker.data = {'jobs': [{'id': 'done', 'title': 'Done'}, {'id': 'failed', 'title': 'Failed'}],
                       'output_root': '.'}
        worker.queue_path = Path('unused-test-queue.json')
        worker.store = None
        worker.control_state = lambda: 'run'
        worker.checkpoint = lambda: None
        worker.emit = lambda *args, **kwargs: None
        worker.native = lambda *args: None
        saved = []
        worker.save = lambda **kwargs: saved.append([j['id'] for j in worker.data['jobs']])
        visited = []
        def prepare():
            visited.append(worker.job['id'])
            worker._prepared_in_pass = True
            return Path('.'), Path('unused-project.ScanTailor'), Path('.')
        worker.prepare = prepare
        with patch('scantailor_ocr_unified.revision_queue.accept_revisions', return_value=([], [])), \
                patch('scantailor_ocr_unified.native_worker.source_paths', return_value=[]):
            self.assertEqual(worker.run(), 0)
        self.assertEqual(visited, ['failed'])
        self.assertTrue(saved)
        self.assertTrue(all(ids == ['done', 'failed'] for ids in saved))


if __name__ == '__main__':
    unittest.main()
