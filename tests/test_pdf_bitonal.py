import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import fitz
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from scantailor_ocr_unified.pdf_compression import compress


class BitonalCompression(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def text_image(self, mode='L'):
        image = Image.new(mode, (900, 1200), 'white')
        draw = ImageDraw.Draw(image)
        font = ImageFont.load_default(size=28)
        for y in range(30, 1160, 35):
            draw.text((25, y), 'Small footnotes 6 8 0.01: text and punctuation.', font=font, fill='black')
        return image

    def pdf(self, images, shared=False):
        source = self.root / 'source.pdf'
        with fitz.open() as doc:
            xref = None
            for number, image in enumerate(images):
                page = doc.new_page(width=216, height=288)
                if shared and xref:
                    page.insert_image(page.rect, xref=xref)
                else:
                    stream = io.BytesIO()
                    image.save(stream, format='PNG')
                    xref = page.insert_image(page.rect, stream=stream.getvalue())
                page.insert_text((20, 30), 'OCR text 6 8 0.01', render_mode=3)
            doc.set_metadata({'title': 'Keep title'})
            doc.set_toc([[1, 'Keep bookmark', 1]])
            doc.save(source, deflate=True)
        return source

    def test_native_resolution_threshold_preserves_ocr_and_structure(self):
        source = self.pdf([self.text_image()])
        target = self.root / 'mono.pdf'
        result = compress(source, target, 'mono_text')
        self.assertEqual(result['images_changed'], 1)
        self.assertFalse(result['lossless'])
        self.assertGreater(result['saved_percent'], 0)
        with fitz.open(source) as before, fitz.open(target) as after:
            self.assertEqual(before[0].get_text('words'), after[0].get_text('words'))
            self.assertEqual(before.get_toc(), after.get_toc())
            self.assertEqual(after[0].get_images()[0][2:5], (900, 1200, 1))
            self.assertEqual(before[0].rect, after[0].rect)

    def test_color_gray_panel_and_faint_note_are_retained(self):
        color = self.text_image('RGB')
        ImageDraw.Draw(color).rectangle((40, 500, 500, 900), fill='red')
        shaded = self.text_image()
        ImageDraw.Draw(shaded).rectangle((40, 500, 500, 900), fill=150)
        faint = self.text_image()
        ImageDraw.Draw(faint).text((40, 1170), 'Faint original annotation', font=ImageFont.load_default(size=20), fill=205)
        source = self.pdf([color, shaded, faint])
        result = compress(source, self.root / 'protected.pdf', 'mono_text')
        self.assertEqual(result['changed_pages'], [])
        self.assertTrue(result['image_pixels_identical'])
        reasons = {item['reason'] for item in result['skipped_images']}
        self.assertIn('color_or_managed_image', reasons)
        self.assertIn('faint_marks', reasons)

    def test_shared_image_is_retained_when_only_one_page_selected(self):
        image = self.text_image()
        result = compress(self.pdf([image, image], shared=True), self.root / 'shared.pdf', 'mono_text', pages='1')
        self.assertEqual(result['changed_pages'], [])

    def test_pure_black_white_encoding_is_actually_lossless(self):
        image = self.text_image().point(lambda value: 255 if value >= 180 else 0)
        result = compress(self.pdf([image]), self.root / 'exact.pdf', 'mono_text')
        self.assertTrue(result['lossless'])
        self.assertTrue(result['validation']['image_pixels_identical'])

    def test_cannot_disable_text_protection(self):
        source = self.pdf([self.text_image()])
        with self.assertRaises(ValueError):
            compress(source, self.root / 'unsafe.pdf', 'mono_text', options={'protect_text': False})

    def test_full_mode_uses_exact_threshold_on_color_and_faint_text(self):
        image = self.text_image('RGB')
        draw = ImageDraw.Draw(image)
        draw.rectangle((100, 100, 700, 600), fill=(160, 190, 220))
        draw.text((30, 1170), 'Faint gray original', fill=(205,205,205), font=ImageFont.load_default(size=20))
        source = self.pdf([image])
        result = compress(source, self.root / 'full.pdf', 'mono_all')
        self.assertFalse(result['lossless'])
        self.assertEqual(result['changed_pages'], [1])
        self.assertEqual(result['review_pages'][0]['page'], 1)
        self.assertIn('faint_marks', result['review_pages'][0]['reasons'])
        self.assertIn('color_or_managed_image', result['review_pages'][0]['reasons'])
        self.assertIn('gray_illustration_or_shading', result['review_pages'][0]['reasons'])
        with fitz.open(result['pdf']) as doc:
            pix = fitz.Pixmap(doc, doc[0].get_images()[0][0])
            actual = np.frombuffer(pix.samples,np.uint8).reshape(pix.height,pix.width)
            expected = np.where(np.asarray(image.convert('L')) >= 180,255,0).astype('uint8')
            self.assertTrue(np.array_equal(actual,expected))
            self.assertIn('OCR text',doc[0].get_text())

    def test_review_uses_pdf_page_order_and_only_converted_pages(self):
        plain = self.text_image().point(lambda value: 255 if value >= 180 else 0)
        faint = self.text_image()
        ImageDraw.Draw(faint).text((40, 1170), 'Faint original annotation',
                                 font=ImageFont.load_default(size=20), fill=205)
        result = compress(self.pdf([plain, faint, faint], shared=False), self.root / 'review.pdf', 'mono_all')
        self.assertEqual([p['page'] for p in result['review_pages']], [2, 3])
        self.assertTrue(all('faint_marks' in p['reasons'] for p in result['review_pages']))

    def test_fast_transport_retains_final_pdf_image_stream_and_ocr(self):
        source = self.pdf([self.text_image()])
        save = Image.Image.save
        def exhaustive(image, *args, **kwargs):
            if kwargs.get('format') == 'PNG' and image.mode == '1':
                kwargs['optimize'] = True
            return save(image, *args, **kwargs)
        with patch.object(Image.Image, 'save', exhaustive):
            baseline = compress(source, self.root / 'baseline.pdf', 'mono_all')
        fast = compress(source, self.root / 'fast.pdf', 'mono_all')
        with fitz.open(baseline['pdf']) as before, fitz.open(fast['pdf']) as after:
            a, b = before[0].get_images()[0][0], after[0].get_images()[0][0]
            self.assertEqual(before.xref_stream_raw(a), after.xref_stream_raw(b))
            self.assertEqual(before[0].get_text('words'), after[0].get_text('words'))
        self.assertEqual(baseline['review_pages'], fast['review_pages'])

    def test_transport_falls_back_at_original_stream_size_boundary(self):
        from scantailor_ocr_unified.pdf_bitonal import encode
        from scantailor_ocr_unified.pdf_compression import _analysis, _image_quality
        image = self.text_image()
        binary = Image.fromarray(np.asarray(image) >= 180)
        fast, compact = io.BytesIO(), io.BytesIO()
        binary.save(fast, format='PNG', compress_level=3)
        binary.save(compact, format='PNG', optimize=True)
        boundary = (len(fast.getvalue()) + len(compact.getvalue())) // 2
        self.assertLess(len(compact.getvalue()), boundary)
        payload, _ = encode(image, 180, _analysis(image), _image_quality, lambda: None,
                            guarded=False, max_bytes=boundary)
        self.assertEqual(payload, compact.getvalue())


if __name__ == '__main__':
    unittest.main()
