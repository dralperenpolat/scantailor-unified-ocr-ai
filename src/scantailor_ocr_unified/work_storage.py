"""Publish verified PDFs from a local working disk without moving originals."""
from pathlib import Path
import os
import uuid
from .project import file_hash


def publish_book(job, working, destination, checkpoint=lambda: None):
    working, destination = Path(working).resolve(), Path(destination).resolve()
    if working == destination:
        return
    destination.mkdir(parents=True, exist_ok=True)
    # Only completed PDFs referenced by this job are deliveries. Page caches,
    # unfinished files and unrelated files in the work directory stay there.
    refs = [(job, key) for key in ('pdf', 'reading_pdf', 'visual_pdf') if job.get(key)]
    refs += [(job[name], key) for name, key in
             (('compressed', 'pdf'), ('pdfa', 'pdf'), ('layout_revision', 'base_pdf'))
             if job.get(name, {}).get(key)]
    published = {}
    for owner, key in refs:
        source = Path(owner[key]).resolve()
        if source.parent != working or source.suffix.lower() != '.pdf':
            continue
        if source in published:
            continue
        checkpoint()
        digest = file_hash(source)
        target = destination/source.name
        if target.exists() and file_hash(target) != digest:
            target = destination/(source.stem+'-'+uuid.uuid4().hex[:12]+source.suffix)
        if not target.exists():
            temporary = destination/('.publish-'+uuid.uuid4().hex+'.tmp')
            try:
                with source.open('rb') as src, temporary.open('xb') as dst:
                    while chunk := src.read(4*1024**2):
                        checkpoint();dst.write(chunk)
                    dst.flush();os.fsync(dst.fileno())
                if file_hash(temporary) != digest:
                    raise ValueError('PDF kopyasının doğrulaması başarısız; çalışma dosyası korundu.')
                # Publish on the destination volume, atomically and exclusively
                # (Windows rename also supports external exFAT disks).
                from .pdf_profiles import publish
                publish(temporary,target)
            finally:
                temporary.unlink(missing_ok=True)
        if file_hash(target) != digest:
            raise ValueError('Son PDF doğrulanamadı; çalışma dosyası korundu.')
        published[source] = str(target)
    # Update references only after every requested delivery verifies.
    for owner, key in refs:
        source = Path(owner[key]).resolve()
        if source in published:owner[key] = published[source]
    job['delivery_folder'] = str(destination)
