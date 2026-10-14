"""Windows readers must not interrupt a durable queue update."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from scantailor_ocr_unified.native_worker import Worker,write_json,save_queue,load_queue


class AtomicWriteTest(unittest.TestCase):
    def test_memory_pressure_streams_checkpoint_without_losing_existing_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'queue.json';write_json(path,{'pages':240})
            with patch('scantailor_ocr_unified.native_worker.json.dumps',side_effect=MemoryError):
                write_json(path,{'pages':241,'title':'Düzenlenmiş kitap'},compact=True)
            self.assertEqual(load_queue(path),{'pages':241,'title':'Düzenlenmiş kitap'})
            self.assertFalse(list(Path(folder).glob('*.tmp')))

    def test_transient_reader_lock_preserves_old_json_until_replaced(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'queue.json'
            write_json(path,{'pages':240})
            replace=os.replace
            attempts=[]
            def briefly_locked(source,destination):
                attempts.append(1)
                self.assertEqual(json.loads(path.read_text()),{'pages':240})
                if len(attempts)<4:raise PermissionError('Windows reader lock')
                replace(source,destination)
            with patch('scantailor_ocr_unified.native_worker.os.replace',side_effect=briefly_locked):
                write_json(path,{'pages':241})
            self.assertEqual(len(attempts),4)
            self.assertEqual(json.loads(path.read_text()),{'pages':241})
            self.assertEqual(list(Path(folder).glob('*.tmp')),[])

    def test_permanent_failure_keeps_old_file_and_removes_temp(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'queue.json'
            write_json(path,{'pages':240})
            with patch('scantailor_ocr_unified.native_worker.os.replace',side_effect=PermissionError('locked')), \
                 patch('scantailor_ocr_unified.native_worker.time.monotonic',side_effect=[0,6]):
                with self.assertRaises(PermissionError):write_json(path,{'pages':241})
            self.assertEqual(json.loads(path.read_text()),{'pages':240})
            self.assertEqual(list(Path(folder).glob('*.tmp')),[])

    def test_locked_gui_does_not_lose_progress_or_stop_worker(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'queue.json';write_json(path,{'pages':240})
            replace=os.replace
            def locked_queue(source,destination):
                if Path(destination)==path:raise PermissionError('GUI holds queue open')
                return replace(source,destination)
            with patch('scantailor_ocr_unified.native_worker.os.replace',side_effect=locked_queue):
                self.assertFalse(save_queue(path,{'pages':241}))
                self.assertFalse(save_queue(path,{'pages':242}))
            self.assertEqual(json.loads(path.read_text()),{'pages':240})
            self.assertEqual(load_queue(path),{'pages':242})
            self.assertTrue(save_queue(path,load_queue(path)))
            self.assertEqual(json.loads(path.read_text()),{'pages':242})
            self.assertFalse(path.with_name(path.name+'.pending.json').exists())

    def test_newer_user_queue_takes_precedence_over_old_pending_checkpoint(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'queue.json'
            pending=path.with_name(path.name+'.pending.json')
            write_json(pending,{'pages':240});os.utime(pending,(100,100))
            write_json(path,{'pages':241,'user_edit':True})
            self.assertEqual(load_queue(path),{'pages':241,'user_edit':True})

    def test_locked_event_log_is_replayed_without_interrupting_processing(self):
        with tempfile.TemporaryDirectory() as folder:
            worker=object.__new__(Worker)
            worker.job={'id':'book','title':'Book'}
            worker.session=Path(folder)/'session.json'
            worker.session_state='running';worker.event_log=Path(folder)/'events.jsonl'
            original=Path.open;attempts=[]
            def locked_once(path,*args,**kwargs):
                if path==worker.event_log and args and args[0]=='a':
                    attempts.append(1)
                    if len(attempts)==1:raise PermissionError('GUI reading event log')
                return original(path,*args,**kwargs)
            with patch.object(Path,'open',locked_once),patch('builtins.print'):
                worker.emit('Output',page=1,total=2)
                worker.emit('Output',page=2,total=2)
            events=[json.loads(line) for line in worker.event_log.read_text('utf8').splitlines()]
            self.assertEqual([row['page'] for row in events],[1,2])
            self.assertEqual(json.loads(worker.session.read_text())['event']['page'],2)
            self.assertFalse(worker._pending_events)

if __name__=='__main__':unittest.main()
