import unittest

from scantailor_ocr_unified.pdf_export import same_aspect_with_pixel_rounding


class AspectRounding(unittest.TestCase):
    def test_independent_thumbnails_of_real_landscape_page(self):
        self.assertTrue(same_aspect_with_pixel_rounding((1399, 854), (4200, 2562)))
        self.assertTrue(same_aspect_with_pixel_rounding((2199, 1342), (4200, 2562)))

    def test_crop_and_distortion_are_still_rejected(self):
        self.assertFalse(same_aspect_with_pixel_rounding((1399, 854), (4200, 2500)))
        self.assertFalse(same_aspect_with_pixel_rounding((1000, 1400), (2000, 3000)))

    def test_extreme_landscape_and_portrait_scaling(self):
        for sizes in (((10000, 101), (5000, 50)), ((101, 10000), (50, 5000))):
            self.assertTrue(same_aspect_with_pixel_rounding(*sizes))
        self.assertFalse(same_aspect_with_pixel_rounding((10000, 101), (5000, 55)))


if __name__ == '__main__':
    unittest.main()
