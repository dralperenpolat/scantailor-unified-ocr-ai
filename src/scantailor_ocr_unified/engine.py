"""Shared geometry, guarded UVDoc, native ScanTailor thresholds and book export.

Original photographs are read only. Analysis and display caches are separate.
The guard is deliberately limited: review warnings are not a quality guarantee.
"""
from __future__ import annotations
from pathlib import Path
import hashlib
import json
import math
import os
import sys
import threading
import numpy as np
import cv2
from PIL import Image
from .project import APP_ROOT, read_rgb, file_hash, cache_root
from .mesh_geometry import sample_mesh, remap_mesh

cv2.setNumThreads(2)
_model = None
_device = None
_model_lock = threading.Lock()
_st = None
_dll = None
ENGINE_VERSION = 'desktop-0.3.0-2-align-corners'

class Cancelled(Exception):
    pass

def check_cancel(cancel):
    if cancel is not None and cancel.is_set():
        raise Cancelled('İş durduruldu; kaynak dosyalar korunuyor.')

def resources():
    path = APP_ROOT / 'local-tools.json'
    if not path.exists():
        raise RuntimeError('Yerel motor ayarları bulunamadı: local-tools.json')
    config=json.loads(path.read_text(encoding='utf8'))
    return {key:str((APP_ROOT/value).resolve()) for key,value in config.items()}

def native_threshold(gray, method, adjust=0):
    global _st, _dll
    if _st is None:
        wheel = Path(resources()['stalib'])
        sys.path.insert(0, str(wheel))
        _dll = os.add_dll_directory(str(wheel / 'scantailor_advanced-1.0.1.data' / 'platlib')) if os.name == 'nt' else None
        import stalib_cpp
        _st = stalib_cpp
    params = _st.output.OutputParams()
    params.despeckle = 0
    params.binarization = method
    params.sauvola_k = .18
    params.sauvola_window = 91
    params.wolf_k = .30
    params.wolf_window = 91
    # Otsu adjustment in the inspected binding is a no-op; implement explicitly.
    if adjust and method == 'otsu':
        cut, _ = cv2.threshold(gray, 0, 255, cv2.THRESH_OTSU | cv2.THRESH_BINARY)
        return np.where(gray > np.clip(cut + adjust, 0, 255), 255, 0).astype('uint8')
    if adjust:
        gray = np.clip(gray.astype('int16') - adjust, 0, 255).astype('uint8')
    h, w = gray.shape
    rect = {'x': 0., 'y': 0., 'width': float(w), 'height': float(h)}
    rgb = np.repeat(gray[:, :, None], 3, axis=2)
    return _st.output.generate_output(rgb, rect, rect, params).primary[:, :, 0]

def normalize(gray):
    # Detect the paper field on a bounded analysis image, preserving thin strokes.
    factor = min(1., 1400 / max(gray.shape))
    small = cv2.resize(gray, None, fx=factor, fy=factor, interpolation=cv2.INTER_AREA) if factor < 1 else gray
    bg = cv2.GaussianBlur(cv2.dilate(small, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (41, 41))), (0, 0), 17)
    if bg.shape != gray.shape:
        bg = cv2.resize(bg, (gray.shape[1], gray.shape[0]), interpolation=cv2.INTER_LINEAR)
    return np.clip(gray.astype('float32') * 245 / np.maximum(bg, 1), 0, 255).astype('uint8')


def paper_background(gray):
    factor=min(1.,1400/max(gray.shape))
    small=cv2.resize(gray,None,fx=factor,fy=factor,interpolation=cv2.INTER_AREA) if factor<1 else gray
    bg=cv2.GaussianBlur(cv2.dilate(small,cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(41,41))),(0,0),17)
    return cv2.resize(bg,(gray.shape[1],gray.shape[0]),interpolation=cv2.INTER_LINEAR)


def soft_monochrome(gray):
    """White paper with continuous gray stroke edges; no hard binary cutoff."""
    cut,_=cv2.threshold(gray,0,255,cv2.THRESH_BINARY|cv2.THRESH_OTSU)
    ink=gray[gray<cut]
    if len(ink)<30:return gray.copy()
    black=float(np.percentile(ink,20))
    white=min(245.,float(cut)+(245-float(cut))*.35)
    if white-black<12:return gray.copy()
    return np.clip((gray.astype('float32')-black)*255/(white-black),0,255).astype('uint8')

