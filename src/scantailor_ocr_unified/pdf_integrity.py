"""PDF checks independent of the compression policy, with bounded render buffers.

Extracted OCR text alone cannot detect damaged visible letters. Lossless checks
therefore include decoded image samples and rendered pages. Lossy exports keep
document structure and text coordinates, and report display changes separately.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path

import fitz
import numpy as np


def file_digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def normalized(value):
    if isinstance(value, dict):
        return {key: normalized(item) for key, item in value.items()
                if key != 'xref'}
    if isinstance(value, (list, tuple, fitz.Rect, fitz.Point, fitz.Quad, fitz.Matrix)):
        return [normalized(item) for item in value]
    if isinstance(value, float):
        return round(value, 5)
    return value


def document_state(doc):
    attachments = []
    for name in doc.embfile_names():
        info = doc.embfile_info(name)
        # Compressed byte length may legitimately change during repacking.
        attachments.append((name, info.get('filename'), info.get('ufilename'),
                            info.get('description'), hashlib.sha256(doc.embfile_get(name)).hexdigest()))
    return normalized({
        'metadata': {key: value for key, value in doc.metadata.items() if key != 'format'},
        'xmp': doc.get_xml_metadata(),
        'outline': doc.get_toc(simple=False), 'labels': doc.get_page_labels(),
        'attachments': attachments,
    })


def page_state(page):
    annotations = []
    for annot in page.annots() or ():
        annotations.append((annot.type, annot.rect, annot.info, annot.colors,
                            annot.opacity, annot.flags, annot.vertices))
    widgets = [(widget.field_name, widget.field_type, widget.field_value,
                widget.field_flags, widget.rect) for widget in page.widgets() or ()]
    return normalized({
        'geometry': [page.rotation, page.mediabox, page.cropbox, page.trimbox,
                     page.bleedbox, page.artbox],
        'text': page.get_text(), 'words': page.get_text('words'),
        'links': page.get_links(), 'annotations': annotations, 'widgets': widgets,
    })


def image_state(page, pixels):
    """Only visible image occurrences, including inline and nested form images."""
    return [normalized({key: row[key] for key in (
        ('bbox', 'transform', 'width', 'height', 'digest') if pixels else ('bbox', 'transform'))})
        for row in page.get_image_info(hashes=pixels)]


def compare_render(before, after, dpi, checkpoint=lambda: None, gray=False):
    """Compare all display tiles, never a downscaled screenshot or page average alone."""
    bounds = before.rect
    if not all(math.isfinite(v) for v in bounds) or bounds.is_empty:
        raise ValueError('PDF sayfa boyutu geçersiz.')
    # Large CAD pages are not allowed to silently exhaust memory or run forever.
    if bounds.width * bounds.height * (dpi / 72) ** 2 > 300_000_000:
        raise ValueError('PDF sayfası doğrulama sınırını aşıyor; daha küçük sayfa boyutu gerekli.')
    a_list, b_list = before.get_displaylist(), after.get_displaylist()
    matrix = fitz.Matrix(dpi / 72, dpi / 72)
    step = 1024 * 72 / dpi
    total, error, maximum, identical = 0, 0, 0.0, True
    for row in range(math.ceil(bounds.height / step)):
        for column in range(math.ceil(bounds.width / step)):
            checkpoint()
            x, y = bounds.x0 + column * step, bounds.y0 + row * step
            clip = fitz.Rect(x, y, min(bounds.x1, x + step), min(bounds.y1, y + step))
            colorspace = fitz.csGRAY if gray else fitz.csRGB
            a = a_list.get_pixmap(matrix=matrix, colorspace=colorspace, alpha=False, clip=clip)
            b = b_list.get_pixmap(matrix=matrix, colorspace=colorspace, alpha=False, clip=clip)
            if (a.width, a.height, a.n) != (b.width, b.height, b.n):
                raise ValueError('PDF görünüm boyutları değişti.')
            aa, bb = np.frombuffer(a.samples_mv, np.uint8), np.frombuffer(b.samples_mv, np.uint8)
            diff = np.abs(aa.astype(np.int16) - bb)
            tile_error = int(diff.sum())
            total += diff.size
            error += tile_error
            maximum = max(maximum, tile_error / max(1, diff.size) / 255)
            identical = identical and tile_error == 0
    return {'identical': identical, 'mean_absolute_error': error / max(1, total) / 255,
            'max_tile_mean_absolute_error': maximum, 'dpi': dpi, 'color': 'gray' if gray else 'rgb'}


def verify(source, candidate, changed_pages=(), notify=lambda event: None,
           checkpoint=lambda: None, password='', gray=False):
    changed_pages = set(changed_pages)
    rows = []
    with fitz.open(source) as before, fitz.open(candidate) as after:
        for doc in (before, after):
            if doc.needs_pass and not doc.authenticate(password):
                raise ValueError('PDF şifreli; doğru şifre gerekli.')
        if len(before) != len(after):
            raise ValueError('Sıkıştırmada sayfa sayısı değişti.')
        if document_state(before) != document_state(after):
            raise ValueError('Sıkıştırmada PDF bilgileri, yer imleri veya ekleri değişti.')
        for index, (a, b) in enumerate(zip(before, after)):
            checkpoint()
            if page_state(a) != page_state(b):
                raise ValueError(f'Sıkıştırmada metin konumu, sayfa ölçüsü veya açıklama değişti: {index + 1}')
            lossless = index not in changed_pages
            if image_state(a, lossless) != image_state(b, lossless):
                raise ValueError(f'Sıkıştırmada görüntü yerleşimi veya korunan pikseller değişti: {index + 1}')
            metrics = compare_render(a, b, 150 if lossless else 300, checkpoint, gray and not lossless)
            if lossless and not metrics['identical']:
                raise ValueError(f'Kayıpsız sayfanın görünümü değişti: {index + 1}')
            if not lossless and metrics['max_tile_mean_absolute_error'] > .15:
                raise ValueError(f'Sıkıştırılmış sayfa görünümü kalite sınırını aşıyor: {index + 1}')
            rows.append({'page': index + 1, 'lossless': lossless, **metrics})
            notify({'stage': 'PDF bütünlüğü doğrulanıyor', 'page': index + 1, 'total': len(before)})
            fitz.TOOLS.store_shrink(100)
    return {'pages': rows, 'text_identical': True, 'text_positions_identical': True,
            'navigation_identical': True, 'annotations_identical': True,
            'attachments_identical': True, 'page_dimensions_identical': True,
            'image_pixels_identical': not changed_pages,
            'renders_identical': all(row['identical'] for row in rows)}
