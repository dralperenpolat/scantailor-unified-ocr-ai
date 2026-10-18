"""User-visible invariants: content, page scope, quality, cancellation and collisions."""
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import fitz
import numpy as np
from PIL import Image, ImageDraw

from scantailor_ocr_unified.pdf_profiles import CompressionCancelled, compress, options_signature, preview
from scantailor_ocr_unified.pdf_integrity import verify


class AdaptiveCompression(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def photo(self, seed=3):
        image = Image.fromarray(np.random.default_rng(seed).integers(0, 256, (480, 360, 3), dtype=np.uint8))
        stream = io.BytesIO()
        image.save(stream, format='PNG')
        return stream.getvalue()

    def document(self, shared=False, rich=False):
        source = self.root / 'source.pdf'
        with fitz.open() as doc:
            first = doc.new_page(width=144, height=192)
            xref = first.insert_image(first.rect, stream=self.photo())
            first.insert_text((12, 30), 'Original 6 / 8 / 0.01', render_mode=3)
            second = doc.new_page(width=192, height=144)
            if shared:
                second.insert_image(second.rect, xref=xref, rotate=90)
            else:
                second.insert_image(second.rect, stream=self.photo(9))
            second.set_rotation(90)
            second.insert_text((12, 30), 'Second page', render_mode=3)
            if rich:
                doc.set_metadata({'title': 'Test book', 'author': 'Author'})
                doc.set_toc([[1, 'Start', 1], [2, 'Rotated page', 2]])
                doc.set_page_labels([{'startpage': 0, 'style': 'r', 'firstpagenum': 1}])
                doc.embfile_add('notes.txt', b'Original attachment', filename='notes.txt')
                page = doc[0]
                page.add_text_annot((20, 40), 'Original comment')
                page.insert_link({'kind': fitz.LINK_GOTO, 'from': fitz.Rect(10, 10, 30, 20), 'page': 1})
                widget = fitz.Widget()
                widget.field_name = 'note'
                widget.field_type = fitz.PDF_WIDGET_TYPE_TEXT
                widget.rect = fitz.Rect(20, 100, 100, 120)
                widget.field_value = 'Keep field'
                page.add_widget(widget)
            doc.save(source)
        return source

    def assert_clean(self):
        self.assertEqual(list(self.root.glob('.stou-compress-*')), [])

    def test_lossless_keeps_pixels_forms_navigation_comments_and_attachments(self):
        source = self.document(rich=True)
        target = self.root / 'lossless.pdf'
        original = source.read_bytes()
        report = compress(source, target, 'lossless')
        self.assertTrue(report['lossless'])
        self.assertLessEqual(target.stat().st_size, len(original))
        with fitz.open(source) as a, fitz.open(target) as b:
            self.assertEqual(a.get_toc(), b.get_toc())
            self.assertEqual(a.get_page_labels(), b.get_page_labels())
            self.assertEqual(b.embfile_get('notes.txt'), b'Original attachment')
            self.assertEqual(list(b[0].widgets())[0].field_value, 'Keep field')
            self.assertEqual(list(b[0].annots())[0].info['content'], 'Original comment')
            self.assertEqual(b[0].get_links()[0]['page'], 1)
            for p, q in zip(a, b):
                self.assertEqual(p.get_pixmap(dpi=300).samples, q.get_pixmap(dpi=300).samples)
        self.assertEqual(source.read_bytes(), original)
        self.assert_clean()

    def test_deep_repack_preserves_content_and_can_cancel_during_write(self):
        try:
            import pikepdf
        except ImportError:
            self.skipTest('Deep lossless repacking requires pikepdf in the release runtime')
        source = self.document(rich=True)
        events = []
        report = compress(source, self.root / 'deep.pdf', 'lossless', events.append,
                          options={'recompress_flate': True})
        self.assertTrue(report['lossless'])
        self.assertTrue(any(e['total'] == 100 for e in events), 'qpdf reports write progress')
        verify(source, report['pdf'])
        stopped = False
        def progress(event):
            nonlocal stopped
            if event['stage'] == 'Kayıpsız PDF yeniden paketleniyor' and event['total'] == 100:
                stopped = True
        with self.assertRaises(CompressionCancelled):
            compress(source, self.root / 'deep-cancelled.pdf', 'lossless', progress,
                     options={'recompress_flate': True}, cancelled=lambda: stopped)
        self.assertFalse((self.root / 'deep-cancelled.pdf').exists())
        self.assert_clean()

    def test_custom_profile_changes_selected_image_only_and_keeps_ocr_coordinates(self):
        source = self.document(rich=True)
        target = self.root / 'selected.pdf'
        report = compress(source, target, 'custom', pages='2', options={'dpi': 200, 'jpeg_quality': 82})
        self.assertEqual(report['changed_pages'], [2])
        self.assertEqual(report['selected_pages'], [2])
        self.assertGreater(report['saved_percent'], 0)
        with fitz.open(source) as a, fitz.open(target) as b:
            self.assertEqual(a[0].get_pixmap(dpi=300).samples, b[0].get_pixmap(dpi=300).samples)
            self.assertEqual(a[1].get_text('words'), b[1].get_text('words'))
            self.assertEqual(a[1].rotation, b[1].rotation)
            self.assertEqual(a.get_toc(), b.get_toc())
        self.assert_clean()

    def test_shared_image_does_not_modify_an_unselected_page(self):
        source = self.document(shared=True)
        target = self.root / 'shared.pdf'
        report = compress(source, target, 'small', pages='1')
        self.assertEqual(report['changed_pages'], [])
        self.assertIn('shared_with_unselected_page', [row['reason'] for row in report['skipped_images']])
        with fitz.open(source) as a, fitz.open(target) as b:
            self.assertEqual(a[1].get_pixmap(dpi=300).samples, b[1].get_pixmap(dpi=300).samples)

    def test_target_search_reports_unreachable_target_without_destroying_quality(self):
        source = self.document()
        target = self.root / 'target.pdf'
        report = compress(source, target, 'target', options={'target_mib': .001})
        self.assertFalse(report['target_met'])
        self.assertGreater(report['after_bytes'], report['target_bytes'])
        self.assertLessEqual(report['after_bytes'], report['before_bytes'])
        self.assertIn('sağlanamadı', report['note'])
        self.assertEqual(report['output_sha256'], hashlib.sha256(target.read_bytes()).hexdigest())
        self.assert_clean()

    def test_already_reachable_target_uses_lossless_result(self):
        source = self.document()
        result = compress(source, self.root / 'enough.pdf', 'target', options={'target_mib': 25})
        self.assertTrue(result['target_met'])
        self.assertTrue(result['lossless'])
        self.assertEqual(len(result['attempts']), 1)

    def test_cancel_mid_attempt_leaves_no_output_or_temporary_pdf(self):
        source = self.document()
        target = self.root / 'cancelled.pdf'
        cancel = [False]
        def notify(event):
            if event['stage'] == 'Sayfaya uygun sıkıştırma hazırlanıyor':
                cancel[0] = True
        with self.assertRaises(CompressionCancelled):
            compress(source, target, 'small', notify, cancelled=lambda: cancel[0])
        self.assertFalse(target.exists())
        self.assert_clean()

    def test_destination_created_while_working_is_never_overwritten(self):
        source = self.document()
        target = self.root / 'collision.pdf'
        def notify(event):
            if not target.exists():
                target.write_bytes(b'Existing user file')
        with self.assertRaises(FileExistsError):
            compress(source, target, 'small', notify)
        self.assertEqual(target.read_bytes(), b'Existing user file')
        self.assert_clean()

    def test_changed_source_is_detected_before_publication(self):
        source = self.document()
        target = self.root / 'stale.pdf'
        changed = [False]
        def notify(event):
            if event['stage'] == 'PDF bütünlüğü doğrulanıyor' and event['page'] == 2 and not changed[0]:
                with source.open('ab') as stream:
                    stream.write(b'\n% changed by another process\n')
                changed[0] = True
        with self.assertRaisesRegex(ValueError, 'işlem sırasında değişti'):
            compress(source, target, 'small', notify)
        self.assertFalse(target.exists())
        self.assert_clean()

    def test_encrypted_pdf_keeps_password_and_does_not_report_it(self):
        source = self.root / 'private.pdf'
        with fitz.open() as doc:
            page = doc.new_page(width=144, height=192)
            page.insert_image(page.rect, stream=self.photo())
            doc.save(source, encryption=fitz.PDF_ENCRYPT_AES_256, user_pw='reader', owner_pw='owner')
        target = self.root / 'encrypted.pdf'
        result = compress(source, target, 'small', options={'password': 'reader'})
        self.assertNotIn('password', result['options'])
        with fitz.open(target) as doc:
            self.assertTrue(doc.needs_pass)
            self.assertTrue(doc.authenticate('reader'))
            self.assertEqual(len(doc), 1)

    def test_invalid_settings_and_existing_output_fail_without_side_effects(self):
        source = self.document()
        for options in ({'dpi': float('nan')}, {'jpeg_quality': 101}, {'target_mib': 0},
                        {'min_dpi': 400}, {'color': 'unknown'}, {'typo': 1}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                compress(source, self.root / 'invalid.pdf', 'target', options=options)
        with self.assertRaises(FileExistsError):
            compress(source, source)
        self.assertFalse((self.root / 'invalid.pdf').exists())
        self.assert_clean()

    def test_small_print_survives_aggressive_custom_settings(self):
        image = Image.new('RGB', (900, 1200), 'white')
        draw = ImageDraw.Draw(image)
        for y in range(20, 1180, 18):
            draw.text((20, y), 'Tiny notes 6 8 0.01 123456789 and punctuation.', fill='black')
        stream = io.BytesIO()
        image.save(stream, format='PNG')
        source, target = self.root / 'fine.pdf', self.root / 'fine-small.pdf'
        with fitz.open() as doc:
            page = doc.new_page(width=216, height=288)
            page.insert_image(page.rect, stream=stream.getvalue())
            doc.save(source)
        result = compress(source, target, 'custom', options={'dpi': 72, 'jpeg_quality': 40})
        with fitz.open(target) as doc:
            pix = fitz.Pixmap(doc, doc[0].get_images()[0][0])
            visible = Image.frombytes('RGB', (pix.width, pix.height), pix.samples).resize(image.size)
            before, after = np.asarray(image.convert('L')), np.asarray(visible.convert('L'))
            ink = before < 100
            self.assertLess(float(np.mean(after[ink] > 200)), .02)
        self.assertLessEqual(result['after_bytes'], result['before_bytes'])

    def test_integrity_rejects_visible_corruption_even_with_identical_ocr(self):
        source = self.document()
        target = self.root / 'corrupt.pdf'
        with fitz.open(source) as doc:
            stream = io.BytesIO()
            Image.new('RGB', (360, 480), 'black').save(stream, format='PNG')
            doc[0].replace_image(doc[0].get_images()[0][0], stream=stream.getvalue())
            doc.save(target)
        with self.assertRaisesRegex(ValueError, 'kalite sınırını'):
            verify(source, target, {0})

    def test_cache_signature_tracks_options_and_preview_rejects_existing_folder(self):
        self.assertNotEqual(options_signature('custom', {'dpi': 300}), options_signature('custom', {'dpi': 200}))
        self.assertNotEqual(options_signature('target', {'target_mib': 10}), options_signature('target', {'target_mib': 20}))
        source = self.document()
        with self.assertRaises(FileExistsError):
            preview(source, self.root)

    def test_queue_rebuilds_when_options_change_or_output_is_missing_or_modified(self):
        from scantailor_ocr_unified.native_worker import Worker
        from scantailor_ocr_unified.pdf_integrity import file_digest
        source = self.document()
        worker = Worker.__new__(Worker)
        worker.root = str(self.root)
        worker.request = {'structured_text': False, 'compression_profile': 'custom', 'compression_options': {'dpi': 300}}
        worker.job = {'pdf': str(source), 'verification': True, 'pdf_hash': file_digest(source), 'title': 'Book', 'pages': []}
        worker.emit = lambda *args, **kwargs: None
        worker.checkpoint = lambda: None
        with patch('scantailor_ocr_unified.storage.cleanup_plan', return_value={'files': []}):
            worker.finish_book(self.root)
            first = Path(worker.job['compressed']['pdf'])
            worker.finish_book(self.root)
            self.assertEqual(worker.job['compressed']['pdf'], str(first))
            worker.request['compression_options']['dpi'] = 200
            worker.finish_book(self.root)
            second = Path(worker.job['compressed']['pdf'])
            self.assertNotEqual(first, second)
            self.assertTrue(first.exists())
            second.write_bytes(b'Modified output')
            worker.finish_book(self.root)
            third = Path(worker.job['compressed']['pdf'])
            self.assertNotEqual(second, third)
            self.assertEqual(second.read_bytes(), b'Modified output')
            third.unlink()
            worker.finish_book(self.root)
            self.assertTrue(Path(worker.job['compressed']['pdf']).is_file())
            source.write_bytes(source.read_bytes()+b'\n% externally changed')
            with self.assertRaisesRegex(ValueError, 'uygulama dışında değişti'):
                worker.finish_book(self.root)


if __name__ == '__main__':
    unittest.main()