def text_components(gray):
    g = normalize(gray)
    cut, binary = cv2.threshold(g, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    _, _, stats, centers = cv2.connectedComponentsWithStats(binary, 8)
    h, w = gray.shape
    s = stats[1:]; c = centers[1:]
    if not len(s):
        return s, c
    minimum = max(2, int(h * .002))
    keep = ((s[:, 3] >= minimum) & (s[:, 3] < h * .05) & (s[:, 2] >= 1)
            & (s[:, 2] < w * .14) & (s[:, 4] >= 3)
            & (s[:, 0] > 1) & (s[:, 1] > 1)
            & (s[:, 0] + s[:, 2] < w - 1) & (s[:, 1] + s[:, 3] < h - 1))
    return s[keep], c[keep]

def content_and_body(gray):
    h, w = gray.shape
    stats, centers = text_components(gray)
    warnings = []
    if len(stats) < 30:
        return [.025, .025, .975, .975], max(6., h / 100), ['Metin az; içerik ve yazı ölçeğini kontrol et.']
    heights = stats[:, 3]
    hist = np.bincount(heights, minlength=max(heights) + 1).astype('float64')
    # Small dots must not become the representative body type size.
    hist[:max(4, int(h * .004))] = 0
    smooth = np.convolve(hist, [1, 2, 3, 2, 1], mode='same')
    mode = int(np.argmax(smooth))
    near = heights[(heights >= mode - 1) & (heights <= mode + 1)]
    body = float(np.median(near)) if len(near) else float(mode)
    # Keep smaller footnote letters and marginal numbers in the content union.
    use = stats[(heights >= max(2, body * .40)) & (heights < body * 4.0)]
    if len(use) < 20: use = stats
    # Isolated edge scratches must not extend the content rectangle. Retain
    # marginal numbers whenever they share a row with actual body text.
    yc=use[:,1]+use[:,3]/2
    row_support=np.array([np.sum(np.abs(yc-y)<body*1.3) for y in yc])
    supported=use[row_support>=4]
    if len(supported)>20:use=supported
    pad = max(4, round(body * .65))
    x0 = max(0, int(np.min(use[:, 0])) - pad)
    y0 = max(0, int(np.min(use[:, 1])) - pad)
    x1 = min(w, int(np.max(use[:, 0] + use[:, 2])) + pad)
    y1 = min(h, int(np.max(use[:, 1] + use[:, 3])) + pad)
    if min(x0 / w, y0 / h, 1 - x1 / w, 1 - y1 / h) < .01:
        warnings.append('Yazı sayfa kenarına yakın; kırpma sınırını kontrol et.')
    if body < 6:
        warnings.append('Kaynak yazı çok küçük; büyütme yeni ayrıntı üretmez.')
    return [x0 / w, y0 / h, x1 / w, y1 / h], max(body, 3.), warnings

def dominant_vertical(gray):
    h,w=gray.shape
    center=normalize(gray)[int(h*.15):int(h*.85),int(w*.15):int(w*.85)]
    _,bw=cv2.threshold(center,0,1,cv2.THRESH_BINARY_INV|cv2.THRESH_OTSU)
    def energy(values):
        smooth=cv2.GaussianBlur(values.astype('float32').reshape(-1,1),(1,0),7).ravel()
        return float(np.std(values-smooth))
    row,col=energy(bw.mean(1)),energy(bw.mean(0))
    return col>max(.04,row*2.5)

def edge_artifacts(gray,content):
    h,w=gray.shape
    x0,y0,x1,y1=[int(v*n) for v,n in zip(content,[w,h,w,h])]
    g=normalize(gray)
    _,mask=cv2.threshold(g,0,255,cv2.THRESH_BINARY_INV|cv2.THRESH_OTSU)
    _,_,stats,_=cv2.connectedComponentsWithStats(mask,8)
    for x,y,cw,ch,area in stats[1:]:
        near=(x<w*.2 or y<h*.15 or x+cw>w*.9 or y+ch>h*.9)
        inside=(x+cw>x0 and x<x1 and y+ch>y0 and y<y1)
        if near and inside and area>w*h*.0015 and (ch>h*.04 or cw>w*.1):
            return True
    return False

def auto_quad(image, half='whole'):
    """Conservative paper guess. Corners remain editable and are labelled a guess."""
    a = np.asarray(image)
    h, w = a.shape[:2]
    xa, xb = (0, w) if half == 'whole' else ((0, w // 2) if half == 'left' else (w // 2, w))
    roi = a[:, xa:xb]
    scale = min(1., 900 / max(roi.shape[:2]))
    g = cv2.cvtColor(cv2.resize(roi, None, fx=scale, fy=scale), cv2.COLOR_RGB2GRAY)
    g = cv2.GaussianBlur(g, (9, 9), 0)
    _, mask = cv2.threshold(g, 0, 255, cv2.THRESH_BINARY | cv2.THRESH_OTSU)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((17, 17), np.uint8))
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    candidates = []
    for c in contours:
        x, y, cw, ch = cv2.boundingRect(c)
        area = cv2.contourArea(c) / g.size
        if .16 < area < .95 and ch > g.shape[0] * .35:
            candidates.append((area, x, y, cw, ch))
    if candidates:
        _, x, y, cw, ch = max(candidates)
        x0, y0, x1, y1 = x / scale + xa, y / scale, (x + cw) / scale + xa, (y + ch) / scale
    else:
        x0, y0, x1, y1 = xa + .03 * (xb - xa), .04 * h, xb - .03 * (xb - xa), .94 * h
    return [[x0 / w, y0 / h], [x1 / w, y0 / h], [x1 / w, y1 / h], [x0 / w, y1 / h]]

def validate_quad(quad):
    q = np.asarray(quad, dtype='float32')
    if q.shape != (4, 2) or not np.isfinite(q).all() or (q < 0).any() or (q > 1).any():
        raise ValueError('Sayfa köşeleri görüntünün içinde olmalı.')
    if not cv2.isContourConvex(q) or abs(cv2.contourArea(q)) < .003:
        raise ValueError('Sayfa köşeleri kesişiyor veya alan çok küçük.')
    return q

def crop_and_perspective(rgb, quad):
    h, w = rgb.shape[:2]
    q = validate_quad(quad) * [w - 1, h - 1]
    x0, y0 = np.floor(q.min(axis=0)).astype(int)
    x1, y1 = np.ceil(q.max(axis=0)).astype(int) + 1
    crop = rgb[y0:y1, x0:x1].copy()
    tw = max(32, round((np.linalg.norm(q[1] - q[0]) + np.linalg.norm(q[2] - q[3])) / 2))
    th = max(32, round((np.linalg.norm(q[3] - q[0]) + np.linalg.norm(q[2] - q[1])) / 2))
    matrix = cv2.getPerspectiveTransform(q.astype('float32'), np.array([[0, 0], [tw - 1, 0], [tw - 1, th - 1], [0, th - 1]], 'float32'))
    rectified = cv2.warpPerspective(rgb, matrix, (tw, th), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
    return crop, rectified

def grid_quality(grid, gray):
    dx = grid[:-1, 1:] - grid[:-1, :-1]
    dy = grid[1:, :-1] - grid[:-1, :-1]
    jac = dx[:, :, 0] * dy[:, :, 1] - dx[:, :, 1] * dy[:, :, 0]
    folded = int(np.sum(jac <= 0))
    ratio = float(np.percentile(jac, 99) / max(np.percentile(jac, 1), 1e-9))
    outside = float((np.abs(grid) > 1.005).any(axis=2).mean())
    stats, centers = text_components(gray)
    hull = cv2.convexHull(grid.reshape(-1, 2).astype('float32'))
    h, w = gray.shape
    samples = centers / [max(w - 1, 1), max(h - 1, 1)] * 2 - 1
    coverage = float(np.mean([cv2.pointPolygonTest(hull, (float(x), float(y)), True) >= -.025 for x, y in samples])) if len(samples) else 1.
    reasons = []
    if folded: reasons.append('Geometri ağı katlanıyor.')
    if ratio > 12: reasons.append('Yerel büyütme farkı aşırı.')
    if outside > .02: reasons.append('Dönüşüm görüntü dışına taşıyor.')
    if coverage < .985: reasons.append('Dönüşüm kenardaki metni dışarıda bırakabilir.')
    if len(stats) < 30: reasons.append('Geometriyi doğrulayacak kadar metin yok.')
    return {'folds': folded, 'stretch_ratio': round(ratio, 3), 'outside': outside,
            'text_coverage': coverage, 'accepted': not reasons, 'reasons': reasons}

def uvdoc(crop):
    global _model, _device
    import torch
    with _model_lock:
        if _model is None:
            torch.set_num_threads(2)
            location = Path(resources()['uvdoc'])
            sys.path.insert(0, str(location))
            from model import UVDocnet
            net = UVDocnet(num_filter=32, kernel_size=5)
            checkpoint = torch.load(location / 'model' / 'best_model.pkl', map_location='cpu', weights_only=True)
            net.load_state_dict(checkpoint['model_state'])
            _device = 'cuda' if torch.cuda.is_available() else 'cpu'
            _model = net.to(_device).eval()
        inp = cv2.resize(crop.astype('float32') / 255., (488, 712)).transpose(2, 0, 1)
        with torch.inference_mode():
            grid, _ = _model(torch.from_numpy(inp).unsqueeze(0).to(_device))
            grid = grid[0].cpu().numpy().transpose(1, 2, 0)
    mapped = remap_mesh(crop, grid)
    return mapped, grid

def deskew(rgb):
    gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
    g = normalize(gray)
    _, bw = cv2.threshold(g, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    h, w = g.shape
    lines = cv2.HoughLinesP(bw, 1, np.pi / 1800, threshold=max(25, w // 12),
                            minLineLength=max(30, w // 7), maxLineGap=12)
    angles = []
    if lines is not None:
        for x0, y0, x1, y1 in np.asarray(lines).reshape(-1,4):
            angle = math.degrees(math.atan2(y1 - y0, x1 - x0))
            if abs(angle) < 8: angles.append(angle)
    angle = float(np.median(angles)) if len(angles) >= 4 else 0.
    if abs(angle) < .12: return rgb, angle
    m = cv2.getRotationMatrix2D((w / 2, h / 2), angle, 1.)
    return cv2.warpAffine(rgb, m, (w, h), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_CONSTANT, borderValue=(255, 255, 255)), angle

def geometry(project, page, cancel=None):
    check_cancel(cancel)
    source = Path(page['source'])
    sha = file_hash(source)
    params = {k: page.get(k) for k in ['quad', 'method', 'rotation']}
    key = hashlib.sha256(json.dumps([ENGINE_VERSION, sha, params], sort_keys=True).encode()).hexdigest()
    directory = cache_root(project) / 'geometry' / key
    pic, record = directory / 'page.png', directory / 'result.json'
    if record.exists() and pic.exists():
        result = json.loads(record.read_text(encoding='utf8'))
        gridfile=result.get('sampling',{}).get('grid')
        if file_hash(pic) == result['image_sha256'] and (not gridfile or Path(gridfile).exists()):
            return result
    rgb = np.asarray(read_rgb(source))
    crop, perspective = crop_and_perspective(rgb, page['quad'])
    method = page.get('method', 'auto'); warnings = []
    quality = None; chosen = method; angle=0.;grid=None
    if method in ['auto', 'dewarp']:
        try:
            corrected, grid = uvdoc(crop)
            quality = grid_quality(grid, cv2.cvtColor(crop, cv2.COLOR_RGB2GRAY))
            if not quality['accepted']:
                if method == 'auto':
                    corrected, angle = deskew(perspective); chosen = 'perspective'
                    warnings.append('Eğrilik adayı elendi; perspektif düzeltmeye dönüldü.')
                else:
                    warnings.append('Elle seçilen eğrilik adayında kontrol uyarısı var.')
                warnings.extend(quality['reasons'])
            else: chosen = 'dewarp'
        except (ImportError, RuntimeError, FileNotFoundError, ValueError) as exc:
            if method == 'dewarp': raise
            corrected, angle = deskew(perspective); chosen = 'perspective'
            warnings.append('Eğrilik motoru çalışmadı; perspektif kullanıldı: ' + str(exc)[:140])
    elif method == 'perspective': corrected, angle = deskew(perspective)
    elif method == 'deskew': corrected, angle = deskew(crop)
    else: corrected = crop
    base_size=[corrected.shape[1],corrected.shape[0]]
    rotation = int(page.get('rotation', 0)) % 360
    if rotation: corrected = np.rot90(corrected, k=-(rotation // 90)).copy()
    gray = cv2.cvtColor(corrected, cv2.COLOR_RGB2GRAY)
    content, body, detected_warnings = content_and_body(gray)
    warnings.extend(detected_warnings)
    vertical=dominant_vertical(gray)
    if vertical:
        _,body,_=content_and_body(np.rot90(gray).copy())
        warnings.append('Yan basılmış metin: OCR yönü ayrıca denenecek; özel sayfa ölçeğini kontrol et.')
    if edge_artifacts(gray,content):
        warnings.append('Kenarda büyük siyah şekil var; parmak / kenar artığı için temizleme alanı kullan.')
    if sha != page.get('source_hash'):
        warnings.append('Kaynak dosya projeye eklendiğinden beri değişmiş.')
    directory.mkdir(parents=True, exist_ok=True)
    q=validate_quad(page['quad'])*[rgb.shape[1]-1,rgb.shape[0]-1]
    lo=np.floor(q.min(0)).astype(int);hi=np.ceil(q.max(0)).astype(int)+1
    sampling={'kind':chosen,'angle':angle,'rotation':rotation,'base_size':base_size,
              'crop_box':[int(lo[0]),int(lo[1]),int(hi[0]),int(hi[1])]}
    if chosen=='dewarp' and grid is not None:
        gridfile=directory/'grid.npy';np.save(gridfile,grid);sampling['grid']=str(gridfile)
    Image.fromarray(corrected).save(pic)
    result = {'key': key, 'image': str(pic), 'image_sha256': file_hash(pic),
              'size': [corrected.shape[1], corrected.shape[0]], 'content': content,
              'body_px': body, 'vertical_text':vertical, 'warnings': warnings, 'selected_method': chosen, 'quality': quality,'sampling':sampling}
    record.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    check_cancel(cancel)
    return result

def content_pixels(page, result):
    w, h = result['size']; rect = page.get('content') or result['content']
    x0, y0, x1, y1 = [float(v) for v in rect]
    if not (0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1):
        raise ValueError('İçerik kutusu geçersiz.')
    return (int(x0 * w), int(y0 * h), max(int(x0 * w) + 1, int(x1 * w)), max(int(y0 * h) + 1, int(y1 * h)))

def body_size(page, result):
    return max(3., float(page.get('body_override') or result.get('layout_body_px') or result['body_px']))


def sample_original(source_gray,page,result,box,size):
    """Compose geometry and final scale; interpolate photograph only once.

    Process strips to keep 600-DPI maps out of the working set.
    """
    sampling=result['sampling'];basew,baseh=sampling['base_size']
    xa,ya,xb,yb=sampling['crop_box'];kind=sampling['kind']
    if kind=='dewarp':grid=np.load(sampling['grid'],allow_pickle=False)
    else:
        matrix=np.array([[1.,0.,-xa],[0.,1.,-ya],[0.,0.,1.]])
        if kind=='perspective':
            h,w=source_gray.shape;q=validate_quad(page['quad'])*[w-1,h-1]
            matrix=cv2.getPerspectiveTransform(q.astype('float32'),np.array([[0,0],[basew-1,0],[basew-1,baseh-1],[0,baseh-1]],'float32'))
        if sampling['angle'] and kind in ('perspective','deskew'):
            rotation=np.vstack([cv2.getRotationMatrix2D((basew/2,baseh/2),sampling['angle'],1.),[0,0,1]])
            matrix=rotation@matrix
        inverse=np.linalg.inv(matrix)
    x0,y0,x1,y1=box;tw,th=size
    result_image=np.empty((th,tw),np.uint8)
    xx=((np.arange(tw,dtype='float32')+.5)*(x1-x0)/tw+x0-.5)[None,:]
    for row in range(0,th,256):
        yy=((np.arange(row,min(row+256,th),dtype='float32')+.5)*(y1-y0)/th+y0-.5)[:,None]
        x,y=np.broadcast_arrays(xx,yy);angle=sampling['rotation']
        if angle==90:x,y=y,baseh-1-x
        elif angle==180:x,y=basew-1-x,baseh-1-y
        elif angle==270:x,y=basew-1-y,x
        if kind=='dewarp':
            uv=sample_mesh(grid,x,y,(basew,baseh))
            mx=(uv[:,:,0]+1)*(xb-xa-1)/2+xa;my=(uv[:,:,1]+1)*(yb-ya-1)/2+ya
        else:
            denom=inverse[2,0]*x+inverse[2,1]*y+inverse[2,2]
            mx=(inverse[0,0]*x+inverse[0,1]*y+inverse[0,2])/denom
            my=(inverse[1,0]*x+inverse[1,1]*y+inverse[1,2])/denom
        result_image[row:row+len(yy)]=cv2.remap(source_gray,mx.astype('float32'),my.astype('float32'),cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT,borderValue=255)
    return result_image

def fit_target(project, pages_and_results):
    target = float(project.get('target_body_px', 30))
    if not 10 <= target <= 70: raise ValueError('Yazı hedefi 10–70 aralığında olmalı.')
    normal=[body_size(p,r)/r['size'][0] for p,r in pages_and_results if not r.get('vertical_text')]
    for page,result in pages_and_results:
        if result.get('vertical_text') and normal and not page.get('body_override'):
            # Annex print can intentionally be smaller than body text. Match the
            # photographed paper scale instead of inflating annex glyphs.
            result['layout_body_px']=float(np.median(normal))*result['size'][0]
    if project.get('normalize_text', True):
        for page, result in pages_and_results:
            x0, y0, x1, y1 = content_pixels(page, result)
            target = min(target, body_size(page, result) * min(2240 / (x1 - x0), 3268 / (y1 - y0)))
    return target

def render_page(project, page, result, target=None, cancel=None):
    check_cancel(cancel)
    target = target or fit_target(project, [(page, result)])
    settings = {k: page.get(k) for k in ['content', 'white_zones', 'threshold', 'threshold_adjust', 'normalize_light', 'body_override', 'allow_empty_ocr']}
    dpi = int(project.get('output_dpi', 600))
    if dpi not in (300, 600): raise ValueError('Çıktı çözünürlüğü 300 veya 600 olmalı.')
    output_mode=project.get('output_mode','binary')
    if output_mode not in ('binary','soft','gray'):raise ValueError('Geçersiz sayfa görünümü.')
    key = hashlib.sha256(json.dumps([ENGINE_VERSION, result['key'], settings, target, body_size(page,result), dpi, project.get('normalize_text', True),output_mode], sort_keys=True).encode()).hexdigest()
    directory = cache_root(project) / 'display' / key
    dp, op, meta = directory / 'display.tif', directory / 'ocr.png', directory / 'result.json'
    if meta.exists() and dp.exists() and op.exists():
        return json.loads(meta.read_text(encoding='utf8'))
    gray = np.asarray(Image.open(result['image']).convert('L')).copy()
    h, w = gray.shape
    x0, y0, x1, y1 = content_pixels(page, result)
    block = gray[y0:y1, x0:x1]
    scale = target / body_size(page, result) if project.get('normalize_text', True) else min(2240 / block.shape[1], 3268 / block.shape[0])
    size = (round(block.shape[1]*scale), round(block.shape[0]*scale))
    if size[0] > 2242 or size[1] > 3270: raise ValueError('İçerik ortak sayfa ölçeğine sığmıyor.')
    offset = ((2480-size[0])//2, (3508-size[1])//2)
    source_gray=cv2.cvtColor(np.asarray(read_rgb(page['source'])),cv2.COLOR_RGB2GRAY)
    background=paper_background(block) if page.get('normalize_light',True) else None
    def produce(target_size):
        sampled=sample_original(source_gray,page,result,(x0,y0,x1,y1),target_size)
        if background is not None:
            bg=cv2.resize(background,target_size,interpolation=cv2.INTER_LINEAR)
            sampled=np.clip(sampled.astype('float32')*245/np.maximum(bg,1),0,255).astype('uint8')
        for zx0,zy0,zx1,zy1 in page.get('white_zones',[]):
            xa=max(0,int((zx0*w-x0)*target_size[0]/block.shape[1]));ya=max(0,int((zy0*h-y0)*target_size[1]/block.shape[0]))
            xb=min(target_size[0],int((zx1*w-x0)*target_size[0]/block.shape[1]));yb=min(target_size[1],int((zy1*h-y0)*target_size[1]/block.shape[0]))
            if xb>xa and yb>ya:sampled[ya:yb,xa:xb]=255
        return sampled
    ocr_block=produce(size)
    recognition = np.full((3508,2480),255,np.uint8)
    recognition[offset[1]:offset[1]+size[1],offset[0]:offset[0]+size[0]] = ocr_block
    multiplier = dpi // 300
    bw_source = ocr_block if multiplier==1 else produce((size[0]*multiplier,size[1]*multiplier))
    if output_mode=='binary':bw = native_threshold(bw_source,page.get('threshold','otsu'),int(page.get('threshold_adjust',0)))
    elif output_mode=='soft':bw=soft_monochrome(bw_source)
    else:bw=bw_source
    display = np.full((3508*multiplier,2480*multiplier),255,np.uint8)
    x,y=offset[0]*multiplier,offset[1]*multiplier
    display[y:y+bw.shape[0],x:x+bw.shape[1]] = bw
    directory.mkdir(parents=True,exist_ok=True)
    Image.fromarray(recognition).save(op,dpi=(300,300))
    if output_mode=='binary':Image.fromarray(display).convert('1',dither=Image.Dither.NONE).save(dp,compression='group4',dpi=(dpi,dpi))
    else:Image.fromarray(display).save(dp,compression='tiff_deflate',dpi=(dpi,dpi))
    output={'id':page['label'],'display_image':str(dp),'ocr_image':str(op),'geometry_id':result['key'],
            'target_body_px':target,'scale':scale,'body_px':body_size(page,result),'content_pixels':[x0,y0,x1,y1],
            'selected_method':result['selected_method'],'warnings':result['warnings'],'allow_empty_ocr':bool(page.get('allow_empty_ocr',False)),
            'ocr_rotations':[90,270] if result.get('vertical_text') else [0],'output_mode':output_mode}
    meta.write_text(json.dumps(output,ensure_ascii=False,indent=2),encoding='utf8')
    check_cancel(cancel)
    return output

def export_book(project, output, cancel=None, notify=lambda *args:None):
    from .cache import assemble
    import time
    started=time.monotonic();pairs=[]
    if not project['pages']: raise ValueError('Projede sayfa yok.')
    for i,page in enumerate(project['pages']):
        check_cancel(cancel);notify('Geometri',i,len(project['pages']),page['label'])
        pairs.append((page,geometry(project,page,cancel)))
    target=fit_target(project,pairs);entries=[]
    for i,(page,result) in enumerate(pairs):
        check_cancel(cancel);notify('Görüntü',i,len(pairs),page['label'])
        entries.append(render_page(project,page,result,target,cancel))
    cfg=resources()
    conf={'schema':1,'language':project.get('language','deu'),'page_mm':[210,297],'margin_mm':0,'ocr_max_edge':4200,
          'tessdata':cfg['tessdata'],'jbig2':cfg['jbig2'],'pages':entries}
    work=cache_root(project)/'exports';work.mkdir(parents=True,exist_ok=True)
    manifest=work/(Path(output).stem+'_'+str(time.time_ns())+'.json')
    manifest.write_text(json.dumps(conf,ensure_ascii=False,indent=2),encoding='utf8')
    def on_page(message):
        check_cancel(cancel);notify('OCR / PDF',0,len(entries),message)
    result=assemble(manifest,output,cache_root(project)/'pdf',notify=on_page,cancel=cancel)
    result.update(target_body_px=target,total_seconds=round(time.monotonic()-started,2),pipeline_pages=entries)
    (work/(Path(output).stem+'_verification.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    return result
