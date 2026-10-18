"""Persistent, local UVDoc curve proposals for the native geometry editor.

Only coordinates are returned: native ScanTailor renders and scores the actual
candidate. Cache hits do not import Torch or load the model. No source is edited.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import sys
import time

VERSION = 'native-uvdoc-curves-1'
ROOT = Path(os.environ.get('STOU_ROOT',Path(__file__).resolve().parents[2]))
sys.path.insert(0,str(ROOT/'src'))
from scantailor_ocr_unified.runtime_layout import resource_path
_models = {}
_model_hash = None


def curves_from_grid(grid, size):
    import numpy as np
    grid = np.asarray(grid, dtype=np.float32)
    if grid.ndim != 3 or grid.shape[2] != 2 or min(grid.shape[:2]) < 2 or not np.isfinite(grid).all():
        raise ValueError('UVDoc sonlu ve geçerli bir koordinat ağı üretmedi.')
    width, height = size
    dx = grid[:-1, 1:] - grid[:-1, :-1]
    dy = grid[1:, :-1] - grid[:-1, :-1]
    jac = dx[..., 0]*dy[..., 1] - dx[..., 1]*dy[..., 0]
    folds = int((jac <= 0).sum())
    ratio = float(np.percentile(jac, 99)/max(float(np.percentile(jac, 1)), 1e-9))
    outside = float((np.abs(grid) > 1.02).any(axis=2).mean())
    # All rows are checked, not merely the visible upper/lower boundaries.
    reasons = []
    if folds: reasons.append('UVDoc ağı katlanıyor.')
    if ratio > 12: reasons.append('UVDoc ağında yerel büyütme farkı aşırı.')
    if outside > .02: reasons.append('UVDoc ağı kaynak alanının dışına taşıyor.')
    curves = [(row+1)*np.array([width-1, height-1])/2 for row in (grid[0], grid[-1])]
    return {'top': curves[0].tolist(), 'bottom': curves[1].tolist(),
            'folds': folds, 'area_ratio': ratio, 'outside': outside, 'reasons': reasons}


def analyze(request):
    global _model_hash
    started = time.perf_counter()
    path = Path(request['image'])
    data = path.read_bytes()
    if len(data) > 25_000_000: raise ValueError('Analiz görüntüsü boyut sınırını aşıyor.')
    device = request.get('device', 'cpu')
    if device not in ('cpu', 'cuda'): raise ValueError('Desteklenmeyen geometri cihazı.')
    weights = resource_path('UVDoc/model/best_model.pkl', ROOT)
    if _model_hash is None: _model_hash = hashlib.sha256(weights.read_bytes()).hexdigest()
    key = hashlib.sha256(data+json.dumps([VERSION, _model_hash, device]).encode()).hexdigest()
    cache = resource_path('cache/native-geometry-v1',ROOT,writable=True)/f'{key}.json'
    if cache.is_file():
        result = json.loads(cache.read_text('utf8'))
        result.update(cached=True, seconds=time.perf_counter()-started)
        return result
    import cv2
    import numpy as np
    cv2.setNumThreads(2)
    image = cv2.imdecode(np.frombuffer(data, np.uint8), cv2.IMREAD_COLOR)
    if image is None or min(image.shape[:2]) < 32 or max(image.shape[:2]) > 1800:
        raise ValueError('Geometri önizlemesinin boyutu geçersiz.')
    import torch
    torch.set_num_threads(2)
    if device == 'cuda':
        if not torch.cuda.is_available(): raise RuntimeError('CUDA kullanılamıyor. CPU seçin.')
        free, total = torch.cuda.mem_get_info()
        if free < 512*1024**2: raise RuntimeError('GPU belleği yetersiz. CPU seçin veya GPU işinin bitmesini bekleyin.')
        torch.backends.cudnn.benchmark = False
    if device not in _models:
        location = weights.parent.parent
        sys.path.insert(0, str(location))
        from model import UVDocnet
        net = UVDocnet(num_filter=32, kernel_size=5)
        checkpoint = torch.load(weights, map_location='cpu', weights_only=True)
        net.load_state_dict(checkpoint['model_state'])
        _models[device] = net.to(device).eval()
    rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    tensor = cv2.resize(rgb.astype('float32')/255., (488, 712)).transpose(2, 0, 1)
    with torch.inference_mode():
        grid, _ = _models[device](torch.from_numpy(tensor).unsqueeze(0).to(device))
        grid = grid[0].cpu().numpy().transpose(1, 2, 0)
    result = curves_from_grid(grid, (image.shape[1], image.shape[0]))
    result.update(ok=True, version=VERSION, device=device, model_sha256=_model_hash,
                  cached=False, seconds=time.perf_counter()-started)
    cache.parent.mkdir(parents=True, exist_ok=True)
    temporary = cache.with_suffix(f'.{os.getpid()}.tmp')
    temporary.write_text(json.dumps(result, ensure_ascii=False, allow_nan=False), 'utf8')
    temporary.replace(cache)
    return result


def main():
    for line in sys.stdin:
        request = {}
        try:
            request = json.loads(line)
            result = analyze(request)
        except Exception as error:
            result = {'ok': False, 'error': str(error)}
        result['id'] = request.get('id')
        print('STOUGEOMETRY '+json.dumps(result, ensure_ascii=False, allow_nan=False), flush=True)


if __name__ == '__main__': main()
