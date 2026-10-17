import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scantailor_ocr_unified import native_launcher as launcher


class NativeLauncher(unittest.TestCase):
    def test_development_build_and_packaged_build_selection(self):
        suffix='.exe' if os.name=='nt' else ''
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            dev=root/('native/release051/scantailor-ocr-unified'+suffix)
            dev.parent.mkdir(parents=True);dev.touch()
            self.assertEqual(launcher.native_executable(root),dev)
            packaged=root/('native/bin/scantailor-ocr-unified'+suffix)
            packaged.parent.mkdir(parents=True);packaged.touch()
            self.assertEqual(launcher.native_executable(root),packaged)

    def test_damaged_optional_recovery_pointer_does_not_block_launch(self):
        suffix='.exe' if os.name=='nt' else ''
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);native=root/'native/bin';native.mkdir(parents=True)
            for name in ('scantailor-ocr-unified','scantailor-experimental-cli'):
                (native/(name+suffix)).touch()
            python=root/'python';python.touch()
            active=root/'projects/active-batch.json';active.parent.mkdir()
            for broken in ('{"queue":','[]','null','{"queue": null}','{"queue": []}','{"queue": 7}'):
                active.write_text(broken,encoding='utf8')
                with patch.object(launcher,'ROOT',root),patch.object(launcher,'data_root',return_value=root),patch.object(launcher,'python_path',return_value=python),patch.object(launcher,'initialize'),patch.object(launcher.subprocess,'Popen') as launch,patch('sys.argv',['launch']):
                    self.assertEqual(launcher.main(),0)
                    launch.assert_called_once()
                self.assertEqual(active.read_text('utf8'),broken)

    def test_valid_recovery_pointer_still_reconnects_queue(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'active.json';value={'queue':'saved.json','request':'run.json'}
            path.write_text(json.dumps(value),encoding='utf8')
            self.assertEqual(launcher.read_pointer(path),value)


if __name__=='__main__':unittest.main()
