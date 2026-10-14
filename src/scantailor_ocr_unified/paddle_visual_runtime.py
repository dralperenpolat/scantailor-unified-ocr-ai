"""Isolated persistent Paddle visual-analysis worker for the native editor."""
from __future__ import annotations

import hashlib
import json
import os
import sys
import time
import traceback
from pathlib import Path

ROOT = Path(os.environ.get('STOU_ROOT',Path(__file__).resolve().parents[2]))
sys.path.insert(0,str(ROOT/'src'))
from scantailor_ocr_unified.runtime_layout import resource_path, prepare_math_libraries, native_model_directory
prepare_math_libraries()
sys.path.insert(0, str(ROOT/'src'))
from scantailor_ocr_unified.visual_content import VERSION, propose

os.environ.setdefault('PADDLE_PDX_CACHE_HOME', str(resource_path('paddle-models',ROOT,writable=True)))
os.environ.setdefault('PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK', 'True')
os.environ.setdefault('PADDLE_PDX_MODEL_SOURCE', 'BOS')
os.environ.setdefault('FLAGS_allocator_strategy', 'auto_growth')
os.environ.setdefault('FLAGS_initial_gpu_memory_in_mb', '128')
os.environ.setdefault('OMP_NUM_THREADS', '2')
os.environ.setdefault('MKL_NUM_THREADS', '2')
DETECTOR = 'PP-OCRv6_small_det'
LAYOUT = 'PP-DocLayout-S'


def emit(value):
    print('STOUAI '+json.dumps(value, ensure_ascii=False, allow_nan=False), flush=True)


class VisualEngine:
    def __init__(self):
        self.detector = self.layout = None
        self.device = None
        self.cache = resource_path('cache/visual-ai-v1',ROOT,writable=True)
        self.cache.mkdir(parents=True, exist_ok=True)

    def load(self, device):
        if self.device is not None and self.device != device:
            raise ValueError('Cihaz değişimi için analizi yeniden başlatın.')
        if self.detector is not None:
            return
        from paddleocr import TextDetection, LayoutDetection
        # Require complete local models. A GUI analysis must not silently spend
        # minutes downloading or switch devices after a CUDA error.
        models = ROOT/'resources/paddle-models/official_models'
        for name in (DETECTOR, LAYOUT):
            if not all((models/name/f).is_file() for f in ('inference.json', 'inference.pdiparams', 'inference.yml')):
                raise RuntimeError('Eksik görsel analiz modeli: '+name)
        self.detector = TextDetection(model_name=DETECTOR, model_dir=native_model_directory(models/DETECTOR), device=device,
                                      enable_mkldnn=False, cpu_threads=2, limit_side_len=1600,
                                      limit_type='max', box_thresh=.5, unclip_ratio=1.5)
        self.layout = LayoutDetection(model_name=LAYOUT, model_dir=native_model_directory(models/LAYOUT), device=device,
                                      enable_mkldnn=False, cpu_threads=2, threshold=.3)
        self.device = device

    def analyze(self, request):
        import numpy as np
        from PIL import Image
        started = time.monotonic()
        source = Path(request['image'])
        raw = source.read_bytes()
        with Image.open(source) as image:
            if max(image.size) > 2000 or min(image.size) < 8:
                raise ValueError('Görsel analiz yalnız sınırlı boyutlu önizlemeleri kabul eder.')
            rgb = np.asarray(image.convert('RGB'))
        device = request.get('device', 'cpu')
        if device == 'gpu': device = 'gpu:0'
        if device not in ('cpu', 'gpu:0'): raise ValueError('Geçersiz analiz cihazı.')
        key = hashlib.sha256(raw+f'{DETECTOR}:1600:.5:1.5:{device}'.encode()).hexdigest()
        cache = self.cache/(key+'.json')
        boxes = None
        if cache.is_file():
            try: boxes = json.loads(cache.read_text('utf8'))['boxes']
            except (ValueError, KeyError, OSError): pass
        cached = boxes is not None
        if boxes is None:
            self.load(device)
            result = list(self.detector.predict(rgb[:, :, ::-1].copy(), batch_size=1))[0]
            boxes = []
            for polygon, score in zip(result['dt_polys'], result['dt_scores']):
                p = np.asarray(polygon)
                boxes.append(dict(box=[float(p[:, 0].min()), float(p[:, 1].min()), float(p[:, 0].max()), float(p[:, 1].max())], score=float(score)))
            temporary = cache.with_suffix('.'+str(os.getpid())+'.tmp')
            temporary.write_text(json.dumps({'boxes': boxes}, allow_nan=False), 'utf8'); os.replace(temporary, cache)
        result = propose(boxes, (rgb.shape[1], rgb.shape[0]), request.get('reference'))
        regions = []
        if request.get('layout', True) and result['candidate']:
            x0, y0, x1, y1 = result['candidate']
            x0, y0 = int(x0), int(y0); x1, y1 = min(rgb.shape[1], int(x1)+1), min(rgb.shape[0], int(y1)+1)
            layout_key = hashlib.sha256((key+f'{LAYOUT}:.3:{x0}:{y0}:{x1}:{y1}').encode()).hexdigest()
            layout_cache = self.cache/(layout_key+'.layout.json')
            cached_regions = None
            if layout_cache.is_file():
                try: cached_regions = json.loads(layout_cache.read_text('utf8'))['regions']
                except (ValueError, KeyError, OSError): pass
            if isinstance(cached_regions, list):
                regions = cached_regions
            else:
                self.load(device)
                layout = list(self.layout.predict(rgb[y0:y1, x0:x1, ::-1].copy(), batch_size=1))[0]
                for r in layout['boxes']:
                    b = r['coordinate']
                    regions.append(dict(box=[float(b[0])+x0, float(b[1])+y0, float(b[2])+x0, float(b[3])+y0], label=r['label'], score=float(r['score'])))
                temporary = layout_cache.with_suffix('.'+str(os.getpid())+'.tmp')
                temporary.write_text(json.dumps({'regions': regions}, allow_nan=False), 'utf8'); os.replace(temporary, layout_cache)
        result.update(ok=True, regions=regions, cached_detection=cached, seconds=time.monotonic()-started,
                      model=DETECTOR, layout_model=LAYOUT, algorithm=VERSION, device=device)
        return result


def main():
    engine = VisualEngine()
    for line in sys.stdin:
        request = {}
        try:
            request = json.loads(line)
            if request.get('action') == 'quit': return
            emit(dict(engine.analyze(request), id=request.get('id')))
        except Exception as error:
            traceback.print_exc(file=sys.stderr)
            emit(dict(ok=False, id=request.get('id'), error=str(error)))


if __name__ == '__main__': main()
