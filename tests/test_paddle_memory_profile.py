import unittest
from scantailor_ocr_unified.paddle_runtime import detector_profile

class PaddleMemoryProfile(unittest.TestCase):
    def test_four_gb_cpu_uses_bundled_mobile_detector(self):
        self.assertEqual(detector_profile('cpu',4*1024**3),('PP-OCRv5_mobile_det',1536))

    def test_gpu_and_roomy_cpu_keep_server_detector(self):
        self.assertEqual(detector_profile('gpu:0',1024**3),('PP-OCRv5_server_det',2048))
        self.assertEqual(detector_profile('cpu',8*1024**3),('PP-OCRv5_server_det',2048))

if __name__=='__main__':unittest.main()
