"""Existing PDFs: reversible scan import and verified lossless file packing.

Imports keep the original PDF and extract its text / boxes before rendering.
Edited page geometry receives fresh OCR in the normal pipeline. Original text
is not silently placed over changed geometry. Work is streamed one page at a time.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import uuid

import fitz


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''): h.update(block)
    return h.hexdigest()


def save_json(path, value):
    path = Path(path)
    temp = path.with_suffix('.'+uuid.uuid4().hex+'.tmp')
    temp.write_text(json.dumps(value, ensure_ascii=False, allow_nan=False), encoding='utf8')
    temp.replace(path)


def import_pdf(source, destination, dpi=300, notify=lambda value: None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if dpi not in (150, 200, 300, 400, 600): raise ValueError('Geçersiz PDF çözünürlüğü.')
    stamp = digest(source)
    name = re.sub(r'[^\w .-]', '_', source.stem)[:70].strip(' .') or 'PDF'
    folder = destination / f'{name}_{stamp[:12]}_{dpi}dpi'
    images = folder/'sayfalar'
    images.mkdir(parents=True, exist_ok=True)
    with fitz.open(source) as doc:
        if doc.needs_pass: raise ValueError('PDF şifreli. Önce erişilebilir bir kopyasını açın.')
        if not len(doc): raise ValueError('PDF boş.')
        manifest = {'schema': 1, 'source': str(source), 'source_sha256': stamp,
                    'dpi': dpi, 'pages': len(doc), 'completed': 0, 'images': str(images),
                    'text_policy': 'source text retained separately; corrected geometry receives fresh OCR'}
        save_json(folder/'import.json', manifest)
        for number, page in enumerate(doc, 1):
            image_path = images/f'page-{number:05d}.png'
            text_path = folder/f'page-{number:05d}.json'
            # The receipt verifies both artifacts, allowing an interrupted import
            # to resume without silently trusting a truncated image.
            receipt_path = folder/f'page-{number:05d}.receipt.json'
            reuse = False
            try:
                receipt = json.loads(receipt_path.read_text('utf8'))
                reuse = receipt['source'] == stamp and receipt['dpi'] == dpi and receipt['image'] == digest(image_path) and receipt['text'] == digest(text_path)
            except (OSError, KeyError, ValueError): pass
            if not reuse:
                area = page.rect.width*page.rect.height
                if not math.isfinite(area) or area <= 0: raise ValueError(f'Geçersiz sayfa boyutu: {number}')
                if area*.25 > 40_000_000: raise ValueError(f'Sayfa güvenli işleme sınırını aşıyor: {number}')
                effective = min(float(dpi), math.sqrt(40_000_000/area)*72.)
                pix = page.get_pixmap(dpi=max(36, int(effective)), colorspace=fitz.csRGB, alpha=False)
                if pix.width*pix.height > 42_000_000: raise ValueError(f'Sayfa çok büyük: {number}')
                temporary = images/f'.{number}-{uuid.uuid4().hex}.png'
                pix.save(temporary)
                temporary.replace(image_path)
                words = page.get_text('words')
                save_json(text_path, {'page': number, 'rect': list(page.rect), 'rotation': page.rotation,
                                     'effective_dpi': int(effective), 'pixels': [pix.width, pix.height],
                                     'text': page.get_text(), 'words': words})
                save_json(receipt_path, {'source': stamp, 'dpi': dpi, 'image': digest(image_path), 'text': digest(text_path)})
            manifest['completed'] = number
            save_json(folder/'import.json', manifest)
            notify({'stage': 'PDF sayfaları hazırlanıyor', 'page': number, 'total': len(doc), 'cached': reuse})
            fitz.TOOLS.store_shrink(100)
    return {'ok': True, 'mode': 'import', 'source': str(source), 'folder': str(images), 'pages': manifest['pages'], 'text_folder': str(folder)}


def compact_pdf(source, destination, notify=lambda value: None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or destination.exists(): raise FileExistsError('Çıktı için yeni bir dosya adı gerekli.')
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.parent/f'.pdf-pack-{uuid.uuid4().hex}.pdf'
    try:
        with fitz.open(source) as before:
            if before.needs_pass: raise ValueError('PDF şifreli.')
            before.save(temporary, garbage=4, deflate=True, deflate_images=True, deflate_fonts=True, use_objstms=1)
            with fitz.open(temporary) as after:
                if len(before) != len(after): raise ValueError('Sayfa sayısı değişti.')
                for number, (a, b) in enumerate(zip(before, after), 1):
                    if a.rect != b.rect or a.get_text() != b.get_text(): raise ValueError(f'Metin / boyut değişti: {number}')
                    # Includes vectors, text, multiple images, masks, annotations.
                    # Pixel comparison is of the final display at 150 dpi.
                    pa = a.get_pixmap(dpi=150, alpha=False)
                    pb = b.get_pixmap(dpi=150, alpha=False)
                    if (pa.width, pa.height, pa.n) != (pb.width, pb.height, pb.n) or hashlib.sha256(pa.samples).digest() != hashlib.sha256(pb.samples).digest():
                        raise ValueError(f'Görünüm değişti: {number}')
                    notify({'stage': 'Kayıpsız çıktı doğrulanıyor', 'page': number, 'total': len(before)})
                    fitz.TOOLS.store_shrink(100)
                pages = len(before)
        # Never deliver a larger "compressed" file. Stream the original if needed.
        if temporary.stat().st_size >= source.stat().st_size:
            import shutil
            shutil.copyfile(source, temporary)
        # Atomic, exclusive publication: cancellation cannot expose a partial PDF.
        import os
        os.link(temporary, destination)
        return {'ok': True, 'mode': 'compact', 'pdf': str(destination), 'pages': pages,
                'before_bytes': source.stat().st_size, 'after_bytes': destination.stat().st_size,
                'text_identical': True, 'render_dpi': 150, 'renders_identical': True}
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['import', 'compact'])
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--dpi', type=int, default=300)
    args = parser.parse_args()
    def emit(value): print('STOUPDF '+json.dumps(value, ensure_ascii=False), flush=True)
    try:
        result = (import_pdf(args.source, args.destination, args.dpi, emit) if args.mode == 'import'
                  else compact_pdf(args.source, args.destination, emit))
        emit(result)
    except Exception as error:
        emit({'ok': False, 'error': str(error)})
        raise SystemExit(1)


if __name__ == '__main__': main()
