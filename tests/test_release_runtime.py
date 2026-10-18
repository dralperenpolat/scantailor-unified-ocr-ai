import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from scantailor_ocr_unified.runtime_layout import initialize, resource_path, paddle_python, python_path, native_model_directory, paddle_model
from scantailor_ocr_unified.native_worker import NativeCrash, Worker


class ReleaseRuntime(unittest.TestCase):
    def test_v6_uses_complete_small_model_pair_and_cache_tracks_weights(self):
        from scantailor_ocr_unified.paddle_runtime import model_profile
        from scantailor_ocr_unified.pdf_export import recognition_signature
        self.assertEqual(model_profile('PP-OCRv6', 'tr', 'cpu', 4*1024**3),
                         ('PP-OCRv6_small_det', 'PP-OCRv6_small_rec', 1536))
        self.assertEqual(model_profile('PP-OCRv5', 'tr', 'cpu', 4*1024**3)[0], 'PP-OCRv5_mobile_det')
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            model=root/'resources/paddle-models/official_models/PP-OCRv6_small_rec'
            model.mkdir(parents=True)
            for name in ('inference.json','inference.pdiparams','inference.yml'):
                (model/name).write_bytes(b'first revision')
            with patch.dict(os.environ, {'STOU_DATA':str(root/'user'),'STOU_ASSET_ROOT':str(root)}):
                conf={'root':str(root),'paddle_version':'PP-OCRv6','paddle_language':'tr'}
                first=recognition_signature(conf,'paddle',root,'tur')
                (model/'inference.pdiparams').write_bytes(b'updated model weights')
                second=recognition_signature(conf,'paddle',root,'tur')
                self.assertNotEqual(first,second)

    def test_interrupted_language_copy_never_publishes_partial_model(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);app=root/'app';data=root/'user'
            source=app/'resources/tessdata_fast/eng.traineddata';source.parent.mkdir(parents=True);source.write_bytes(b'complete model')
            def interrupted(src,dest):
                dest.write(b'partial');raise OSError('simulated interrupted copy')
            with patch.dict(os.environ,{'STOU_DATA':str(data)}):
                with patch('scantailor_ocr_unified.runtime_layout.shutil.copyfileobj',side_effect=interrupted):
                    with self.assertRaises(OSError):initialize(app)
                target=data/'resources/tessdata_fast/eng.traineddata'
                self.assertFalse(target.exists())
                self.assertEqual(list(target.parent.iterdir()),[])
                initialize(app);self.assertEqual(target.read_bytes(),b'complete model')

    def test_partial_user_model_cache_does_not_hide_bundled_or_shared_models(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder).resolve();app=root/'app';data=root/'user';shared=root/'shared'
            suffix=Path('resources/paddle-models/official_models/example')
            (data/suffix).mkdir(parents=True);(app/suffix).mkdir(parents=True);(shared/suffix).mkdir(parents=True)
            for base in (app,shared):
                for name in ('inference.json','inference.pdiparams','inference.yml'):(base/suffix/name).write_bytes(b'model')
            with patch.dict(os.environ,{'STOU_DATA':str(data),'STOU_ASSET_ROOT':str(shared)}):
                self.assertEqual(paddle_model('example',app),app/suffix)
                (app/suffix/'inference.json').unlink()
                self.assertEqual(paddle_model('example',app),shared/suffix)
                for name in ('inference.json','inference.pdiparams','inference.yml'):(data/suffix/name).write_bytes(b'user model')
                self.assertEqual(paddle_model('example',app),data/suffix)
    def test_native_unicode_model_path_is_readable_and_ascii_on_windows(self):
        with tempfile.TemporaryDirectory(prefix='stou Türkçe ') as folder:
            model=Path(folder)/'model';model.mkdir();(model/'inference.json').write_bytes(b'{"model":1}')
            resolved=Path(native_model_directory(model))
            self.assertEqual((resolved/'inference.json').read_bytes(),b'{"model":1}')
            if os.name=='nt':self.assertTrue(str(resolved).isascii())

    def test_relocated_readonly_assets_and_user_language_preserved(self):
        with tempfile.TemporaryDirectory(prefix='stou taşınabilir ') as folder:
            app=Path(folder)/'Kurulum';data=Path(folder)/'Veriler'
            (app/'runtime').mkdir(parents=True);(app/'runtime/python.exe').touch()
            (app/'release.json').write_text('{}');(app/'resources/tessdata_best').mkdir(parents=True)
            (app/'resources/tessdata_best/eng.traineddata').write_bytes(b'bundled')
            with patch.dict(os.environ,{'STOU_ROOT':str(app),'STOU_DATA':str(data)}):
                initialize(app);model=resource_path('tessdata_best/eng.traineddata',app)
                self.assertEqual(model.read_bytes(),b'bundled')
                model.write_bytes(b'user language');initialize(app)
                self.assertEqual(model.read_bytes(),b'user language')
                self.assertEqual((app/'resources/tessdata_best/eng.traineddata').read_bytes(),b'bundled')
                self.assertEqual(paddle_python(app,'cpu'),python_path(app))

    def worker(self, failures):
        worker=object.__new__(Worker);worker.native_output_workers=2;worker.job={};calls=[]
        worker.checkpoint=lambda:None;worker.save=lambda:None;worker.emit=lambda *a,**k:None
        def native(args):
            calls.append(worker.native_output_workers)
            if len(calls)<=failures:raise NativeCrash(3221225477,{'stage':'Output','page':398})
        worker.native=native
        return worker,calls

    def test_parallel_crash_retries_saved_settings_once_serially(self):
        worker,calls=self.worker(1);worker.native_resilient([],Path('unused'))
        self.assertEqual(calls,[2,1]);self.assertEqual(len(worker.job['native_recoveries']),1)

    def test_repeated_crash_is_reported_not_hidden(self):
        worker,calls=self.worker(2)
        with self.assertRaises(NativeCrash):worker.native_resilient([],Path('unused'))
        self.assertEqual(calls,[2,1])

if __name__=='__main__':unittest.main()
