import json,tempfile,unittest
from pathlib import Path
from scantailor_ocr_unified.native_worker import native_execution
from scantailor_ocr_unified.project import file_hash

class VerifiedNativeExecution(unittest.TestCase):
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
