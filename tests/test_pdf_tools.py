import tempfile
import unittest
import io
import json
from pathlib import Path
import fitz
from PIL import Image, ImageDraw
from scantailor_ocr_unified.pdf_tools import import_pdf, compact_pdf, digest

class PdfToolsTest(unittest.TestCase):
    def scan_bytes(self, mode='RGB'):
        image = Image.new(mode, (180, 240), 'white')
        draw = ImageDraw.Draw(image)
        draw.rectangle((10, 15, 60, 70), fill='black')
        draw.text((15, 100), 'SCAN 123', fill='black')
        stream = io.BytesIO(); image.save(stream, format='PNG')
        return image, stream.getvalue()

    def test_native_scan_preserves_pixels_rotation_margins_and_hidden_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root/'Türkçe tarama.pdf'
            expected = []
            with fitz.open() as doc:
                for rotation, mode in ((0, 'L'), (90, 'RGB'), (180, 'RGB'), (270, 'RGB')):
                    scan, data = self.scan_bytes(mode)
                    page = doc.new_page(width=200, height=260)
                    page.insert_image(fitz.Rect(10, 10, 190, 250), stream=data)
                    page.insert_text((20, 60), 'Original hidden OCR', render_mode=3)
                    page.set_rotation(rotation)
                    canvas = Image.new(mode, (200, 260), 'white'); canvas.paste(scan, (10, 10))
                    expected.append(canvas.rotate(-rotation, expand=True))
                doc.save(source)
            original = digest(source)
            result = import_pdf(source, root/'import', 300)
            self.assertEqual((result['extracted'], result['rendered']), (4, 0))
            for n, canvas in enumerate(expected, 1):
                with Image.open(Path(result['folder'])/f'page-{n:05d}.png') as actual:
                    self.assertEqual(actual.size, canvas.size)
                    self.assertEqual(actual.tobytes(), canvas.tobytes())
                meta = json.loads((Path(result['text_folder'])/f'page-{n:05d}.json').read_text('utf8'))
                self.assertIn('Original hidden OCR', meta['text'])
                self.assertTrue(meta['native_pixels_verified'])
            self.assertEqual(digest(source), original)

    def test_complex_pages_keep_visible_content_and_pdf_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root/'mixed.pdf'
            _, data = self.scan_bytes()
            with fitz.open() as doc:
                # Visible text overlay; two scans; annotations; crop; transparency.
                for case in range(5):
                    page = doc.new_page(width=180, height=240)
                    page.insert_image(page.rect, stream=data)
                    if case == 0: page.insert_text((10, 140), 'VISIBLE OVERLAY')
                    if case == 1: page.insert_image(fitz.Rect(70, 50, 160, 170), stream=data)
                    if case == 2: page.add_rect_annot(fitz.Rect(15, 15, 120, 120)).update()
                    if case == 3: page.set_cropbox(fitz.Rect(20, 20, 150, 210))
                    if case == 4:
                        # A clipping path does not appear in get_bboxlog. The
                        # native-pixel comparison must reject raw extraction.
                        contents = page.get_contents()[0]
                        doc.update_stream(contents, b'q 20 20 60 60 re W n\n'+doc.xref_stream(contents)+b'\nQ')
                doc.save(source)
            result = import_pdf(source, root/'import', 150)
            self.assertEqual((result['extracted'], result['rendered']), (0, 5))
            with fitz.open(source) as doc:
                for n, page in enumerate(doc, 1):
                    reference = page.get_pixmap(dpi=150, colorspace=fitz.csRGB, alpha=False)
                    with Image.open(Path(result['folder'])/f'page-{n:05d}.png') as actual:
                        self.assertEqual(actual.tobytes(), reference.samples)

    def test_render_choice_and_interrupted_import_are_separate_and_resumable(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root/'scan.pdf'; _, data = self.scan_bytes()
            with fitz.open() as doc:
                for _ in range(3):
                    page = doc.new_page(width=180, height=240); page.insert_image(page.rect, stream=data)
                doc.save(source)
            def stop(event):
                if event['page'] == 2: raise InterruptedError('cancel')
            with self.assertRaises(InterruptedError): import_pdf(source, root/'import', 150, stop)
            events = []
            result = import_pdf(source, root/'import', 150, events.append)
            self.assertEqual([e['cached'] for e in events], [True, True, False])
            self.assertTrue(json.loads((Path(result['text_folder'])/'import.json').read_text('utf8'))['complete'])
            rendered = import_pdf(source, root/'import', 150, strategy='render')
            self.assertNotEqual(result['folder'], rendered['folder'])
            self.assertEqual(rendered['rendered'], 3)
            with self.assertRaises(ValueError): import_pdf(source, root/'import', strategy='invalid')

    def test_password_protected_pdf_is_not_silently_empty(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root/'private.pdf'
            with fitz.open() as doc:
                doc.new_page(); doc.save(source, encryption=fitz.PDF_ENCRYPT_AES_256,
                                         owner_pw='owner', user_pw='reader')
            with self.assertRaisesRegex(ValueError, 'şifreli'):
                import_pdf(source, root/'import')

    def test_import_resume_text_and_pack(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); source=root/'Kitap.pdf'
            with fitz.open() as doc:
                for n in range(3):
                    page=doc.new_page(width=300,height=450)
                    page.insert_text((24,48),f'Page {n+1} - readable source text.')
                    page.draw_rect(fitz.Rect(25,70,200,200),color=(0,0,1))
                doc.save(source,deflate=False)
            original=digest(source); events=[]
            result=import_pdf(source,root/'import',150,events.append)
            self.assertEqual(result['pages'],3)
            self.assertEqual(len(list(Path(result['folder']).glob('*.png'))),3)
            self.assertIn('readable source text',(Path(result['text_folder'])/'page-00001.json').read_text('utf8'))
            events.clear();import_pdf(source,root/'import',150,events.append)
            self.assertTrue(all(e['cached'] for e in events))
            (Path(result['folder'])/'page-00002.png').write_bytes(b'broken')
            events.clear();import_pdf(source,root/'import',150,events.append)
            self.assertEqual([e['cached'] for e in events],[True,False,True])
            packed=compact_pdf(source,root/'small.pdf')
            self.assertLessEqual(packed['after_bytes'],packed['before_bytes'])
            self.assertTrue(packed['text_identical'] and packed['renders_identical'])
            self.assertEqual(digest(source),original)
            with self.assertRaises(FileExistsError):compact_pdf(source,source)
            with self.assertRaises(FileExistsError):compact_pdf(source,root/'small.pdf')

if __name__=='__main__': unittest.main()
