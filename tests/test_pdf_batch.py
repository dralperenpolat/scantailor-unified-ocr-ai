import io
from pathlib import Path
import tempfile
import shutil
import unittest
from unittest.mock import patch

import fitz
from PIL import Image,ImageDraw

from scantailor_ocr_unified import pdf_batch
from scantailor_ocr_unified.pdf_integrity import file_digest
from scantailor_ocr_unified.pdf_paths import native_path


class PdfFolderBatch(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name).resolve()
        def cleanup_long_paths():
            target=self.root.resolve()
            assert target.is_relative_to(Path(tempfile.gettempdir()).resolve()) and target.name.startswith('tmp')
            shutil.rmtree(native_path(target,force=True))
        self.addCleanup(cleanup_long_paths)
        self.source=self.root/'originals'
        self.source.mkdir()
        self.conf={'root':str(self.root),'temp_root':str(self.root/'temp'),'reserve_gib':0}

    def pdf(self,relative='book.pdf',mixed=False):
        path=self.source/relative
        Path(native_path(path.parent,force=True)).mkdir(parents=True,exist_ok=True)
        with fitz.open() as doc:
            first=doc.new_page(width=144,height=192)
            first.insert_text((10,30),'Existing digital text',fontsize=8)
            if mixed:
                image=Image.new('RGB',(600,800),'white')
                ImageDraw.Draw(image).text((30,40),'Scanned original',fill='black')
                stream=io.BytesIO();image.save(stream,format='PNG')
                second=doc.new_page(width=144,height=192)
                second.insert_image(second.rect,stream=stream.getvalue())
                first=doc[0]
                first.insert_link({'kind':fitz.LINK_GOTO,'from':fitz.Rect(10,40,30,50),'page':1})
                first.add_text_annot((20,60),'Original annotation')
            doc.set_toc([[1,'Keep bookmark',1]])
            doc.embfile_add('keep.txt',b'Attachment retained')
            doc.save(native_path(path))
        return path

    def recognize(self,page,conf,work):
        with fitz.open() as doc:
            p=doc.new_page(width=page.rect.width,height=page.rect.height)
            p.insert_text((10,30),'New hidden OCR text',fontsize=8,render_mode=3)
            return doc.tobytes()

    def test_recursive_mirror_same_names_and_adjacent_output_exclusion(self):
        self.pdf('a/same.pdf');self.pdf('b/same.pdf');self.pdf('a/same - küçük.pdf')
        mirror=self.source/'mirror'
        self.pdf('mirror/previous.pdf')
        result=pdf_batch.create([self.source],self.root/'db',mirror)
        self.assertEqual(len(result['jobs']),2)
        self.assertEqual({Path(j['destination']).relative_to(mirror).as_posix() for j in result['jobs']},
                         {'a/same.pdf','b/same.pdf'})

    def test_missing_ocr_only_then_compress_preserves_links_attachments_and_original(self):
        original=self.pdf(mixed=True)
        before=file_digest(original)
        batch=pdf_batch.create([original],self.root/'db',profile='lossless')['batch']
        calls=[]
        def recognize(page,*args):
            calls.append(page.number)
            return self.recognize(page,*args)
        with patch('scantailor_ocr_unified.pdf_batch._recognize',side_effect=recognize):
            result=pdf_batch.run(batch,self.conf)
        job=result['jobs'][0]
        self.assertEqual(job['status'],'complete',job)
        self.assertEqual(calls,[1])
        self.assertEqual(job['ocr_pages'],1)
        self.assertEqual(file_digest(original),before)
        with fitz.open(job['destination']) as doc:
            self.assertEqual(doc[0].get_text().strip(),'Existing digital text')
            self.assertEqual(doc[1].get_text().strip(),'New hidden OCR text')
            self.assertEqual(doc[0].get_links()[0]['page'],1)
            self.assertEqual(list(doc[0].annots())[0].info['content'],'Original annotation')
            self.assertEqual(doc.embfile_get('keep.txt'),b'Attachment retained')

    def test_explicit_replacement_removes_only_own_result(self):
        original=self.pdf()
        batch=pdf_batch.create([original],self.root/'db',self.root/'mirror',profile='lossless')['batch']
        job=pdf_batch.run(batch,self.conf)['jobs'][0]
        result_hash=file_digest(job['destination'])
        replaced=pdf_batch.replace_sources(batch,[job['id']])['jobs'][0]
        self.assertEqual(replaced['status'],'replaced')
        self.assertEqual(file_digest(original),result_hash)
        self.assertFalse(Path(job['destination']).exists())
        self.assertEqual(pdf_batch.replace_sources(batch,[job['id']])['jobs'][0]['status'],'replaced')

    def test_modified_source_is_never_overwritten(self):
        original=self.pdf()
        batch=pdf_batch.create([original],self.root/'db',profile='lossless')['batch']
        job=pdf_batch.run(batch,self.conf)['jobs'][0]
        with original.open('ab') as stream:stream.write(b'\n% user modification\n')
        changed=file_digest(original)
        with self.assertRaisesRegex(ValueError,'sonradan'):
            pdf_batch.replace_sources(batch,[job['id']])
        self.assertEqual(file_digest(original),changed)
        self.assertTrue(Path(native_path(job['destination'],force=True)).exists())

    def test_output_collision_uses_new_name_without_overwriting(self):
        original=self.pdf()
        occupied=original.with_name(original.stem+' - küçük.pdf')
        occupied.write_bytes(b'Existing user output')
        plan=pdf_batch.create([original],self.root/'db',profile='lossless')
        self.assertTrue(plan['jobs'][0]['destination'].endswith('_v2.pdf'))
        self.assertEqual(occupied.read_bytes(),b'Existing user output')

    def test_resume_after_publication_before_status_commit(self):
        original=self.pdf()
        batch=pdf_batch.create([original],self.root/'db',profile='lossless')['batch']
        real=pdf_batch._write_job
        failed=False
        def interrupted(db,job):
            nonlocal failed
            if job['status']=='complete' and not failed:
                failed=True
                raise RuntimeError('interrupted commit')
            return real(db,job)
        with patch('scantailor_ocr_unified.pdf_batch._write_job',side_effect=interrupted):
            self.assertEqual(pdf_batch.run(batch,self.conf)['jobs'][0]['status'],'error')
        self.assertEqual(pdf_batch.run(batch,self.conf)['jobs'][0]['status'],'complete')

    def test_long_source_and_mirror_paths(self):
        relative='/'.join(['nested-'+str(i)+'x'*24 for i in range(7)])+'/book.pdf'
        original=self.pdf(relative)
        self.assertGreater(len(str(original)),240)
        plan=pdf_batch.create([self.source],self.root/'db',self.root/'mirror',profile='lossless')
        job=pdf_batch.run(plan['batch'],self.conf)['jobs'][0]
        self.assertEqual(job['status'],'complete',job)
        self.assertTrue(Path(native_path(job['destination'],force=True)).exists())

    def test_missing_ocr_coordinates_follow_rotated_pages(self):
        source=self.source/'rotated.pdf'
        with fitz.open() as doc:
            for angle in (0,90,180,270):
                page=doc.new_page(width=240,height=320)
                page.set_rotation(angle)
            doc.save(source)
        expected=[]
        def recognize(page,conf,work):
            data=self.recognize(page,conf,work)
            with fitz.open(stream=data,filetype='pdf') as layer:
                expected.append(fitz.Rect(layer[0].get_text('words')[0][:4])*page.derotation_matrix)
            return data
        output=self.root/'ocr-rotated.pdf'
        with patch('scantailor_ocr_unified.pdf_batch._recognize',side_effect=recognize):
            pdf_batch.add_missing_ocr(source,output,self.conf)
        with fitz.open(output) as doc:
            for page,box in zip(doc,expected):
                actual=fitz.Rect(page.get_text('words')[0][:4])
                for x,y in zip(actual,box):self.assertAlmostEqual(x,y,places=2)

    def test_pause_between_files_preserves_completed_work_and_ocr_settings(self):
        self.pdf('a.pdf');self.pdf('b.pdf')
        batch=pdf_batch.create([self.source],self.root/'db',profile='lossless')['batch']
        paused=False
        def notify(event):
            nonlocal paused
            if event.get('status')=='complete':paused=True
        result=pdf_batch.run(batch,{**self.conf,'language':'tur','ocr_engine':'tesseract'},notify,lambda:paused)
        self.assertEqual([j['status'] for j in result['jobs']],['complete','waiting'])
        output=Path(result['jobs'][0]['destination']);stamp=output.stat().st_mtime_ns
        result=pdf_batch.run(batch,self.conf)
        self.assertEqual([j['status'] for j in result['jobs']],['complete','complete'])
        self.assertEqual(output.stat().st_mtime_ns,stamp)
        self.assertEqual(result['settings']['ocr_settings']['language'],'tur')


if __name__=='__main__':unittest.main()
