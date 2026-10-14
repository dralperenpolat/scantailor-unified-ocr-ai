import unittest
from unittest.mock import patch
from scantailor_ocr_unified.memory_budget import bounded_workers,is_memory_failure,available_bytes

class MemoryBudgetTest(unittest.TestCase):
    def test_native_page_concurrency_shrinks_when_headroom_is_low(self):
        with patch('scantailor_ocr_unified.memory_budget.available_bytes',return_value=5*1024**3):
            self.assertEqual(bounded_workers(4),1)
        with patch('scantailor_ocr_unified.memory_budget.available_bytes',return_value=20*1024**3):
            self.assertEqual(bounded_workers(4),4)

    def test_fallback_and_failure_detection(self):
        with patch('scantailor_ocr_unified.memory_budget.available_bytes',return_value=None):
            self.assertEqual(bounded_workers(2),2)
        self.assertTrue(is_memory_failure(MemoryError()))
        self.assertTrue(is_memory_failure(RuntimeError('Unable to allocate 720 MiB')))
        self.assertFalse(is_memory_failure(RuntimeError('Unknown image format')))
        value=available_bytes()
        self.assertTrue(value is None or value>0)

if __name__=='__main__':unittest.main()
