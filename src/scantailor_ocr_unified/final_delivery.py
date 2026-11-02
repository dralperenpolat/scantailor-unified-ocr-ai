"""After a complete queue, publish one verified PDF per book and retire variants.

The OS queue lock excludes exporters. A small per-book transaction receipt
allows recovery after replacement but before the queue commit. Only PDFs in
the explicitly owned delivery directory are removed; sources and projects
are not cleanup targets. No cleanup begins until every book is published.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import time
import uuid

import fitz

from .pdf_compression import PROFILES, compress, options_signature
from .pdf_integrity import file_digest
from .queue_lock import exclusive_queue
from .queue_store import QueueStore
from .settings import atomic_json


def _safe_name(name):
    from .native_worker import safe_name
    return safe_name(name)


def _owned_directory(job, output_root):
    root = Path(output_root).resolve()
    directory = root / (_safe_name(job['title']) + '_' + job['id'][:8])
    if directory.is_symlink() or directory.is_junction() or directory.resolve().parent != root:
        raise ValueError('Kitap çıktı klasörü başka bir konuma yönleniyor; temizlik yapılmadı.')
    delivery = Path(job['delivery_folder']).resolve()
    if not delivery.is_relative_to(directory.resolve()):
        raise ValueError('Teslim klasörü kitapla eşleşmiyor; temizlik yapılmadı.')
    for key in ('folder', 'source_pdf'):
        if job.get(key) and Path(job[key]).resolve().is_relative_to(directory.resolve()):
            raise ValueError('Özgün kaynak teslim klasöründe; otomatik temizlik yapılmadı.')
    return directory


def _inventory(directory):
    result = []
    for current, dirs, files in os.walk(directory, followlinks=False):
        for name in dirs + files:
            path = Path(current) / name
            if path.is_symlink() or path.is_junction() or not path.resolve().is_relative_to(directory):
                raise ValueError('Teslim klasöründe yönlendirilmiş dosya var; temizlik yapılmadı.')
        for name in files:
            path = Path(current) / name
            if path.suffix.lower() == '.pdf':
                result.append({'path': str(path), 'sha256': file_digest(path), 'bytes': path.stat().st_size})
    return result


def _ready(jobs):
    return bool(jobs) and all(
        job.get('reading_pdf') and job.get('delivery_folder') and not job.get('error')
        and isinstance(job.get('verification'), dict) and job['verification'].get('pages', 0) > 0
        and job['verification'].get('ocr_timing') != 'none'
        and (not job.get('edit_request_id') or job.get('edit_completed_id') == job['edit_request_id'])
        for job in jobs)


def _expected_hash(job, source):
    if Path(job.get('pdf', '')).resolve() == source:
        return job.get('pdf_hash')
    if Path(job.get('compressed', {}).get('pdf', '')).resolve() == source:
        return job['compressed'].get('output_sha256')
    raise ValueError('Son okuma PDF kaydı doğrulanamadı.')


def _commit(job, receipt, store, data):
    directory = _owned_directory(job, data['output_root'])
    destination = Path(receipt['destination'])
    staged = Path(receipt['staged'])
    report = receipt['report']
    if (destination != directory / (_safe_name(job['title']) + '.pdf')
            or staged.parent != directory or not staged.name.startswith('.final-')
            or staged.suffix.lower() != '.pdf' or staged.is_symlink()
            or not report.get('text_identical') or not report.get('text_positions_identical')):
        raise ValueError('Son PDF işlem kaydının yolu veya doğrulaması geçersiz.')
    actual = file_digest(destination) if destination.exists() else None
    if actual != report['output_sha256']:
        if actual != receipt['destination_before']:
            raise ValueError('Hedef PDF işlem sırasında değişti; üzerine yazılmadı.')
        if not staged.is_file() or file_digest(staged) != report['output_sha256']:
            raise ValueError('Doğrulanmış geçici PDF bulunamadı.')
        if file_digest(receipt['source']) != report['source_sha256']:
            raise ValueError('Okuma PDF işlem sırasında değişmiş; son çıktı yayımlanmadı.')
        # Same-volume replacement happens only after all-page verification.
        os.replace(staged, destination)
    elif staged.exists():
        staged.unlink()
    if file_digest(destination) != report['output_sha256']:
        raise ValueError('Son PDF yayımlama doğrulaması başarısız.')
    job.update(pdf=str(destination), reading_pdf=str(destination), pdf_hash=report['output_sha256'],
               delivery_folder=str(destination.parent))
    job['compressed'] = {**report, 'pdf': str(destination), 'source_hash': report['output_sha256']}
    job['final_delivery'] = {
        'pdf': str(destination), 'sha256': report['output_sha256'],
        'profile': report['profile'], 'options_signature': report['options_signature'],
        'before_bytes': report['before_bytes'], 'after_bytes': report['after_bytes'],
        'saved_percent': report['saved_percent'], 'lossless': report['lossless'],
        'receipt': receipt['receipt'], 'published_at': time.time(), 'cleaned': False,
    }
    job.setdefault('verification', {})['final_compression'] = {
        'text_identical': True, 'text_positions_identical': True,
        'image_pixels_identical': report['image_pixels_identical'],
        'sha256': report['output_sha256'], 'pages': report['pages'],
    }
    for key in ('visual_pdf', 'visual_pdf_signature', 'layout_revision', 'pdfa'):
        job.pop(key, None)
    store.save(data, dirty_jobs={job['id']})
    store.publish(data)


def finalize_data(data, store, profile, options=None, notify=lambda event: None, checkpoint=lambda: None):
    if profile not in PROFILES:
        raise ValueError('Bilinmeyen son PDF sıkıştırma profili.')
    options = options if options is not None else ({'recompress_flate': True} if profile == 'lossless' else {})
    signature = options_signature(profile, options)
    if not _ready(data['jobs']):
        raise ValueError('Tüm kitapların PDF ve OCR işlemi bitmeden son sıkıştırma/temizlik başlamaz.')
    # Preflight every path before publishing or removing anything.
    directories = [_owned_directory(job, data['output_root']) for job in data['jobs']]
    if len(set(directories)) != len(directories):
        raise ValueError('Birden fazla kitap aynı teslim klasörünü kullanıyor.')
    for job, directory in zip(data['jobs'], directories):
        checkpoint()
        final = job.get('final_delivery', {})
        if final:
            if final.get('options_signature') != signature:
                raise ValueError('Son PDF daha önce başka ayarla hazırlandı; özgün görüntülerden yeni çıktı gerekli.')
            if file_digest(final['pdf']) != final['sha256']:
                raise ValueError('Son PDF dışarıdan değiştirilmiş; temizlik yapılmadı.')
            continue
        receipt_path = directory / '.final-pdf.json'
        if receipt_path.exists():
            receipt = json.loads(receipt_path.read_text('utf8'))
            if receipt.get('job_id') != job['id'] or receipt['report']['options_signature'] != signature:
                raise ValueError('Önceki son PDF işlemi bu kitap/ayar ile eşleşmiyor.')
            _commit(job, receipt, store, data)
            continue
        # Compression always starts from the verified master, never a previous
        # lossy reading rendition. The final result becomes the single master.
        source = Path(job['pdf']).resolve()
        if not source.is_relative_to(directory.resolve()) or file_digest(source) != _expected_hash(job, source):
            raise ValueError('Doğrulanmış okuma PDF değişmiş veya teslim klasörü dışında.')
        inventory = _inventory(directory)
        destination = directory / (_safe_name(job['title']) + '.pdf')
        staged = directory / ('.final-' + uuid.uuid4().hex + '.pdf')
        def progress(event):
            checkpoint()
            notify({'book': job['title'], 'job': job['id'], **event})
        def cancelled():
            checkpoint()
            return False
        try:
            report = compress(source, staged, profile, progress, options=options, cancelled=cancelled)
            if report['pages'] != job['verification']['pages']:
                raise ValueError('Son PDF sayfa sayısı tamamlanma kaydıyla eşleşmiyor.')
        except BaseException:
            staged.unlink(missing_ok=True)
            raise
        receipt = {
            'version': 1, 'job_id': job['id'], 'receipt': str(receipt_path),
            'source': str(source), 'destination': str(destination), 'staged': str(staged),
            'destination_before': next((r['sha256'] for r in inventory if Path(r['path']) == destination), None),
            'report': report, 'cleanup': inventory,
        }
        atomic_json(receipt_path, receipt)
        _commit(job, receipt, store, data)
    # Every book now has a complete verified replacement. Retire variants only
    # after this barrier, not at the first OCR success or a worker interruption.
    for job, directory in zip(data['jobs'], directories):
        checkpoint()
        if _owned_directory(job, data['output_root']) != directory or directory.resolve() != directory:
            raise ValueError('Teslim klasörü işlem sırasında değişmiş; temizlik yapılmadı.')
        final = job['final_delivery']
        if file_digest(final['pdf']) != final['sha256']:
            raise ValueError('Son PDF değişmiş; kalan ara PDF dosyaları korundu.')
        receipt = json.loads(Path(final['receipt']).read_text('utf8'))
        destination = Path(final['pdf']).resolve()
        for entry in receipt['cleanup']:
            path = Path(entry['path'])
            if path.resolve() == destination or not path.exists():
                continue
            if path.is_symlink() or not path.resolve().is_relative_to(directory):
                raise ValueError('Temizlik yolu kitap çıktı klasörü dışında.')
            checkpoint()
            if file_digest(path) != entry['sha256']:
                raise ValueError('Ara PDF dışarıdan değiştirilmiş; bu dosya silinmedi.')
            path.unlink()
        remaining = [row['path'] for row in _inventory(directory)]
        if remaining != [str(destination)]:
            raise ValueError('Kitap çıktı klasöründe yeni veya tanınmayan ek PDF var; korundu.')
        with fitz.open(destination) as doc:
            if len(doc) != job['verification']['pages']:
                raise ValueError('Son teslim sayfa sayısı doğrulanamadı.')
        final.update(cleaned=True, completed_at=time.time())
        store.save(data, dirty_jobs={job['id']})
        store.publish(data)
        notify({'book': job['title'], 'job': job['id'], 'stage': 'Tek PDF teslimi tamamlandı'})
    return {'books': len(data['jobs']),
            'before_bytes': sum(j['final_delivery']['before_bytes'] for j in data['jobs']),
            'after_bytes': sum(j['final_delivery']['after_bytes'] for j in data['jobs']),
            'profile': profile, 'single_pdf_per_book': True}


def finalize(queue, profile='lossless', options=None, notify=lambda event: None):
    queue = Path(queue).resolve()
    with exclusive_queue(queue):
        store = QueueStore(queue)
        try:
            data = store.load(json.loads(queue.read_text('utf8')))
            def checkpoint():
                if list(Path(str(queue) + '.commands').glob('*.request.json')) or list(Path(str(queue) + '.edits').glob('*.request.json')):
                    raise ValueError('Yeni kuyruk/düzeltme isteği var; son işlem durdu, mevcut çıktılar korundu.')
            checkpoint()
            return finalize_data(data, store, profile, options, notify, checkpoint)
        finally:
            store.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--queue', required=True)
    parser.add_argument('--profile', choices=PROFILES, default='lossless')
    args = parser.parse_args()
    result = finalize(args.queue, args.profile, notify=lambda e: print(json.dumps(e, ensure_ascii=False), flush=True))
    print(json.dumps(result, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
