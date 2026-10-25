import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock,patch
from scantailor_ocr_unified.work_storage import publish_book
from scantailor_ocr_unified.native_worker import Worker,fingerprint,source_paths
from scantailor_ocr_unified.native_resume import saved_checkpoint,resume_native_args
from scantailor_ocr_unified.project import file_hash


class WorkingDiskTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.work=self.root/'ssd';self.output=self.root/'usb'
        self.work.mkdir();self.output.mkdir()

    def test_delivery_preserves_originals_and_reuses_identical_copy(self):
        source=self.work/'book.pdf';source.write_bytes(b'verified PDF bytes')
        old=self.output/'book.pdf';old.write_bytes(b'previous edition')
        job={'pdf':str(source),'reading_pdf':str(source),'pdf_hash':file_hash(source)}
        publish_book(job,self.work,self.output)
        self.assertEqual(old.read_bytes(),b'previous edition')
        self.assertEqual(Path(job['pdf']).read_bytes(),source.read_bytes())
        self.assertEqual(job['reading_pdf'],job['pdf'])
        self.assertEqual(file_hash(job['pdf']),job['pdf_hash'])
        before=list(self.output.iterdir());publish_book(job,self.work,self.output)
        self.assertEqual(before,list(self.output.iterdir()))

    def test_cancel_during_copy_never_publishes_partial_pdf(self):
        source=self.work/'book.pdf';source.write_bytes(b'large PDF')
        job={'pdf':str(source)};before=copy.deepcopy(job)
        with self.assertRaises(InterruptedError):
            publish_book(job,self.work,self.output,Mock(side_effect=[None,InterruptedError()]))
        self.assertEqual(job,before);self.assertEqual(list(self.output.iterdir()),[])
        self.assertTrue(source.is_file())

    def test_work_disk_migration_keeps_relative_sources_and_manual_params(self):
        original=self.output/'saved';original.mkdir();(original/'scan.png').write_bytes(b'original')
        project=original/'book.ScanTailor'
        project.write_text('<project outputDirectory="out"><directories><directory id="1" path="."/></directories>'
            '<files><file id="1" dirId="1" name="scan.png"/></files><filters><deskew><page id="7">'
            '<params><rotation mode="manual" angle="2.5"/></params></page></deskew></filters></project>')
        sha=file_hash(project)
        worker=object.__new__(Worker);worker.job={'id':'abcd','title':'Kitap','project':str(project),'preparing':True}
        worker.data={'output_root':str(self.output)}
        worker.request={'work_root':str(self.work),'mode':'prepare','resume_native':{
            'job_id':'abcd','project':str(project),'sha256':sha,'stage':2,'page':1}}
        worker.save=Mock();worker.emit=Mock();worker.native_resilient=Mock()
        folder,new,out=worker.prepare()
        self.assertTrue(new.is_relative_to(self.work));self.assertEqual(file_hash(project),sha)
        self.assertEqual(source_paths(new),[(original/'scan.png').resolve()])
        self.assertIn('mode="manual" angle="2.5"',new.read_text())
        args,page=resume_native_args(['--output-project='+str(new),new,out],worker.request['resume_native'],worker.job,worker.request)
        self.assertEqual((args[0],page),('--start-filter=3',1))

    def test_saved_cursor_is_not_a_processing_setting(self):
        project=self.work/'book.ScanTailor';project.write_text('<project/>')
        expected=fingerprint(project,{})
        project.write_text('<project><batch-checkpoint stage="3" page="7" total="10" token="run"/></project>')
        self.assertEqual(fingerprint(project,{}),expected)
        self.assertEqual(saved_checkpoint(project),{'stage':3,'page':7,'total':10,'token':'run'})
        project.write_text('<project><batch-checkpoint stage="3" page="11" total="10"/></project>')
        with self.assertRaises(ValueError):saved_checkpoint(project)

    def test_split_resume_rechecks_changed_page_sequence(self):
        project=self.work/'book.ScanTailor';project.write_text('<project/>')
        job={'id':'book','project':str(project)}
        resume={'job_id':'book','project':str(project),'sha256':file_hash(project),'stage':1,'page':42}
        _,page=resume_native_args(['--output-project='+str(project),project],resume,job,{'mode':'prepare'})
        self.assertEqual(page,0)


if __name__=='__main__':unittest.main()
