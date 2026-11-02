import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import fitz

from scantailor_ocr_unified.final_delivery import finalize
from scantailor_ocr_unified.pdf_integrity import file_digest
from scantailor_ocr_unified.queue_lock import exclusive_queue
from scantailor_ocr_unified.queue_store import QueueStore


class SinglePdfDelivery(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.queue = self.root / 'queue.json'
        self.output = self.root / 'output'
        self.source = self.root / 'originals'
        self.source.mkdir()

    def make_queue(self, count=1, incomplete=False):
        jobs = []
        for i in range(count):
            identifier = f'{i+1:08d}-job'
            title = f'Book {i+1}'
            directory = self.output / f'{title}_{identifier[:8]}'
            directory.mkdir(parents=True)
            source = directory / (title + ' - Reading.pdf')
            with fitz.open() as doc:
                page = doc.new_page(width=144, height=192)
                page.insert_text((10, 30), 'Preserved OCR text 6 8 0.01', fontsize=8)
                doc.save(source)
            (directory / (title + '.pdf')).write_bytes(source.read_bytes())
            (directory / (title + ' - OCR Pending.pdf')).write_bytes(source.read_bytes())
            (directory / 'kitap.ScanTailor').write_text('Keep manual selections')
            original = self.source / f'{title}.pdf'
            original.write_bytes(source.read_bytes())
            jobs.append({'id': identifier, 'title': title, 'folder': str(self.source),
                         'source_pdf': str(original), 'pdf': str(source), 'reading_pdf': str(source),
                         'pdf_hash': file_digest(source), 'delivery_folder': str(directory),
                         'visual_pdf': str(directory / (title + ' - OCR Pending.pdf')),
                         'verification': {'pages': 1, 'ocr_timing': 'after'}, 'pages': []})
        if incomplete:
            jobs[-1]['error'] = 'OCR not complete'
        data = {'jobs': jobs, 'output_root': str(self.output)}
        self.queue.write_text(json.dumps(data), encoding='utf8')
        store = QueueStore(self.queue)
        store.load(data)
        store.close()
        return data

    def test_one_pdf_originals_projects_and_ocr_preserved(self):
        self.make_queue(2)
        originals = {p: p.read_bytes() for p in self.source.glob('*.pdf')}
        result = finalize(self.queue, options={})
        self.assertEqual(result['books'], 2)
        data = json.loads(self.queue.read_text('utf8'))
        for job in data['jobs']:
            directory = Path(job['delivery_folder'])
            self.assertEqual(list(directory.rglob('*.pdf')), [Path(job['reading_pdf'])])
            self.assertEqual(job['pdf'], job['reading_pdf'])
            self.assertEqual(file_digest(job['pdf']), job['pdf_hash'])
            self.assertTrue(job['final_delivery']['cleaned'])
            self.assertTrue((directory / 'kitap.ScanTailor').exists())
            with fitz.open(job['pdf']) as doc:
                self.assertIn('Preserved OCR text 6 8 0.01', doc[0].get_text())
        for path, payload in originals.items():
            self.assertEqual(path.read_bytes(), payload)
        self.assertEqual(finalize(self.queue, options={})['books'], 2)

    def test_incomplete_queue_and_active_worker_cannot_trigger_cleanup(self):
        self.make_queue(2, incomplete=True)
        before = {p: file_digest(p) for p in self.output.rglob('*.pdf')}
        with self.assertRaisesRegex(ValueError, 'bitmeden'):
            finalize(self.queue, options={})
        with exclusive_queue(self.queue), self.assertRaises(RuntimeError):
            finalize(self.queue, options={})
        self.assertEqual(before, {p: file_digest(p) for p in self.output.rglob('*.pdf')})

    def test_interruption_before_all_books_publish_keeps_all_old_variants(self):
        self.make_queue(2)
        from scantailor_ocr_unified.pdf_compression import compress
        calls = 0
        def interrupted(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise RuntimeError('simulated interruption')
            return compress(*args, **kwargs)
        with patch('scantailor_ocr_unified.final_delivery.compress', side_effect=interrupted), self.assertRaises(RuntimeError):
            finalize(self.queue, options={})
        for folder in self.output.iterdir():
            self.assertEqual(len(list(folder.glob('*.pdf'))), 3)
        self.assertEqual(finalize(self.queue, options={})['books'], 2)

    def test_recover_replacement_before_queue_commit(self):
        self.make_queue()
        with patch.object(QueueStore, 'save', side_effect=RuntimeError('commit interrupted')), self.assertRaises(RuntimeError):
            finalize(self.queue, options={})
        self.assertEqual(finalize(self.queue, options={})['books'], 1)
        self.assertEqual(len(list(self.output.rglob('*.pdf'))), 1)

    def test_source_inside_output_is_never_removed(self):
        data = self.make_queue()
        store = QueueStore(self.queue)
        loaded = store.load(data)
        loaded['jobs'][0]['folder'] = loaded['jobs'][0]['delivery_folder']
        store.save(loaded)
        store.publish(loaded)
        store.close()
        with self.assertRaisesRegex(ValueError, 'Özgün kaynak'):
            finalize(self.queue, options={})
        self.assertEqual(len(list(self.output.rglob('*.pdf'))), 3)


if __name__ == '__main__':
    unittest.main()
