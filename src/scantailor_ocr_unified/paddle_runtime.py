"""Persistent, isolated PaddleOCR worker; stdin paths in, local boxes/text out."""
from __future__ import annotations
import gc,json,os,sys,time,traceback
from pathlib import Path

def emit(value):print('STOUOCR '+json.dumps(value,ensure_ascii=False),flush=True)
def device_name(value):return 'gpu:0' if value.startswith('gpu') else 'cpu'

def detector_profile(device, available):
    # The server detector at 2048 px exceeds 4 GB on the Linux CPU wheel.
    # Limit only the recognition copy; visible PDF pixels are unaffected.
    if not device.startswith('gpu') and available is not None and available < 6*1024**3:
        return 'PP-OCRv5_mobile_det',1536
    return 'PP-OCRv5_server_det',2048

def model_profile(version, language, device, available):
    detector, limit = detector_profile(device, available)
    if version == 'PP-OCRv6':
        return 'PP-OCRv6_small_det', 'PP-OCRv6_small_rec', limit
    if version != 'PP-OCRv5':
        raise ValueError('Unsupported Paddle OCR version')
    recognizer = 'latin_PP-OCRv5_mobile_rec' if language in ('de','tr','fr','it','es','pt','nl','pl','en') else None
    return detector, recognizer, limit
def check(device):
    sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'src'))
    from scantailor_ocr_unified.runtime_layout import prepare_math_libraries
    prepare_math_libraries()
    import paddle
    import paddleocr
    if device.startswith('gpu') and (not paddle.device.is_compiled_with_cuda() or paddle.device.cuda.device_count()<1):
        raise RuntimeError('GPU seçildi, fakat Paddle CUDA cihazını çalıştıramadı. CPU seçebilir veya GPU paketini yeniden kurabilirsiniz.')
    paddle.set_device(device_name(device))
    value=(paddle.ones([16,16])@paddle.ones([16,16])).numpy()
    if float(value[0,0])!=16:raise RuntimeError('Paddle cihaz hesaplama testi başarısız.')
    return {'ok':True,'device':paddle.device.get_device(),'paddle':paddle.__version__,'paddleocr':paddleocr.__version__,'tested':time.time()}
def main():
    # Keep downloaded models under this portable program. No user photos sent.
    root=Path(os.environ.get('STOU_ROOT',Path(__file__).resolve().parents[2]))
    sys.path.insert(0,str(root/'src'))
    from scantailor_ocr_unified.runtime_layout import resource_path, native_model_directory, paddle_model
    os.environ.setdefault('PADDLE_PDX_CACHE_HOME',str(resource_path('paddle-models',root,writable=True)))
    os.environ.setdefault('PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK','True')
    if '--probe' in sys.argv:
        try:emit(check(sys.argv[-1]));return 0
        except Exception as exc:emit({'ok':False,'error':str(exc)});return 1
    engines={}
    for raw in sys.stdin:
        try:
            request=json.loads(raw)
            if request.get('action')=='quit':return 0
            device=request.get('device','cpu');lang=request.get('language','de');version=request.get('version','PP-OCRv5');key=(device,lang,version)
            if key not in engines:
                engines.clear()
                gc.collect()
                from scantailor_ocr_unified.memory_budget import available_bytes
                detector_name,recognizer_name,detector_limit=model_profile(version,lang,device,available_bytes())
                check(device)
                from paddleocr import PaddleOCR
                local={}
                detector=paddle_model(detector_name,root)
                local['text_detection_model_name']=detector_name
                if detector:local['text_detection_model_dir']=native_model_directory(detector)
                if recognizer_name:
                    recognizer=paddle_model(recognizer_name,root)
                    local['text_recognition_model_name']=recognizer_name
                    if recognizer:local['text_recognition_model_dir']=native_model_directory(recognizer)
                engines[key]=PaddleOCR(lang=lang,ocr_version=version,device=device_name(device),
                    use_doc_orientation_classify=False,use_doc_unwarping=False,use_textline_orientation=False,
                    text_det_limit_side_len=detector_limit,text_det_limit_type='max',enable_mkldnn=False,
                    cpu_threads=int(os.environ.get('OMP_NUM_THREADS','4')),**local)
            start=time.monotonic();source=request['image']
            if os.name=='nt' and not source.isascii():
                import numpy as np
                from PIL import Image
                with Image.open(source) as im:source=np.asarray(im.convert('RGB'))[:,:,::-1].copy()
            result=engines[key].predict(source);lines=[]
            for page in result:
                texts=page['rec_texts'];scores=page['rec_scores'];boxes=page.get('rec_boxes');polys=page.get('rec_polys')
                for i,text in enumerate(texts):
                    if not text.strip():continue
                    if boxes is not None:box=[float(v) for v in boxes[i]]
                    else:
                        points=polys[i];box=[float(points[:,0].min()),float(points[:,1].min()),float(points[:,0].max()),float(points[:,1].max())]
                    lines.append({'text':text,'score':float(scores[i]),'box':box})
            emit({'ok':True,'lines':lines,'device':device_name(device),'language':lang,'version':version,'detector':detector_name,'recognizer':recognizer_name or 'library_default','detector_limit':detector_limit,'seconds':round(time.monotonic()-start,3)})
        except Exception as exc:
            traceback.print_exc(file=sys.stderr);emit({'ok':False,'error':str(exc)})
    return 0
if __name__=='__main__':raise SystemExit(main())
