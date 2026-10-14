import tempfile,unittest
from pathlib import Path
from scantailor_ocr_unified.queue_lock import exclusive_queue

class QueueLock(unittest.TestCase):
 def test_duplicate_writer_fails_and_finished_writer_releases(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'queue.json'
   with exclusive_queue(p):
    with self.assertRaises(RuntimeError):
     with exclusive_queue(p):pass
   with exclusive_queue(p):pass

if __name__=='__main__':unittest.main()
