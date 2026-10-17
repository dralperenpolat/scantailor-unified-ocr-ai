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
from PIL import Image

IMPORT_PIPELINE = 'native-scan-v2'
MAX_PIXELS = 40_000_000


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


def native_scan(page):
    """Return one verified scan at its native pixel scale, or use page rendering.

    Resource lists alone also contain unused images. Require one actual image,
    no visible text / drawing overlays, and a simple upright placement. Compare
    every pixel with a full page render at the native scale before accepting it:
    clipping, transparency, colour transforms and page rotation must all agree.
    White PDF margins are retained without resampling the embedded scan.
    """
    info = page.get_image_info()
    if len(info) != 1:
        return None
    item = info[0]
    if item['width'] * item['height'] > MAX_PIXELS:
        return None
    resources = page.get_images(full=True)
    if len(resources) != 1 or resources[0][1]:
        return None  # masks and composite pages need the complete page renderer
    if page.first_annot is not None or page.first_widget is not None:
        return None
    if any(entry[0] not in ('fill-image', 'ignore-text') for entry in page.get_bboxlog()):
        return None
    a, b, c, d, _, _ = item['transform']
    if a <= 0 or d <= 0 or abs(b) > .001 or abs(c) > .001:
        return None
    bounds = page.rect * page.derotation_matrix
    box = fitz.Rect(item['bbox'])
    if box.is_empty or not (bounds + (-.01, -.01, .01, .01)).contains(box):
        return None
    scale = item['width'] / box.width
    if abs(item['height'] / box.height / scale - 1) > .001:
        return None
    width, height = math.ceil(bounds.width * scale), math.ceil(bounds.height * scale)
    if width * height > MAX_PIXELS or min(width, height) < 1:
        return None
    pix = fitz.Pixmap(page.parent, resources[0][0])
    if pix.alpha or pix.colorspace is None or pix.n not in (1, 3):
        return None
    if (pix.width, pix.height) != (item['width'], item['height']):
        return None
    mode = 'L' if pix.n == 1 else 'RGB'
    scan = Image.frombytes(mode, (pix.width, pix.height), pix.samples)
    canvas = Image.new(mode, (width, height), 255 if mode == 'L' else (255, 255, 255))
    x, y = round((box.x0 - bounds.x0) * scale), round((box.y0 - bounds.y0) * scale)
    if x < 0 or y < 0 or x + scan.width > width or y + scan.height > height:
        return None
    canvas.paste(scan, (x, y))
    if page.rotation:
        canvas = canvas.transpose({90: Image.Transpose.ROTATE_270,
                                   180: Image.Transpose.ROTATE_180,
                                   270: Image.Transpose.ROTATE_90}[page.rotation])
    # No approximate visual match: extraction is used only if native pixels
    # and the PDF's actual visible page agree exactly.
    rendered = page.get_pixmap(matrix=fitz.Matrix(scale, scale),
                               colorspace=fitz.csGRAY if mode == 'L' else fitz.csRGB,
                               alpha=False)
    if canvas.size != (rendered.width, rendered.height) or canvas.tobytes() != rendered.samples:
        return None
    return canvas, {'method': 'extracted', 'effective_dpi': scale * 72,
                    'pixels': list(canvas.size), 'image_xref': resources[0][0],
                    'native_pixels_verified': True}


def render_page(page, dpi, number):
    area = page.rect.width * page.rect.height
    if not math.isfinite(area) or area <= 0:
        raise ValueError(f'Geçersiz sayfa boyutu: {number}')
    if area * .25 > MAX_PIXELS:
        raise ValueError(f'Sayfa güvenli işleme sınırını aşıyor: {number}')
    effective = min(float(dpi), math.sqrt(MAX_PIXELS / area) * 72.)
    pix = page.get_pixmap(dpi=max(36, int(effective)), colorspace=fitz.csRGB, alpha=False)
    if pix.width * pix.height > 42_000_000:
        raise ValueError(f'Sayfa çok büyük: {number}')
    return pix, {'method': 'rendered', 'effective_dpi': int(effective),
                 'pixels': [pix.width, pix.height], 'native_pixels_verified': False}


