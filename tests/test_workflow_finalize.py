import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import fitz
from scantailor_ocr_unified.workflow_finalize import finalize, sha


class FinalizeTests(unittest.TestCase):
    def fixture(self, folder):
        root=Path(folder); project=root/'book'/'images'; project.mkdir(parents=True)
        owner=project.as_posix(); source=root/'original.jpg'; source.write_bytes(b'original')
        native=project/'page.tif'; native.write_bytes(b'generated')
        gray=project/'page.tif.ocr.tif'; gray.write_bytes(b'gray')
        for kind in ['speckles','automask','predespeckle']:
            cache=project/'cache'/kind;cache.mkdir(parents=True);(cache/'page.tif').write_bytes(b'generated native mask')
        cache=project/'.ocr-text-cache'; cache.mkdir(); (cache/'text.pdf').write_bytes(b'cached')
        for path in [project,cache]: (path/'.stou-cache-owner.json').write_text(json.dumps({'project':owner}))
        pdf=root/'final-OCR.pdf'
        with fitz.open() as doc:
            doc.new_page().insert_text((72,72),'Searchable text'); doc.save(pdf)
        previous=root/'visual.pdf'; previous.write_bytes(pdf.read_bytes())
        fingerprint='current'; records=[{'has_ocr':True,'page':1}]
        receipt={'sha256':sha(pdf),'records':records,'project_fingerprint':fingerprint}
        pdf.with_suffix('.workflow.json').write_text(json.dumps(receipt))
        previous.with_suffix('.workflow.json').write_text(json.dumps(receipt))
        registry={'roots':[{'path':str(project),'role':'native'},{'path':str(cache),'role':'ocr'}],'pdfs':[str(previous),str(pdf)],'saved_projects':[]}
        (project/'.project-cache.json').write_text(json.dumps(registry)); (project/'.workflow-state.json').write_text('{}')
        req={'cache_owner':owner,'source_pdf':str(pdf),'source_pdf_sha256':sha(pdf),'project_fingerprint':fingerprint,'work':str(root/'worker'),
             'pages':[{'source':str(native),'gray':str(gray)}]}
        return req,project,pdf,previous,source

    def test_only_searchable_pdf_remains_and_original_is_untouched(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,old,source=self.fixture(folder);events=[];before=sha(pdf)
            result=finalize(req,events.append)
            self.assertTrue(result['ok']);self.assertEqual(sha(pdf),before)
            self.assertEqual(result['retained'],[])
            self.assertFalse(project.exists());self.assertFalse(old.exists());self.assertEqual(source.read_bytes(),b'original')
            self.assertEqual({p.name for p in Path(folder).iterdir()},{'original.jpg','final-OCR.pdf'})
            self.assertIn('verify',{e['phase'] for e in events});self.assertIn('cleanup',{e['phase'] for e in events})

    def test_changed_pdf_refuses_all_deletion(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,old,_=self.fixture(folder);pdf.write_bytes(pdf.read_bytes()+b'changed')
            with self.assertRaises(ValueError):finalize(req)
            self.assertTrue((project/'page.tif').exists());self.assertTrue(old.exists())

    def test_incomplete_ocr_refuses_all_deletion(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,_,_=self.fixture(folder);receipt=pdf.with_suffix('.workflow.json');d=json.loads(receipt.read_text());d['records'][0]['has_ocr']=False;receipt.write_text(json.dumps(d))
            with self.assertRaises(ValueError):finalize(req)
            self.assertTrue((project/'page.tif').exists())

    def test_cancel_during_verification_deletes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,old,_=self.fixture(folder);cancel=Path(folder)/'cancel';req['cancel_file']=str(cancel)
            def stop(event):
                if event['phase']=='verify':cancel.write_text('cancel')
            with self.assertRaises(ValueError):finalize(req,stop)
            self.assertTrue((project/'page.tif').exists());self.assertTrue(old.exists());self.assertTrue(pdf.exists())

    def test_saved_project_dependencies_are_retained(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,_,_,_=self.fixture(folder);saved=Path(folder)/'book.ScanTailor';saved.write_text('project');req['saved_project']=str(saved)
            with self.assertRaises(ValueError):finalize(req)
            self.assertTrue((project/'page.tif').exists())

    def test_unregistered_file_and_changed_old_pdf_survive(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,old,_=self.fixture(folder);unknown=project/'my-note.txt';unknown.write_text('retain');old.write_bytes(old.read_bytes()+b'changed')
            finalize(req)
            self.assertTrue(unknown.exists());self.assertTrue(old.exists());self.assertTrue(pdf.exists())

    def test_failed_delete_retains_recovery_metadata_and_pdf(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,_,_=self.fixture(folder);original=Path.unlink
            def locked(path,*args,**kwargs):
                if path.name=='page.tif':raise PermissionError('in use')
                return original(path,*args,**kwargs)
            with patch.object(Path,'unlink',locked):result=finalize(req)
            self.assertFalse(result['ok']);self.assertTrue(pdf.exists());self.assertTrue(pdf.with_suffix('.workflow.json').exists());self.assertTrue((project/'.project-cache.json').exists())

    def test_other_project_root_is_never_deleted(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,_,_=self.fixture(folder);other=Path(folder)/'other';other.mkdir();(other/'file.png').write_bytes(b'other');(other/'.stou-cache-owner.json').write_text(json.dumps({'project':'someone-else'}))
            registry=project/'.project-cache.json';d=json.loads(registry.read_text());d['roots'].append({'path':str(other),'role':'temporary'});registry.write_text(json.dumps(d))
            finalize(req);self.assertTrue((other/'file.png').exists())

    def test_registered_reading_copy_is_removed_with_its_master(self):
        with tempfile.TemporaryDirectory() as folder:
            req,project,pdf,old,_=self.fixture(folder);copy=Path(folder)/'reading.pdf';copy.write_bytes(old.read_bytes())
            sidecar=old.with_suffix('.workflow.json');record=json.loads(sidecar.read_text());record['compressed']={'pdf':str(copy),'output_sha256':sha(copy)};sidecar.write_text(json.dumps(record))
            registry=project/'.project-cache.json';record=json.loads(registry.read_text());record['pdfs'].append(str(copy));registry.write_text(json.dumps(record))
            result=finalize(req);self.assertTrue(result['ok']);self.assertFalse(copy.exists());self.assertTrue(pdf.exists())

if __name__=='__main__':unittest.main()
