"""Conservative, inspectable content proposals from local text detections.

Coordinates always refer to the already transformed native page. This module
never changes pixels, recognizes text, or treats missing detections as blankness.
"""
from __future__ import annotations

import math
import statistics

VERSION = 'visual-content-1'


def valid_box(box):
    return (isinstance(box, (list, tuple)) and len(box) == 4
            and all(isinstance(x, (float, int)) and math.isfinite(x) for x in box)
            and box[2] > box[0] and box[3] > box[1])


def intersection(a, b):
    return max(0., min(a[2], b[2])-max(a[0], b[0])) * max(0., min(a[3], b[3])-max(a[1], b[1]))


def area(b):
    return max(0., b[2]-b[0])*max(0., b[3]-b[1])


def propose(boxes, size, reference=None):
    width, height = size
    if not (0 < width <= 2400 and 0 < height <= 2400):
        raise ValueError('AI önizleme boyutu geçersiz.')
    accepted = []
    for value in boxes:
        if not valid_box(value.get('box')) or not math.isfinite(value.get('score', 0)):
            continue
        if value.get('score', 0) < .5:
            continue
        x0, y0, x1, y1 = value['box']
        b = [max(0., x0), max(0., y0), min(float(width), x1), min(float(height), y1)]
        if valid_box(b):
            accepted.append(dict(value, box=b, excluded=False))
    if not accepted:
        return dict(candidate=None, boxes=[], warnings=['Metin bulunamadı; mevcut seçim korunmalı.'], excluded=0, safe=False)

    warnings = []
    # Only independently edited peers can veto isolated camera-bar text.
    # Current-page content is NOT a reference: that would hide missing numbers.
    envelope = (reference or {}).get('envelope')
    peers = (reference or {}).get('peers', 0)
    if valid_box(envelope) and peers >= 5 and (reference or {}).get('manual'):
        pad = max(12., min(width, height)*.025)
        e = [envelope[0]-pad, envelope[1]-pad, envelope[2]+pad, envelope[3]+pad]
        inside = [b for b in accepted if intersection(b['box'], e)/area(b['box']) > .2]
        # A sparse title, colophon or illustration is not a normal body page.
        if len(inside) >= 12:
            for value in accepted:
                if intersection(value['box'], e) == 0:
                    value['excluded'] = True
            if any(v['excluded'] for v in accepted):
                warnings.append('Kitap düzeninden uzaktaki turuncu metinler öneriye alınmadı; sayfa numarası olmadıklarını kontrol edin.')
    included = [b['box'] for b in accepted if not b['excluded']]
    glyph = statistics.median(min(b[2]-b[0], b[3]-b[1]) for b in included)
    pad = max(8., glyph*.65)
    rect = [max(0., min(b[0] for b in included)-pad), max(0., min(b[1] for b in included)-pad),
            min(float(width), max(b[2] for b in included)+pad), min(float(height), max(b[3] for b in included)+pad)]
    warnings.append('Algılanmayan soluk yazı veya görsel olabilir. Yeşil sınırı kaynakla karşılaştırın.')
    return dict(candidate=rect, boxes=accepted, warnings=warnings,
                excluded=sum(b['excluded'] for b in accepted), safe=False)