def import_pdf(source, destination, dpi=300, notify=lambda value: None, *, strategy='auto', pages='', password=''):
    from scantailor_ocr_unified.smart_pdf import page_range
    from scantailor_ocr_unified.storage import ensure_space
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if dpi not in (150, 200, 300, 400, 600): raise ValueError('Geçersiz PDF çözünürlüğü.')
    if strategy not in ('auto', 'render'): raise ValueError('Geçersiz PDF içe aktarma yöntemi.')
    stamp = digest(source)
    name = re.sub(r'[^\w .-]', '_', source.stem)[:70].strip(' .') or 'PDF'
    selection_tag=('_'+hashlib.sha256(str(pages).encode()).hexdigest()[:8]) if pages else ''
    folder = destination / f'{name}_{stamp[:12]}_{strategy}_{dpi}dpi_v2{selection_tag}'
    images = folder/'sayfalar'
    with fitz.open(source) as doc:
        if not doc.is_pdf: raise ValueError('Kaynak dosya PDF değil.')
        if doc.needs_pass and not doc.authenticate(password): raise ValueError('PDF şifreli; şifre gerekli veya yanlış.')
        if not len(doc): raise ValueError('PDF boş.')
        indices=page_range(pages,len(doc))
        images.mkdir(parents=True, exist_ok=True)
        manifest = {'schema': 2, 'pipeline': IMPORT_PIPELINE, 'strategy': strategy,
                    'source': str(source), 'source_sha256': stamp,
                    'dpi': dpi, 'pages': len(indices), 'completed': 0, 'images': str(images),
                    'page_mapping':[{'output_page':n,'source_page':i+1} for n,i in enumerate(indices,1)],
                    'metadata':doc.metadata,'toc':doc.get_toc(),'page_labels':doc.get_page_labels(),
                    'extracted': 0, 'rendered': 0, 'complete': False,
                    'text_policy': 'source text retained separately; corrected geometry receives fresh OCR'}
        save_json(folder/'import.json', manifest)
        for number,index in enumerate(indices,1):
            page=doc[index]
            ensure_space([destination],needed=64*1024**2)
            image_path = images/f'page-{number:05d}.png'
            text_path = folder/f'page-{number:05d}.json'
            # The receipt verifies both artifacts, allowing an interrupted import
            # to resume without silently trusting a truncated image.
            receipt_path = folder/f'page-{number:05d}.receipt.json'
            reuse = False
            try:
                receipt = json.loads(receipt_path.read_text('utf8'))
                reuse = (receipt['source'] == stamp and receipt['dpi'] == dpi
                         and receipt['pipeline'] == IMPORT_PIPELINE and receipt['strategy'] == strategy
                         and receipt['method'] in ('extracted', 'rendered')
                         and receipt['image'] == digest(image_path) and receipt['text'] == digest(text_path))
            except (OSError, KeyError, ValueError): pass
            if not reuse:
                native = native_scan(page) if strategy == 'auto' else None
                pix, metadata = native if native is not None else render_page(page, dpi, number)
                temporary = images/f'.{number}-{uuid.uuid4().hex}.png'
                try:
                    if isinstance(pix, Image.Image):
                        pix.save(temporary, dpi=(metadata['effective_dpi'], metadata['effective_dpi']))
                    else:
                        pix.save(temporary)
                    temporary.replace(image_path)
                finally:
                    temporary.unlink(missing_ok=True)
                words = page.get_text('words')
                save_json(text_path, {'page': number, 'source_page':index+1,'label':page.get_label(),'rect': list(page.rect), 'rotation': page.rotation,
                                     **metadata,
                                     'text': page.get_text(), 'words': words})
                receipt = {'source': stamp, 'dpi': dpi, 'pipeline': IMPORT_PIPELINE,
                           'strategy': strategy, 'method': metadata['method'],
                           'image': digest(image_path), 'text': digest(text_path)}
                save_json(receipt_path, receipt)
            manifest['completed'] = number
            manifest[receipt['method']] += 1
            save_json(folder/'import.json', manifest)
            notify({'stage': 'Tarama resmi çıkarılıyor' if receipt['method'] == 'extracted' else 'PDF sayfası görüntüye çevriliyor',
                    'page': number, 'total': len(indices), 'cached': reuse, 'method': receipt['method']})
            fitz.TOOLS.store_shrink(100)
    if digest(source) != stamp:
        raise ValueError('İçe aktarma sırasında kaynak PDF değişti; yeniden başlatın.')
    manifest['complete'] = True
    save_json(folder/'import.json', manifest)
    return {'ok': True, 'mode': 'import', 'source': str(source), 'source_sha256': stamp,
            'folder': str(images), 'pages': manifest['pages'], 'text_folder': str(folder),
            'strategy': strategy, 'page_mapping':manifest['page_mapping'],'extracted': manifest['extracted'], 'rendered': manifest['rendered']}


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
    parser.add_argument('mode', choices=['import', 'compact','smart'])
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--dpi', type=int, default=300)
    parser.add_argument('--strategy', choices=['auto', 'render'], default='auto')
    parser.add_argument('--pages',default='')
    parser.add_argument('--language',default='deu')
    args = parser.parse_args()
    def emit(value): print('STOUPDF '+json.dumps(value, ensure_ascii=False), flush=True)
    try:
        if args.mode=='import':result=import_pdf(args.source,args.destination,args.dpi,emit,strategy=args.strategy,pages=args.pages)
        elif args.mode=='smart':
            from scantailor_ocr_unified.smart_pdf import process
            from scantailor_ocr_unified.settings import load
            from scantailor_ocr_unified.runtime_layout import app_root
            result=process(args.source,args.destination,{**load(),'root':str(app_root()),'pages':args.pages,'language':args.language,'dpi':args.dpi},emit)
        else:result=compact_pdf(args.source,args.destination,emit)
        emit(result)
    except Exception as error:
        emit({'ok': False, 'error': str(error)})
        raise SystemExit(1)


if __name__ == '__main__': main()
