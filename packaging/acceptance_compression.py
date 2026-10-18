"""Read-only real-corpus PDF acceptance and measured compression benchmark.

The corpus supplies page locations, never OCR ground truth. All outputs go to a
new work directory. Run using the release interpreter and current source tree.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import sys
import threading
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--corpus', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--pages', type=int, default=250)
    parser.add_argument('--profiles', default='lossless,balanced,small')
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
    import cv2
    import fitz
    import psutil
    from scantailor_ocr_unified.pdf_profiles import compress
    from scantailor_ocr_unified.pdf_integrity import file_digest
    cv2.setNumThreads(2)
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=False)
    corpus = json.loads(Path(args.corpus).read_text('utf8'))
    rows = corpus['pages'][:args.pages]
    if len(rows) != args.pages:
        raise ValueError('Not enough corpus pages')
    sources = {Path(row['pdf']) for row in rows}
    stats = {str(path): [path.stat().st_size, path.stat().st_mtime_ns] for path in sources}
    source = work / 'real-pages-source.pdf'
    with fitz.open() as result:
        for index, row in enumerate(rows):
            with fitz.open(row['pdf']) as original:
                page = row['pdf_page_index']
                result.insert_pdf(original, from_page=page, to_page=page)
            if (index+1) % 25 == 0:
                print(json.dumps({'preparing_pages': index+1, 'total': len(rows)}), flush=True)
            fitz.TOOLS.store_shrink(100)
        result.set_metadata({'title': 'Real-page compression acceptance', 'creator': 'ScanTailor acceptance'})
        result.save(source, garbage=4, deflate=True, use_objstms=1)
    digest = file_digest(source)
    reports = []
    process = psutil.Process()
    for profile in args.profiles.split(','):
        peak = [process.memory_info().rss]
        stop = threading.Event()
        def monitor():
            while not stop.wait(.1):
                peak[0] = max(peak[0], process.memory_info().rss)
        thread = threading.Thread(target=monitor, daemon=True)
        thread.start()
        last = [0.]
        def progress(event):
            now = time.monotonic()
            if now-last[0] >= 15:
                last[0] = now
                print(json.dumps({'profile': profile, **event}, ensure_ascii=False), flush=True)
        try:
            report = compress(source, work / (profile + '.pdf'), profile, progress)
        finally:
            stop.set();thread.join()
        report['process_peak_rss_mib'] = round(peak[0] / 1024**2, 1)
        assert report['after_bytes'] <= report['before_bytes']
        assert report['source_sha256'] == digest == file_digest(source)
        with fitz.open(source) as original, fitz.open(report['pdf']) as compressed:
            assert len(compressed) == len(rows)
            for a, b in zip(original, compressed):
                assert a.get_text('words') == b.get_text('words')
            for index in (0, len(rows)//2, len(rows)-1):
                compressed[index].get_pixmap(dpi=120).save(work / f'{profile}-page-{index+1}.png')
        (work / f'{profile}.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), 'utf8')
        summary = {key: report[key] for key in ('profile', 'before_bytes', 'after_bytes', 'saved_percent',
                   'seconds', 'images_changed', 'lossless', 'process_peak_rss_mib')}
        summary['skipped'] = dict(Counter(row['reason'] for row in report['skipped_images']))
        reports.append(summary)
        print(json.dumps(summary, ensure_ascii=False), flush=True)
    assert all([Path(path).stat().st_size, Path(path).stat().st_mtime_ns] == stamp for path, stamp in stats.items())
    result = {'pages': len(rows), 'source_books': len(sources), 'source_pdf_sha256': digest,
              'original_files_unchanged_by_stat': True, 'reports': reports,
              'ocr_accuracy_measured': False, 'note': 'Corpus references are not human verified.'}
    (work / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), 'utf8')
    print(json.dumps(result, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
