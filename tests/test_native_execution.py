import json,tempfile,unittest
from pathlib import Path
from scantailor_ocr_unified.native_worker import native_execution
from scantailor_ocr_unified.project import file_hash
from scantailor_ocr_unified.native_resume import resume_native_args

class VerifiedNativeExecution(unittest.TestCase):
    def test_preparation_resume_requires_matching_project_and_durable_hash(self):
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)/'book.ScanTailor';project.write_text('<project/>')
            job={'id':'book','project':str(project)}
            resume={'job_id':'book','project':str(project),'sha256':file_hash(project),'stage':3,'page':42}
            args=['--end-filter=5','--output-project='+str(project),project,Path(folder)/'out']
            result,page=resume_native_args(args,resume,job,{'mode':'prepare'})
            self.assertEqual(result[0],'--start-filter=4');self.assertEqual(page,42)
            for bad in ({'sha256':'changed'},{'page':-1},{'page':True},{'stage':5},{'job_id':'other'}):
                with self.subTest(bad=bad),self.assertRaises(ValueError):
                    resume_native_args(args,{**resume,**bad},job,{'mode':'prepare'})
            with self.assertRaises(ValueError):resume_native_args(args,resume,job,{'mode':'export'})
            with self.assertRaises(ValueError):resume_native_args(args[:-2],resume,job,{'mode':'prepare'})
            project.write_text('<edited/>')
            with self.assertRaises(ValueError):resume_native_args(args,resume,job,{'mode':'prepare'})
    def test_only_matching_binaries_can_share_verified_output_caches(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);base=root/'base.exe';fast=root/'fast.exe'
            base.write_bytes(b'base');fast.write_bytes(b'parallel')
            record={'path':'fast.exe','base_sha256':file_hash(base),'sha256':file_hash(fast),'workers':2}
            config=root/'local-native-performance.json';config.write_text(json.dumps(record))
            self.assertEqual(native_execution(root,base,{'threads':8}),(fast,2))
            self.assertEqual(native_execution(root,base,{'threads':1}),(fast,1))
            fast.write_bytes(b'unverified build')
            self.assertEqual(native_execution(root,base,{}),(base,1))
            with self.assertRaises(ValueError):native_execution(root,base,{'native_variant':record})

if __name__=='__main__':unittest.main()
