"""Verify PDF -> imported pages -> native processing -> OCR in an installed package."""
from pathlib import Path
import argparse
import io
import json
import os
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()
    root, work = Path(args.root).resolve(), Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    os.environ.update(STOU_ROOT=str(root), STOU_DATA=str(work/'user'),
                      STOU_CPU_ONLY='1', CUDA_VISIBLE_DEVICES='-1',
                      PYTHONPATH=str(root/'src'), PYTHONUTF8='1',
                      OMP_NUM_THREADS='2', OMP_THREAD_LIMIT='2',
                      QT_QPA_PLATFORM='offscreen')
    sys.path.insert(0, str(root/'src'))
    from PIL import Image, ImageDraw, ImageFont
    import fitz
    from scantailor_ocr_unified.pdf_tools import import_pdf, digest

    image = Image.new('RGB', (1200, 1600), 'white')
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=32)
    draw.rectangle((80, 90, 1120, 1490), outline='black', width=2)
    for y, text in zip((120, 280, 440, 600), (
            'SCAN IMPORT TEST', 'Original raster image: 1200 x 1600',
            'Page order and native pixels are preserved.',
            'Footnote 1: text near the bottom stays visible.')):
        draw.text((100, y), text, font=font, fill='black')
    buffer = io.BytesIO()
    image.save(buffer, format='PNG')
    source = work/'Türkçe PDF deneme.pdf'
    with fitz.open() as pdf:
        page = pdf.new_page(width=600, height=800)
        page.insert_image(page.rect, stream=buffer.getvalue())
        page.insert_text((45, 60), 'Existing hidden OCR', render_mode=3)
        page = pdf.new_page(width=600, height=800)
        page.insert_text((45, 50), 'SECOND PAGE - VISIBLE TEXT', fontsize=18)
        page.draw_rect(fitz.Rect(45, 80, 555, 300))
        page.insert_image(fitz.Rect(60, 340, 280, 650), stream=buffer.getvalue())
        page.insert_text((45, 750), 'Footer: complex content must be preserved.', fontsize=14)
        pdf.save(source)
    original = digest(source)
    imported = import_pdf(source, work/'import', 150)
    assert (imported['pages'], imported['extracted'], imported['rendered']) == (2, 1, 1), imported
    queue, control = work/'queue.json', work/'control.json'
    queue.write_text(json.dumps({'schema': 1, 'output_root': str(work/'books'), 'jobs': [{
        'id': 'pdf-import-test', 'title': 'PDF yeniden işleme',
        'folder': imported['folder'], 'split': '1', 'source_pdf': str(source),
        'status': 'Bekliyor'}]}, ensure_ascii=False), encoding='utf8')
    control.write_text('{"state":"run"}', encoding='utf8')
    cli = root/('native/bin/scantailor-experimental-cli'+('.exe' if os.name == 'nt' else ''))
    request = {'root': str(root), 'cli': str(cli), 'queue': str(queue),
        'control': str(control), 'mode': 'export', 'language': 'eng',
        'appearance': 'gray', 'ocr_timing': 'during', 'paper': 'a4', 'margin_mm': 10,
        'threads': 2, 'auto_warp': False, 'native_scale': 1, 'page_cleanup': False,
        'ocr_engine': 'tesseract', 'ocr_device': 'cpu', 'tess_quality': 'best'}
    request_path = work/'request.json'
    request_path.write_text(json.dumps(request), encoding='utf8')
    started = time.monotonic()
    with (work/'worker.log').open('w', encoding='utf8') as log:
        subprocess.run([sys.executable, '-u', '-m', 'scantailor_ocr_unified.native_worker',
                        str(request_path)], env=os.environ, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=300)
    job = json.loads(queue.read_text('utf8'))['jobs'][0]
    assert not job.get('error'), job.get('error')
    with fitz.open(job['pdf']) as pdf:
        assert len(pdf) == 2
        texts = [page.get_text() for page in pdf]
        assert 'SCAN IMPORT TEST' in texts[0], texts[0]
        assert 'SECOND PAGE' in texts[1], texts[1]
        for number, page in enumerate(pdf, 1):
            page.get_pixmap(dpi=110, alpha=False).save(work/f'final-{number}.png')
    assert digest(source) == original
    result = {'pages': 2, 'extracted': imported['extracted'], 'rendered': imported['rendered'],
              'source_unchanged': True, 'ocr_chars': [len(text) for text in texts],
              'seconds': round(time.monotonic()-started, 2), 'result': 'passed'}
    (work/'result.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
