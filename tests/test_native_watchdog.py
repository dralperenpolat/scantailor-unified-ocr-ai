"""A stalled native child must not hang cancellation or the whole book queue."""
import contextlib
import io
import sys
import tempfile
import time
import unittest
from pathlib import Path
from scantailor_ocr_unified.native_worker import Worker, Cancelled, write_json


class NativeWatchdog(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='unified_watchdog_')
        self.base = Path(self.temp.name)
        write_json(self.base / 'queue.json', {'jobs': []})
        write_json(self.base / 'control.json', {'state': 'run'})
        self.worker = Worker({'root': str(self.base), 'cli': sys.executable,
            'queue': str(self.base / 'queue.json'), 'control': str(self.base / 'control.json'),
            'language': 'deu', 'native_page_timeout': .6})

    def tearDown(self):
        self.temp.cleanup()

    def run_child(self, code):
        with contextlib.redirect_stdout(io.StringIO()):
            self.worker.native(['-u', '-c', code])

    def test_normal_exit_is_not_terminated_at_stdout_eof(self):
        self.run_child('print(123)')

    def test_stalled_page_times_out(self):
        start = time.monotonic()
        with self.assertRaises(TimeoutError):
            self.run_child('import time; time.sleep(30)')
        self.assertLess(time.monotonic() - start, 5)

    def test_cancel_terminates_unresponsive_child(self):
        self.worker.request['native_page_timeout'] = 60
        write_json(self.base / 'control.json', {'state': 'cancel'})
        start = time.monotonic()
        with self.assertRaises(Cancelled):
            self.run_child('import time; time.sleep(30)')
        self.assertLess(time.monotonic() - start, 6)

    def test_pause_does_not_count_as_a_stall(self):
        write_json(self.base / 'control.json', {'state': 'pause'})
        self.run_child('import os,json,time; time.sleep(1.2); '
            f'open({str(self.base / "control.json")!r},"w").write(json.dumps({{"state":"run"}})); print(123)')


if __name__ == '__main__':
    unittest.main(verbosity=2)
