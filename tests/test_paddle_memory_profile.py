import unittest
from scantailor_ocr_unified.paddle_runtime import detector_profile, execution_devices, model_profile

class PaddleMemoryProfile(unittest.TestCase):
    def test_four_gb_cpu_uses_bundled_mobile_detector(self):
        self.assertEqual(detector_profile('cpu',4*1024**3),('PP-OCRv5_mobile_det',1536))

    def test_gpu_and_roomy_cpu_keep_server_detector(self):
        self.assertEqual(detector_profile('gpu:0',1024**3),('PP-OCRv5_server_det',2048))
        self.assertEqual(detector_profile('cpu',8*1024**3),('PP-OCRv5_server_det',2048))

    def test_cpu_execution_keeps_saved_gpu_model_and_resolution(self):
        device, profile = execution_devices('gpu', force_cpu=True)
        self.assertEqual(device, 'cpu')
        self.assertEqual(model_profile('PP-OCRv5', 'de', profile, 4*1024**3),
                         ('PP-OCRv5_server_det', 'latin_PP-OCRv5_mobile_rec', 2048))

    def test_explicit_cpu_profile_still_respects_its_memory_budget(self):
        device, profile = execution_devices('cpu', force_cpu=True)
        self.assertEqual(device, 'cpu')
        self.assertEqual(model_profile('PP-OCRv5', 'de', profile, 4*1024**3)[0], 'PP-OCRv5_mobile_det')

    def test_default_device_selection_is_unchanged(self):
        self.assertEqual(execution_devices('gpu'), ('gpu', 'gpu'))
        self.assertEqual(execution_devices('cpu'), ('cpu', 'cpu'))

if __name__=='__main__':unittest.main()
