"""Lossless packing must preserve grayscale edges, binary scans and OCR."""
import hashlib
import io
from pathlib import Path
import tempfile
import unittest

import fitz
from PIL import Image, ImageDraw

from scantailor_ocr_unified.pdf_optimize import compact


class PdfPacking(unittest.TestCase):
    def test_mixed_scans_and_searchable_text_are_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, target = root / 'before.pdf', root / 'after.pdf'
            doc = fitz.open()
            for mode in ('1', 'L', 'RGB'):
                im = Image.new(mode, (620, 870), 'white')
                draw = ImageDraw.Draw(im)
                for n in range(256):
                    shade = int(n > 127) if mode == '1' else n if mode == 'L' else (n, 255-n, n)
                    draw.line((70+n, 90, 70+n, 210), fill=shade)
                draw.text((80, 250), '126 / 307 / 308 / 309', fill='black')
                stream = io.BytesIO()
                im.save(stream, format='PNG')
                page = doc.new_page(width=420, height=594)
                page.insert_image(page.rect, stream=stream.getvalue())
                page.insert_text((40, 270), 'Banken 126 307 308 309', render_mode=3)
            doc.save(source, deflate=True)
            doc.close()
            original_hash = hashlib.sha256(source.read_bytes()).digest()
            result = compact(source, target)
            self.assertEqual(result['pages'], 3)
            self.assertLessEqual(target.stat().st_size, source.stat().st_size)
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), original_hash)
            with fitz.open(target) as output:
                for page in output:
                    self.assertEqual(page.get_text().strip(), 'Banken 126 307 308 309')
                    self.assertEqual(len(page.get_images()), 1)
                    self.assertEqual(page.rect, fitz.Rect(0, 0, 420, 594))
            saved = target.read_bytes()
            with self.assertRaises(FileExistsError):
                compact(source, target)
            self.assertEqual(target.read_bytes(), saved)


if __name__ == '__main__':
    unittest.main()
