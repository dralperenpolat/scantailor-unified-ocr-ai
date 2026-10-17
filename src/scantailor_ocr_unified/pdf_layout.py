"""Fit the printed area, rather than ScanTailor's padded canvas, to paper.

This is a PDF coordinate transform: embedded pixels and the existing OCR text
are preserved. All visible marks (including isolated numbers) and OCR boxes
participate in the bounds. Sparse pages use the book's scale, not an arbitrary
full-page enlargement. Layout is independent of the costly recognition cache.
"""
from __future__ import annotations

import hashlib
from pathlib import Path

import fitz
import numpy as np

VERSION = 'printed-area-v1'
PT_MM = 72 / 25.4


def content_bounds(page):
    # No connected-component size cutoff: even a solitary page number counts.
    # A small safety band retains antialiased edges and faint punctuation.
    pix = page.get_pixmap(dpi=100, colorspace=fitz.csGRAY, alpha=False)
    gray = np.frombuffer(pix.samples, np.uint8).reshape(pix.height, pix.width)
    yy, xx = np.where(gray < 245)
    if not len(xx):
        return None, 0
    box = fitz.Rect(xx.min(), yy.min(), xx.max() + 1, yy.max() + 1) * (72 / 100)
    words = page.get_text('words')
    for word in words:
        rect = fitz.Rect(word[:4]) & page.rect
        if not rect.is_empty:
            box |= rect
    box = fitz.Rect(box.x0 - 2, box.y0 - 2, box.x1 + 2, box.y1 + 2) & page.rect
    return list(box), len(words)


def plan(source, native_layout=None, paper='a4_text', margin_mm=10., target_x_mm=0., notify=lambda *_: None):
    if paper not in ('a4_text', 'a4_fill'):
        raise ValueError('İçeriğe göre yerleşim A4 yazı eşitleme / doldurma için kullanılabilir.')
    if not 0 <= margin_mm < 80:
        raise ValueError('Kenar payı 0–80 mm aralığında olmalı.')
    rows = []
    available = np.array([210 - 2 * margin_mm, 297 - 2 * margin_mm]) * PT_MM
    old_rows = (native_layout or {}).get('pages', [])
    with fitz.open(source) as doc:
        for n, page in enumerate(doc, 1):
            box, words = content_bounds(page)
            old = old_rows[n - 1] if n <= len(old_rows) else {}
            measurement = old.get('body_measurement') or {}
            body = old.get('body_mm') if measurement.get('confidence', 0) >= .3 else None
            rect = fitz.Rect(box) if box else page.rect
            capacity = min(available / [rect.width, rect.height])
            rows.append({'page': n, 'source': str(source), 'paper': paper,
                         'crop_pt': list(rect), 'blank': box is None,
                         'words': words, 'capacity': float(capacity),
                         'old_body_mm': body, 'body_measurement': measurement,
                         'old_page_pt': list(page.rect), 'warning': ''})
            notify(n, len(doc))

    # Long text pages determine the common type size; a short chapter heading,
    # cover, blank, or rotated table cannot inflate/shrink the entire book.
    regular = [r for r in rows if r['words'] >= 80 and r['old_body_mm']
               and not r['body_measurement'].get('sideways')]
    usable = regular or [r for r in rows if r['old_body_mm'] and not r['blank']]
    fits = [r['capacity'] * r['old_body_mm'] for r in usable]
    target = float(target_x_mm or np.quantile(fits, .2) * .985) if fits else None
    scales = [min(r['capacity'], target / r['old_body_mm']) for r in usable] if target else []
    fallback = float(np.median(scales)) if scales else float(np.quantile(
        [r['capacity'] for r in rows if r['words'] >= 80] or [1.], .2))
    for r in rows:
        desired = target / r['old_body_mm'] if target and r['old_body_mm'] else fallback
        if paper == 'a4_fill' and r['words'] >= 80:
            desired = r['capacity']
        scale = min(desired, r['capacity']) if not r['blank'] else 1.
        if not r['blank'] and desired > r['capacity'] * 1.03:
            r['warning'] = f'İçerik korunarak sığdırıldı; ortak yazı ölçeğinden %{(1-scale/desired)*100:.1f} küçük. Geniş tablo / kenar izlerini kontrol edin.'
        crop = fitz.Rect(r['crop_pt'])
        width, height = crop.width * scale, crop.height * scale
        left = (210 * PT_MM - width) / 2
        # Body pages share a top edge. A cover/blank remains centred.
        top = margin_mm * PT_MM if r['words'] >= 40 else (297 * PT_MM - height) / 2
        if r['blank']:
            left = top = 0.
        r.update(scale=scale, dest_pt=[left, top, left + width, top + height],
                 page_mm=[210, 297], image_mm=[width / PT_MM, height / PT_MM],
                 body_mm=r['old_body_mm'] * scale if r['old_body_mm'] else None,
                 target_body_mm=target)
    return {'schema': 2, 'version': VERSION, 'paper': paper, 'margin_mm': margin_mm,
            'target_body_mm': target, 'pages': rows}


