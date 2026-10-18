"""Lossless packing for this program's image-plus-text scan PDFs.

No resampling, threshold changes, JPEG encoding or OCR rewriting. The result
must retain every image pixel, extracted text, page size and rendered page.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import tempfile
import zlib
from concurrent.futures import ThreadPoolExecutor
from collections import deque

import fitz
from pypdf import PdfReader, PdfWriter
from pypdf.generic import NameObject


def compact(source, destination, notify=lambda message: None, workers=4):
    from .pdf_integrity import file_digest, verify
    from .pdf_profiles import publish
    from .memory_budget import bounded_workers
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if destination.exists():
        raise FileExistsError(destination)
    source_hash = file_digest(source)
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
    workers=bounded_workers(max(1,min(4,int(workers))),bytes_per_worker=512*1024**2,reserve=512*1024**2)
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

    count = len(writer.pages)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Serialise and repack through private files: do not retain three complete
    # PDF byte strings alongside the pypdf object graph for a large book.
    with tempfile.TemporaryDirectory(prefix='.stou-lossless-',dir=destination.parent) as folder:
        intermediate=Path(folder)/'objects.pdf';result=Path(folder)/'verified.pdf'
        with intermediate.open('xb') as stream:
            writer.write(stream)
        writer.close()
        del writer
        with fitz.open(intermediate) as packed:
            packed.save(result,garbage=4,deflate=True,deflate_fonts=True,use_objstms=1)
        if result.stat().st_size >= source.stat().st_size:
            shutil.copyfile(source,result)
            repacked=0
        verify(source,result,notify=lambda e: notify(f"Sıkıştırma doğrulaması: {e['page']}/{e['total']}"))
        if file_digest(source)!=source_hash:
            raise ValueError('Kaynak PDF işlem sırasında değişti; yeni çıktı yayımlanmadı.')
        publish(result,destination)
    return {'pages': count, 'before_bytes': source.stat().st_size,
            'after_bytes': destination.stat().st_size, 'repacked_images': repacked,
            'image_pixels_identical': True, 'text_identical': True,
            'page_renders_identical': True}
