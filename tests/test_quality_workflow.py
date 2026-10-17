import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import fitz
import numpy as np
from PIL import Image
from scantailor_ocr_unified.benchmark import scores,create_corpus,annotate,evaluate,distance
from scantailor_ocr_unified.quality import integrity,export_text
from scantailor_ocr_unified.smart_pdf import page_range,process
from scantailor_ocr_unified.pdf_export import export
from scantailor_ocr_unified.pdf_profiles import compress
from scantailor_ocr_unified.archive_pdf import validation_report


class QualityWorkflow(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
    def document(self):
        path=self.root/'source.pdf'
        with fitz.open() as doc:
            page=doc.new_page(width=300,height=400);page.insert_text((30,40),'Digital legal text 123.');page.draw_rect(fitz.Rect(20,70,100,100),color=(1,0,0))
            doc.set_metadata({'title':'Test book','author':'Test author'});doc.set_toc([[1,'Chapter',1]]);doc.set_page_labels([{'startpage':0,'prefix':'','style':'r','firstpagenum':1}]);doc.save(path)
        return path
    def test_ranges_are_unique_ordered_and_validated(self):
        self.assertEqual(page_range('3,1-3,5',5),[0,1,2,4])
        for bad in ('0','3-1','9','1-x'):
            with self.assertRaises(ValueError):page_range(bad,5)
    def test_fast_edit_distance_matches_dynamic_programming_oracle(self):
        import random
        rng=random.Random(50)
        def oracle(a,b):
            row=list(range(len(b)+1))
            for i,left in enumerate(a,1):
                next_row=[i]
                for j,right in enumerate(b,1):next_row.append(min(row[j]+1,next_row[-1]+1,row[j-1]+(left!=right)))
                row=next_row
            return row[-1]
        for _ in range(200):
            a=[rng.choice(['a','b','ğ','İ','word']) for _ in range(rng.randrange(60))]
            b=[rng.choice(['a','b','ğ','İ','word']) for _ in range(rng.randrange(60))]
            self.assertEqual(distance(a,b),oracle(a,b))
    def test_digital_pdf_keeps_vectors_text_navigation_and_source(self):
        source=self.document();original=source.read_bytes();target=self.root/'smart.pdf'
        result=process(source,target,{'reserve_gib':0})
        self.assertEqual(result['pages'][0]['method'],'preserved');self.assertEqual(source.read_bytes(),original)
        with fitz.open(source) as before,fitz.open(target) as after:
            self.assertEqual(before[0].get_text(),after[0].get_text());self.assertEqual(len(before[0].get_drawings()),len(after[0].get_drawings()))
            self.assertEqual(after.get_toc(),before.get_toc());self.assertEqual(after[0].get_label(),'i');self.assertEqual(after.metadata['title'],'Test book')
    def test_scan_gets_text_overlay_without_replacing_image(self):
        source=self.root/'scan.pdf';stream=io.BytesIO();Image.new('RGB',(200,300),'white').save(stream,format='PNG')
        with fitz.open() as doc:
            page=doc.new_page(width=200,height=300);page.insert_image(page.rect,stream=stream.getvalue());doc.save(source)
        with fitz.open() as layer:
            page=layer.new_page(width=200,height=300);page.insert_text((20,50),'Recognized text',render_mode=3);payload=layer.tobytes()
        with patch.object(fitz.Pixmap,'pdfocr_tobytes',return_value=payload):process(source,self.root/'out.pdf',{'reserve_gib':0})
        with fitz.open(self.root/'out.pdf') as doc:self.assertIn('Recognized text',doc[0].get_text());self.assertEqual(len(doc[0].get_images()),1)
    def test_failed_pdf_export_does_not_publish_partial_output(self):
        display=self.root/'display.tif';Image.new('L',(100,150),255).save(display)
        config=self.root/'export.json';config.write_text(json.dumps({'ocr_engine':'none','reserve_gib':0,'temp_root':str(self.root/'temp'),
            'pages':[{'display_image':str(display)}]}))
        with patch('scantailor_ocr_unified.pdf_export.verify_page',side_effect=ValueError('pixels changed')):
            with self.assertRaises(ValueError):export(config,self.root/'result.pdf')
        self.assertFalse((self.root/'result.pdf').exists());self.assertEqual(list((self.root/'temp').iterdir()),[])
        result=export(config,self.root/'result.pdf',notify=lambda _:None);self.assertTrue(result['checks'][0]['pixel_identical'])
    def test_rotated_scan_overlay_has_matching_visible_coordinates(self):
        source=self.root/'rotated.pdf';stream=io.BytesIO();Image.new('RGB',(200,300),'white').save(stream,format='PNG')
        with fitz.open() as doc:
            page=doc.new_page(width=200,height=300);page.insert_image(page.rect,stream=stream.getvalue());page.set_rotation(90);doc.save(source)
        with fitz.open() as layer:
            page=layer.new_page(width=300,height=200);page.insert_text((20,50),'Rotated',render_mode=3);expected=fitz.Rect(page.get_text('words')[0][:4]);payload=layer.tobytes()
        with patch.object(fitz.Pixmap,'pdfocr_tobytes',return_value=payload):process(source,self.root/'rotated-out.pdf',{'reserve_gib':0})
        with fitz.open(self.root/'rotated-out.pdf') as doc:
            page=doc[0];actual=fitz.Rect(page.get_text('words')[0][:4])*page.rotation_matrix
            self.assertEqual(page.rotation,90)
            for a,b in zip(actual,expected):self.assertAlmostEqual(a,b,places=3)
    def test_corpus_reads_final_book_when_page_cache_was_reclaimed(self):
        source=self.document();queue=self.root/'final-queue.json';queue.write_text(json.dumps({'jobs':[{'id':'b','title':'Book','pdf':str(source),'pages':[{'page':1,'page_pdf':str(self.root/'removed-cache.pdf')}]}]}))
        result=create_corpus(queue,self.root/'final-corpus',250)
        self.assertEqual(result['sampled'],1)
        row=json.loads(Path(result['manifest']).read_text('utf8'))['pages'][0]
        self.assertEqual(row['pdf'],str(source.resolve()));self.assertEqual(row['pdf_page_index'],0)
    def test_lossy_profile_preserves_text_and_reports_actual_changes(self):
        image=Image.fromarray(np.random.default_rng(3).integers(0,256,(900,700,3),dtype=np.uint8));stream=io.BytesIO();image.save(stream,format='PNG')
        source=self.root/'photo.pdf'
        with fitz.open() as doc:
            page=doc.new_page(width=200,height=260);page.insert_image(page.rect,stream=stream.getvalue());page.insert_text((10,20),'Keep this text',render_mode=3);doc.save(source)
        report=compress(source,self.root/'small.pdf','small')
        self.assertFalse(report['lossless']);self.assertGreater(report['images_changed'],0);self.assertLess(report['after_bytes'],report['before_bytes']);self.assertTrue(report['text_identical'])
    def test_benchmark_excludes_unverified_ocr_and_retains_reference_revisions(self):
        source=self.document();queue=self.root/'queue.json';queue.write_text(json.dumps({'jobs':[{'id':'b','title':'Book','pages':[{'page':1,'page_pdf':str(source)}]}]}))
        result=create_corpus(queue,self.root/'corpus',250);manifest=Path(result['manifest'])
        self.assertEqual(evaluate(manifest)['verified'],0);self.assertIsNone(evaluate(manifest)['totals']['CER'])
        annotate(manifest,'0001','Digital legal text 123.\n',['body']);first=json.loads(manifest.read_text('utf8'))['pages'][0]['reference']
        self.assertEqual(evaluate(manifest)['totals']['CER'],0)
        annotate(manifest,'0001','Correction');self.assertTrue((manifest.parent/first).exists())
        self.assertEqual(scores('abc','adc')['CER'],1/3);self.assertIsNone(scores('','text')['CER'])
    def test_integrity_and_structured_exports_are_explicit_about_uncertainty(self):
        result=integrity([{'page':1,'pixels_sha256':'a'},{'page':3,'pixels_sha256':'a'}],3)
        self.assertEqual({r['code'] for r in result},{'page_order','page_count','duplicate_image'})
        files=export_text(self.document(),self.root/'text');self.assertIn('Digital legal text',Path(files['txt']).read_text('utf8'))
        self.assertIn('role_inferred',Path(files['layout.json']).read_text('utf8'))
    def test_pdfa_requires_positive_validation_record(self):
        self.assertFalse(validation_report('<report/>')['compliant'])
        self.assertFalse(validation_report('<report><validationReport isCompliant="false"/></report>')['compliant'])
        self.assertTrue(validation_report('<report><validationReport isCompliant="true" profileName="PDF/A-2B"/></report>')['compliant'])


if __name__=='__main__':unittest.main()