def signature(paper, margin_mm, target_x_mm):
    return [VERSION, paper, float(margin_mm), float(target_x_mm)]


def _pixels(doc, page):
    return [(p.width, p.height, p.n, hashlib.sha256(p.samples).hexdigest())
            for p in (fitz.Pixmap(doc, x[0]) for x in page.get_images(full=True))]


def verify_text_transform(before, after, row):
    """Check words and their actual affine motion, not extracted whitespace.

    PDF text extraction can collapse redundant spaces after a pure scale. Some
    input OCR boxes already extend past the page (usually a false recognition
    on a cover). Record those existing defects; reject any NEW displacement,
    missing word or out-of-page text caused by this layout.
    """
    old_text, new_text = before.get_text(), after.get_text()
    old_words, new_words = before.get_text('words'), after.get_text('words')
    if old_text.split() != new_text.split() or [w[4] for w in old_words] != [w[4] for w in new_words]:
        raise ValueError(f'Yerleşimde OCR metni değişti / kesildi: {row["page"]}')
    crop, dest = fitz.Rect(row['crop_pt']), fitz.Rect(row['dest_pt'])
    sx, sy = dest.width / crop.width, dest.height / crop.height
    matrix = fitz.Matrix(sx, 0, 0, sy, dest.x0 - sx * crop.x0, dest.y0 - sy * crop.y0)
    inherited = 0
    for old, new in zip(old_words, new_words):
        old_box, new_box = fitz.Rect(old[:4]), fitz.Rect(new[:4])
        expected = old_box * matrix
        if max(abs(a - b) for a, b in zip(expected, new_box)) > .25:
            raise ValueError(f'Yerleşimde OCR koordinatları görüntüden ayrıldı: {row["page"]}')
        old_inside = (before.rect + (-.2, -.2, .2, .2)).contains(old_box)
        if not (after.rect + (-.2, -.2, .2, .2)).contains(new_box):
            if old_inside:
                raise ValueError(f'Yerleşimde metin sayfa dışına çıktı: {row["page"]}')
            inherited += 1
    return {'whitespace_changed': old_text != new_text,
            'preexisting_ocr_boxes_outside_page': inherited}


def reframe(source, destination, layout, notify=lambda *_: None):
    source, destination = Path(source), Path(destination)
    if destination.exists():
        raise FileExistsError(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with fitz.open(source) as before, fitz.open() as after:
        if len(before) != len(layout['pages']):
            raise ValueError('Yerleşim ve PDF sayfa sayısı uyuşmuyor.')
        for n, row in enumerate(layout['pages']):
            page = after.new_page(width=210 * PT_MM, height=297 * PT_MM)
            page.show_pdf_page(fitz.Rect(row['dest_pt']), before, n,
                               clip=fitz.Rect(row['crop_pt']))
            notify(n + 1, len(before))
        after.set_metadata(before.metadata)
        after.set_toc(before.get_toc())
        after.set_page_labels(before.get_page_labels())
        if before.get_xml_metadata():after.set_xml_metadata(before.get_xml_metadata())
        after.save(destination, garbage=4, deflate=True, use_objstms=1)
    # Check the written PDF, not only in-memory transform parameters.
    whitespace_pages, inherited_pages = [], []
    with fitz.open(source) as before, fitz.open(destination) as after:
        for n, (a, b) in enumerate(zip(before, after), 1):
            row = layout['pages'][n - 1]
            text_check = verify_text_transform(a, b, row)
            if text_check['whitespace_changed']:
                whitespace_pages.append(n)
            if text_check['preexisting_ocr_boxes_outside_page']:
                inherited_pages.append(n)
                row['warning'] += ' Kaynak PDF’den kalan taşan OCR kutusu var; sayfa görüntüsü ve metni korundu, OCR konumunu kontrol edin.'
            if _pixels(before, a) != _pixels(after, b):
                raise ValueError(f'Yerleşimde görüntü pikselleri değişti: {n}')
            # Every measured visible mark lies inside the safe output box.
            if not b.rect.contains(fitz.Rect(row['dest_pt'])):
                raise ValueError(f'Yerleşimde içerik sayfa dışına çıktı: {n}')
            if n % 20 == 0 or n == len(before):
                notify(n, len(before))
                fitz.TOOLS.store_shrink(100)
    return {'pages': len(layout['pages']), 'image_pixels_identical': True,
            'ocr_text_identical': not whitespace_pages, 'ocr_words_identical': True,
            'ocr_coordinates_verified': True, 'whitespace_only_pages': whitespace_pages,
            'preexisting_ocr_box_warning_pages': inherited_pages, 'content_bounds_checked': True,
            'bytes': destination.stat().st_size}
