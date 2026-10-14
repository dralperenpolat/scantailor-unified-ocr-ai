"""Lossless packing for this program's image-plus-text scan PDFs.

No resampling, threshold changes, JPEG encoding or OCR rewriting. The result
must retain every image pixel, extracted text, page size and rendered page.
"""
from __future__ import annotations

import hashlib
import io
from pathlib import Path
import zlib
from concurrent.futures import ThreadPoolExecutor
from collections import deque

import fitz
from pypdf import PdfReader, PdfWriter
from pypdf.generic import NameObject


def compact(source, destination, notify=lambda message: None, workers=4):
    source, destination = Path(source), Path(destination)
    if destination.exists():
        raise FileExistsError(destination)
    writer = PdfWriter()
    writer.clone_document_from_reader(PdfReader(source))
    repacked = 0
    seen = set()
    # MuPDF and PDF objects stay on this thread. Only zlib's independent byte
    # buffers run concurrently; bounded submission limits additional memory.
    pending = deque()
    def finish_one():
        nonlocal repacked
        index, image, future = pending.popleft()
        packed = future.result()
        if len(packed) < len(image._data):
            image._data = packed
            image[NameObject('/Filter')] = NameObject('/FlateDecode')
            image.pop('/DecodeParms', None)
            repacked += 1
        notify(f'Kayıpsız sıkıştırma: {index + 1}/{len(writer.pages)}')
    workers=max(1,min(4,int(workers)))
    with ThreadPoolExecutor(max_workers=workers) as pool, fitz.open(source) as original:
        for index, page in enumerate(writer.pages):
            images = original[index].get_images(full=True)
            if len(images) != 1:
                continue
            image_ref = next((ref for ref in page['/Resources'].get('/XObject', {}).values()
                              if ref.get_object().get('/Subtype') == '/Image'), None)
            if image_ref is None or image_ref.idnum in seen:
                continue
            seen.add(image_ref.idnum)
            image = image_ref.get_object()
            if (image.get('/BitsPerComponent') != 8 or image.get('/SMask')
                    or image.get('/ColorSpace') not in ('/DeviceGray', '/DeviceRGB')):
                continue
            pix = fitz.Pixmap(original, images[0][0])
            if pix.alpha:
                continue
            pending.append((index,image,pool.submit(zlib.compress,pix.samples,9)))
            if len(pending)>=workers:finish_one()
            if index % 25 == 0:
                fitz.TOOLS.store_shrink(100)
        while pending:finish_one()

    stream = io.BytesIO()
    writer.write(stream)
    with fitz.open(stream=stream.getvalue(), filetype='pdf') as packed:
        result = packed.tobytes(garbage=4, deflate=True, deflate_fonts=True, use_objstms=1)
    if len(result) >= source.stat().st_size:
        result = source.read_bytes()

    with fitz.open(source) as before, fitz.open(stream=result, filetype='pdf') as after:
        if len(before) != len(after):
            raise ValueError('Sıkıştırmada sayfa sayısı değişti.')
        for number, (a, b) in enumerate(zip(before, after), 1):
            if a.rect != b.rect or a.get_text() != b.get_text():
                raise ValueError(f'Sıkıştırmada metin veya sayfa boyutu değişti: {number}')
            images_a, images_b = a.get_images(full=True), b.get_images(full=True)
            if len(images_a) != 1 or len(images_b) != 1:
                raise ValueError(f'Taranmış sayfada tek görüntü bekleniyordu: {number}')
            x, y = fitz.Pixmap(before, images_a[0][0]), fitz.Pixmap(after, images_b[0][0])
            if ((x.width, x.height, x.n) != (y.width, y.height, y.n)
                    or hashlib.sha256(x.samples).digest() != hashlib.sha256(y.samples).digest()):
                raise ValueError(f'Sıkıştırmada görüntü pikselleri değişti: {number}')
            if a.get_pixmap(dpi=72, alpha=False).samples != b.get_pixmap(dpi=72, alpha=False).samples:
                raise ValueError(f'Sıkıştırmada görünüm değişti: {number}')
            if number % 25 == 0:
                notify(f'Sıkıştırma doğrulaması: {number}/{len(before)}')
                fitz.TOOLS.store_shrink(100)
        count = len(before)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('xb') as output:
        output.write(result)
    return {'pages': count, 'before_bytes': source.stat().st_size,
            'after_bytes': len(result), 'repacked_images': repacked,
            'image_pixels_identical': True, 'text_identical': True,
            'page_renders_identical': True}
