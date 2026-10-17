import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from scantailor_ocr_unified.storage import workspace


class WorkflowTemporaryOwnership(unittest.TestCase):
    def test_temporary_workspace_is_registered_before_work_and_removed_on_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            project = root / 'project'
            project.mkdir()
            owner = project.as_posix()
            (project/'.stou-cache-owner.json').write_text(json.dumps({'project': owner}), encoding='utf8')
            conf = {'cache_owner': owner, 'temp_root': str(root/'transient'), 'reserve_gib': 0}
            with patch.dict(os.environ, {'STOU_DATA': str(root/'app-data')}):
                with self.assertRaisesRegex(RuntimeError, 'interrupted'):
                    with workspace(conf) as temporary:
                        marker = json.loads((temporary/'.stou-cache-owner.json').read_text('utf8'))
                        self.assertEqual(marker['project'], owner)
                        manifest = json.loads((project/'.project-cache.json').read_text('utf8'))
                        self.assertIn({'path': temporary.as_posix(), 'role': 'temporary'}, manifest['roots'])
                        (temporary/'partial.pdf').write_bytes(b'incomplete')
                        raise RuntimeError('interrupted')
                self.assertFalse(temporary.exists())
            self.assertTrue(project.is_dir())

    def test_unregistered_project_cannot_claim_temporary_workspace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root/'unmanaged'
            project.mkdir()
            with patch.dict(os.environ, {'STOU_DATA': str(root/'app-data')}):
                with workspace({'cache_owner': str(project), 'temp_root': str(root/'transient'), 'reserve_gib': 0}) as temporary:
                    self.assertFalse((temporary/'.stou-cache-owner.json').exists())
                    self.assertFalse((project/'.project-cache.json').exists())


if __name__ == '__main__':
    unittest.main()
