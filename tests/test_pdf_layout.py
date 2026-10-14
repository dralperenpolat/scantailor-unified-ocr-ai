import io
import json
import tempfile
import unittest
from pathlib import Path

import fitz
from PIL import Image, ImageDraw, ImageFont

from scantailor_ocr_unified.pdf_layout import plan, reframe, content_bounds, verify_text_transform, PT_MM


class PrintedAreaLayout(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='stou_layout_test_')
        self.root = Path(self.temp.name)
        self.source = self.root / 'source.pdf'
        meta = []
        with fitz.open() as doc:
            for count in (30, 30, 5, 0):
                im = Image.new('L', (1000, 1414), 255)
                draw = ImageDraw.Draw(im)
                font = ImageFont.load_default(size=13)
                for y in range(320, 320 + count * 20, 20):
                    draw.text((280, y), 'Body letters with a footnote and isolated page number.', fill=0, font=font)
                if count:
                    draw.text((610, 970), '7', fill=0, font=font)
                stream = io.BytesIO(); im.save(stream, format='PNG')
                p = doc.new_page(width=210 * PT_MM, height=297 * PT_MM)
                p.insert_image(p.rect, stream=stream.getvalue())
                for n in range(count):
                    p.insert_text((p.rect.width * .28, p.rect.height * (320 + n * 20) / 1414 + 6),
                                  'Body letters with a footnote and isolated page number.',
                                  fontsize=6, render_mode=3)
                if count:
                    p.insert_text((p.rect.width * .61, p.rect.height * .69), '7', fontsize=6, render_mode=3)
                meta.append({'body_mm': 1.4 if count > 5 else None,
                             'body_measurement': {'confidence': .8, 'sideways': False}})
            doc.save(self.source)
        self.meta = {'pages': meta}

    def tearDown(self):
        self.temp.cleanup()

    def test_crop_enlarges_body_without_clipping_numbers_or_rewriting_ocr(self):
        layout = plan(self.source, self.meta)
        self.assertGreater(layout['pages'][0]['scale'], 1.5)
        self.assertAlmostEqual(layout['pages'][0]['scale'], layout['pages'][1]['scale'])
        destination = self.root / 'result.pdf'
        result = reframe(self.source, destination, layout)
        self.assertTrue(result['image_pixels_identical'])
        self.assertTrue(result['ocr_text_identical'])
        with fitz.open(self.source) as a, fitz.open(destination) as b:
            for i in range(3):
                self.assertEqual(a[i].get_text(), b[i].get_text())
                self.assertIn('7', b[i].get_text())
                box, _ = content_bounds(b[i])
                self.assertGreaterEqual(box[0], 8.5 * PT_MM)
                self.assertGreaterEqual(box[1], 8.5 * PT_MM)
                self.assertLessEqual(box[2], 201.5 * PT_MM)
                self.assertLessEqual(box[3], 288.5 * PT_MM)

    def test_sparse_and_blank_pages_cannot_inflate_the_book_scale(self):
        layout = plan(self.source, self.meta)
        self.assertLessEqual(layout['pages'][2]['scale'], layout['pages'][0]['scale'] * 1.01)
        self.assertEqual(layout['pages'][3]['scale'], 1.)
        self.assertTrue(layout['pages'][3]['blank'])

    def test_manual_type_size_and_no_overwrite(self):
        layout = plan(self.source, self.meta, target_x_mm=2.)
        self.assertAlmostEqual(layout['pages'][0]['body_mm'], 2.)
        with self.assertRaises(FileExistsError):
            reframe(self.source, self.source, layout)

    def test_worker_can_disable_and_reenable_without_rescaling_twice(self):
        from scantailor_ocr_unified.native_worker import Worker, write_json
        from scantailor_ocr_unified.project import file_hash
        write_json(self.root/'layout-plan.json', self.meta)
        pages = []
        with fitz.open(self.source) as doc:
            for n in range(len(doc)):
                cache=self.root/'.ocr-cache'/str(n);cache.mkdir(parents=True)
                path=cache/'page.pdf';preview=cache/'preview.png'
                with fitz.open() as one:
                    one.insert_pdf(doc,from_page=n,to_page=n)
                    one.save(path)
                doc[n].get_pixmap(dpi=72).save(preview)
                pages.append({'page':n+1,'page_pdf':str(path),'preview':str(preview),'total':len(doc)})
        original_hash = file_hash(self.source)
        worker=object.__new__(Worker)
        worker.job={'title':'Test','pdf':str(self.source),'pdf_hash':original_hash,'pages':pages,'warnings':[]}
        worker.request={'paper':'a4_text','content_fit':True,'margin_mm':10,'target_x_mm':0}
        worker.checkpoint=lambda:None;worker.emit=lambda *a,**k:None;worker.save=lambda:None
        worker.finalize_layout(self.root)
        first=worker.job['pdf'];first_scales=[p['layout_scale'] for p in worker.job['pages']]
        worker.finalize_layout(self.root)
        self.assertEqual(worker.job['pdf'],first)
        worker.request['content_fit']=False;worker.finalize_layout(self.root)
        self.assertEqual(worker.job['pdf'],str(self.source))
        self.assertEqual(worker.job['pages'],[{**p,'warning':False} for p in pages])
        worker.request['content_fit']=True;worker.finalize_layout(self.root)
        self.assertEqual([p['layout_scale'] for p in worker.job['pages']],first_scales)
        self.assertEqual(file_hash(self.source),original_hash)
        self.assertTrue(Path(first).exists())


class TextTransformValidation(unittest.TestCase):
    class Page:
        rect=fitz.Rect(0,0,600,840)
        def __init__(self,text,words):self.text=text;self.words=words
        def get_text(self,kind=None):return self.words if kind=='words' else self.text

    def setUp(self):
        self.row={'page':1,'crop_pt':[100,100,500,700],'dest_pt':[20,20,500,740]}
        self.before=self.Page('Text  \n7\n',[(120,120,180,135,'Text'),(490,600,530,620,'7')])
        self.after=self.Page('Text \n7\n',[(44,44,116,62,'Text'),(488,620,536,644,'7')])

    def test_redundant_spaces_are_not_missing_text(self):
        check=verify_text_transform(self.before,self.after,self.row)
        self.assertTrue(check['whitespace_changed'])

    def test_actual_missing_page_number_and_misalignment_are_rejected(self):
        with self.assertRaises(ValueError):
            verify_text_transform(self.before,self.Page('Text\n',self.after.words[:1]),self.row)
        moved=list(self.after.words);moved[1]=(500,620,548,644,'7')
        with self.assertRaises(ValueError):
            verify_text_transform(self.before,self.Page(self.after.text,moved),self.row)

    def test_inherited_bad_box_is_flagged_but_new_overflow_is_rejected(self):
        old=self.Page('X1',[(490,600,650,620,'X1')])
        new=self.Page('X1',[(488,620,680,644,'X1')])
        self.assertEqual(verify_text_transform(old,new,self.row)['preexisting_ocr_boxes_outside_page'],1)
        old_inside=self.Page('X1',[(490,600,590,620,'X1')])
        new_outside=self.Page('X1',[(488,620,608,644,'X1')])
        with self.assertRaisesRegex(ValueError,'sayfa dışına'):
            verify_text_transform(old_inside,new_outside,self.row)


if __name__ == '__main__':
    unittest.main(verbosity=2)
