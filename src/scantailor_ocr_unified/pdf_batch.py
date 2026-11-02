"""Resumable folder-mirrored PDF OCR/compression, with explicit source replacement."""
from __future__ import annotations

import io
import json
import os
from pathlib import Path
import shutil
import sqlite3
import tempfile
import time
import uuid
from contextlib import contextmanager

import fitz

from .pdf_compression import CompressionCancelled, compress
from .pdf_integrity import document_state, file_digest, image_state, page_state
from .pdf_paths import native_path, display_path
from .queue_lock import exclusive_queue

MODES = ('lossless', 'mono_text', 'mono_all')
SMALL_SUFFIX = ' - küçük'


@contextmanager
def _connect(path):
    db = sqlite3.connect(path, timeout=15)
    db.execute('PRAGMA journal_mode=WAL')
    db.execute('PRAGMA synchronous=FULL')
    try:
        with db:
            yield db
    finally:
        db.close()


def _metadata(db):
    return json.loads(db.execute('SELECT value FROM meta WHERE key=?', ('settings',)).fetchone()[0])


def _write_job(db, job):
    with db:
        db.execute('UPDATE jobs SET body=? WHERE id=?', (json.dumps(job, ensure_ascii=False), job['id']))


def inspect(batch):
    columns = ('id','source','destination','relative','status','before_bytes','after_bytes','saved_percent','ocr_pages','ocr_empty_pages','error','replaced')
    with _connect(batch) as db:
        # Do not load every full per-page verification report just to refresh
        # the library table. A large batch can keep gigabytes of those reports.
        query = 'SELECT '+','.join(f"json_extract(body,'$.{key}')" for key in columns)+' FROM jobs ORDER BY id'
        jobs = []
        for values in db.execute(query):
            job = {key:value for key,value in zip(columns,values) if value is not None}
            if 'ocr_empty_pages' in job:job['ocr_empty_pages']=json.loads(job['ocr_empty_pages'])
            if 'replaced' in job:job['replaced']=bool(job['replaced'])
            jobs.append(job)
        settings = _metadata(db)
    return {'batch': str(batch), 'settings': settings, 'jobs':jobs}


def create(inputs, batch_root, output_root='', profile='mono_text', notify=lambda e: None, cancelled=lambda: False):
    if profile not in MODES:
        raise ValueError('Az sıkıştır, gerektiğinde gri koru veya tam sıkıştır seçin.')
    roots = list(dict.fromkeys(Path(native_path(p,force=True)) for p in inputs))
    if not roots or any(not p.exists() for p in roots):
        raise ValueError('En az bir mevcut PDF veya klasör seçin.')
    target = Path(native_path(output_root,force=True)) if output_root else None
    if target and any(p.is_dir() and p == target for p in roots):
        raise ValueError('Ayna klasörü kaynak klasörüyle aynı olamaz; yanına kaydetmek için hedefi boş bırakın.')
    try:
        base = Path(os.path.commonpath([p if p.is_dir() else p.parent for p in roots]))
    except ValueError:
        base = None
    files = set()
    for root in roots:
        if root.is_file():
            if root.suffix.lower() != '.pdf':
                raise ValueError('Yalnız PDF dosyaları eklenebilir.')
            files.add(root)
            continue
        for directory, dirs, names in os.walk(root, followlinks=False):
            if cancelled():
                raise CompressionCancelled('Dosya listeleme durduruldu.')
            dirs[:] = [name for name in dirs if not (Path(directory)/name).is_symlink()
                       and not (Path(directory)/name).is_junction()
                       and (not target or not (Path(directory)/name).resolve().is_relative_to(target))]
            for name in names:
                path = Path(directory)/name
                if path.suffix.lower() == '.pdf' and not path.is_symlink() and SMALL_SUFFIX not in path.stem:
                    if not target or not path.resolve().is_relative_to(target):
                        files.add(path.resolve())
            notify({'stage':'PDF dosyaları listeleniyor','page':len(files),'total':0})
    if not files:
        raise ValueError('Seçimde işlenecek PDF bulunamadı.')
    planned = set()
    rows = []
    for number, source in enumerate(sorted(files, key=lambda p: str(p).casefold()), 1):
        relative = source.relative_to(base) if base else Path(source.drive.replace(':','') or 'root').joinpath(*source.parts[1:])
        destination = target/relative if target else source.with_name(source.stem + SMALL_SUFFIX + '.pdf')
        if destination == source:
            raise ValueError('Çıktı kaynak PDF üzerine gelemez.')
        original = destination
        version = 2
        while destination.exists() or destination in planned:
            destination = original.with_name(f'{original.stem}_v{version}.pdf')
            version += 1
        planned.add(destination)
        rows.append({'id': number, 'source': display_path(source), 'destination': display_path(destination),
                     'relative': str(relative), 'status': 'waiting', 'before_bytes': source.stat().st_size})
    batch_root = Path(batch_root).resolve()
    batch_root.mkdir(parents=True, exist_ok=True)
    batch = batch_root / ('pdf-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8] + '.sqlite3')
    with _connect(batch) as db:
        db.executescript('CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL); CREATE TABLE jobs(id INTEGER PRIMARY KEY,body TEXT NOT NULL);')
        db.execute('INSERT INTO meta VALUES(?,?)', ('settings', json.dumps({
            'inputs': [display_path(p) for p in roots], 'output_root': display_path(target) if target else '',
            'profile': profile, 'created': time.time(), 'version': 1,
        }, ensure_ascii=False)))
        db.executemany('INSERT INTO jobs VALUES(?,?)', [(r['id'], json.dumps(r, ensure_ascii=False)) for r in rows])
    return inspect(batch)


def _recognize(page, conf, work):
    from PIL import Image
    from .pdf_tools import render_page
    from .runtime_layout import resource_path, native_model_directory
    from .pdf_export import recognition_signature, text_layer_cache
    engine = conf.get('ocr_engine', 'tesseract')
    if engine not in ('paddle','tesseract','kraken'):
        raise ValueError('Metinsiz sayfalar için bir OCR motoru seçin.')
    dpi = min(300, max(72, int(float(conf.get('ocr_max_edge',4200))*72/max(page.rect.width,page.rect.height))))
    pix, _ = render_page(page, dpi, page.number+1)
    pix.set_dpi(dpi,dpi)
    image = Image.frombytes('RGB',(pix.width,pix.height),pix.samples)
    tessdata = resource_path('tessdata_'+conf.get('tess_quality','best'), conf.get('root'))
    language = conf.get('language','deu')
    def recognize():
        if engine in ('paddle','kraken'):
            from .ocr_backend import recognize_pdf
            return recognize_pdf(image, conf, work)
        return pix.pdfocr_tobytes(language=language, tessdata=native_model_directory(tessdata))
    try:
        if conf.get('ocr_cache'):
            signature = recognition_signature(conf,engine,tessdata,language)
            return text_layer_cache(image,0,signature,conf['ocr_cache'],recognize)
        return recognize()
    finally:
        image.close()


def add_missing_ocr(source, destination, conf, notify=lambda e: None, checkpoint=lambda: None):
    """Add hidden text in place in a cloned document, retaining original objects."""
    from pypdf import PdfReader, PdfWriter
    from .storage import workspace
    rows = []
    with fitz.open(native_path(source)) as before, fitz.open(native_path(source)) as after, workspace(conf) as work:
        for doc in (before,after):
            if doc.needs_pass and not doc.authenticate(conf.get('password','')):
                raise ValueError('PDF şifresi gerekli veya yanlış.')
        if before.get_sigflags() > 0:
            raise ValueError('İmzalı PDF otomatik değiştirilemez.')
        for index, page in enumerate(before):
            checkpoint()
            target = after[index]
            old_state = page_state(page)
            method = 'preserved'
            if not page.get_text().strip():
                data = _recognize(page, conf, work)
                writer = PdfWriter()
                writer.clone_document_from_reader(PdfReader(io.BytesIO(data)))
                writer.remove_images()
                stream = io.BytesIO()
                writer.write(stream)
                writer.close()
                with fitz.open(stream=stream.getvalue(),filetype='pdf') as layer:
                    if layer[0].get_text().strip():
                        rotation = target.rotation
                        target.set_rotation(0)
                        target.show_pdf_page(target.rect,layer,0,rotate=rotation)
                        target.set_rotation(rotation)
                        method = 'ocr_added'
                    else:
                        method = 'ocr_empty'
            new_state = page_state(target)
            for key in old_state:
                if key not in ('text','words') and old_state[key] != new_state[key]:
                    raise ValueError(f'OCR sırasında sayfa öğeleri değişti: {index+1}, {key}')
            if method == 'preserved' and old_state != new_state:
                raise ValueError('Mevcut OCR metni değişti.')
            if image_state(page,True) != image_state(target,True):
                raise ValueError('OCR sırasında kaynak görüntü değişti.')
            if page.get_pixmap(dpi=120,alpha=False).samples != target.get_pixmap(dpi=120,alpha=False).samples:
                raise ValueError('OCR metni görünür sayfayı değiştirdi.')
            rows.append({'page':index+1,'method':method,'characters':len(target.get_text().strip()),'render_identical':True})
            notify({'stage':'Eksik OCR tamamlanıyor','page':index+1,'total':len(before)})
            fitz.TOOLS.store_shrink(100)
        if document_state(before) != document_state(after):
            raise ValueError('OCR sırasında yer imleri, ekler veya belge bilgileri değişti.')
        after.save(native_path(destination),garbage=4,deflate=True,use_objstms=1,encryption=fitz.PDF_ENCRYPT_KEEP)
    return rows


def run(batch, conf=None, notify=lambda e: None, cancelled=lambda: False):
    conf = conf or {}
    def checkpoint():
        if cancelled():
            raise CompressionCancelled('Duraklatıldı; tamamlanan PDF dosyaları korundu.')
    with exclusive_queue(batch), _connect(batch) as db:
        settings = _metadata(db)
        ocr_keys = ('language','paddle_language','ocr_engine','ocr_device','paddle_version','tess_quality','ocr_max_edge')
        settings['ocr_settings'] = {**settings.get('ocr_settings',{}), **{k:conf[k] for k in ocr_keys if k in conf}}
        db.execute('UPDATE meta SET value=? WHERE key=?',(json.dumps(settings,ensure_ascii=False),'settings'))
        db.commit()
        conf = {**conf, **settings['ocr_settings']}
        total = db.execute('SELECT count(*) FROM jobs').fetchone()[0]
        pending = [row[0] for row in db.execute("SELECT id FROM jobs WHERE json_extract(body,'$.status') NOT IN ('complete','replaced') ORDER BY id")]
        for identifier in pending:
            if cancelled():return inspect(batch)
            job = json.loads(db.execute('SELECT body FROM jobs WHERE id=?',(identifier,)).fetchone()[0])
            source, destination = Path(native_path(job['source'],force=True)), Path(native_path(job['destination'],force=True))
            if job.get('status') in ('verified','error') and job.get('compression') and job.get('output_sha256') and destination.exists():
                if file_digest(destination) == job.get('output_sha256') and file_digest(source) == job.get('source_sha256'):
                    job.update(status='complete',completed=time.time())
                    _write_job(db,job)
                    continue
                raise ValueError('Kesilen işlemin kaynağı veya sonucu değişmiş; dosyalar korundu.')
            job.pop('error',None)
            job['status'] = 'running'
            _write_job(db,job)
            def progress(event):
                notify({'job':job['id'],'source':job['source'],'file_index':job['id'],'files':total,**event})
            try:
                original_hash = file_digest(source)
                job['source_sha256'] = original_hash
                destination.parent.mkdir(parents=True,exist_ok=True)
                with tempfile.TemporaryDirectory(prefix='.stou-batch-',dir=destination.parent) as temporary:
                    work = Path(temporary)
                    with fitz.open(native_path(source)) as doc:
                        if doc.needs_pass and not doc.authenticate(conf.get('password','')):
                            raise ValueError('PDF şifresi gerekli veya yanlış.')
                        missing = [p.number for p in doc if not p.get_text().strip()]
                    input_pdf = source
                    ocr = []
                    if missing:
                        input_pdf = work/'ocr.pdf'
                        ocr = add_missing_ocr(source,input_pdf,conf,progress,checkpoint)
                    options = {'recompress_flate':True} if settings['profile']=='lossless' else {}
                    if conf.get('password'):
                        options['password'] = conf['password']
                    prepared = work/'verified.pdf'
                    report = compress(native_path(input_pdf),native_path(prepared),settings['profile'],progress,options=options,cancelled=cancelled)
                    checkpoint()
                    if file_digest(source) != original_hash:
                        raise ValueError('Kaynak PDF işlem sırasında değişti; çıktı yayımlanmadı.')
                    job.update(status='verified',output_sha256=report['output_sha256'],
                               before_bytes=source.stat().st_size,after_bytes=prepared.stat().st_size,
                               saved_percent=(1-prepared.stat().st_size/source.stat().st_size)*100,
                               ocr_pages=sum(r['method']=='ocr_added' for r in ocr),
                               ocr_empty_pages=[r['page'] for r in ocr if r['method']=='ocr_empty'],
                               ocr_verification=ocr,compression=report)
                    job['compression']['pdf']=str(destination)
                    _write_job(db,job)
                    from .pdf_compression import publish
                    publish(prepared,destination)
                job.update(status='complete',completed=time.time())
                _write_job(db,job)
                progress({'stage':'PDF hazır','status':'complete','result':{k:job[k] for k in ('id','destination','before_bytes','after_bytes','saved_percent','ocr_pages')}})
            except CompressionCancelled:
                job['status']='waiting'
                _write_job(db,job)
                return inspect(batch)
            except Exception as exc:
                job.update(status='error',error=str(exc))
                _write_job(db,job)
                progress({'stage':'Dosya işlenemedi','status':'error','error':str(exc)})
        return inspect(batch)


def replace_sources(batch, identifiers, notify=lambda e: None):
    """Called only by the explicit replace action after the user reviews results."""
    identifiers = set(map(int,identifiers))
    if not identifiers:
        raise ValueError('Yerine geçirilecek tamamlanmış dosyaları seçin.')
    with exclusive_queue(batch), _connect(batch) as db:
        chosen=[]
        for identifier in sorted(identifiers):
            row=db.execute('SELECT body FROM jobs WHERE id=?',(identifier,)).fetchone()
            if row:chosen.append(json.loads(row[0]))
        if len(chosen) != len(identifiers):
            raise ValueError('Seçilen dosya bu işte bulunamadı.')
        # Check the complete selection before changing the first source.
        for job in chosen:
            if job['status'] not in ('complete','replaced'):
                raise ValueError('Yalnız tamamlanan sonuçlar ana dosyanın yerine geçirilebilir.')
            source, destination = Path(native_path(job['source'],force=True)), Path(native_path(job['destination'],force=True))
            source_hash = file_digest(source)
            if source_hash not in (job['source_sha256'],job['output_sha256']):
                raise ValueError('Kaynak sonradan değişmiş; üzerine yazılmadı: '+str(source))
            if job['status']!='replaced' and file_digest(destination)!=job['output_sha256']:
                raise ValueError('Sıkıştırılmış sonuç değişmiş; kaynak korundu: '+str(destination))
        for job in chosen:
            if job['status']=='replaced':
                source,destination=Path(native_path(job['source'],force=True)),Path(native_path(job['destination'],force=True))
                if source.resolve()!=destination.resolve() and destination.exists() and file_digest(destination)==job['output_sha256']:
                    destination.unlink()
                continue
            source, destination = Path(native_path(job['source'],force=True)), Path(native_path(job['destination'],force=True))
            if file_digest(source) != job['output_sha256']:
                staged = source.with_name('.replace-'+uuid.uuid4().hex+'.tmp')
                try:
                    with destination.open('rb') as src, staged.open('xb') as out:
                        shutil.copyfileobj(src,out,length=4*1024**2)
                        out.flush();os.fsync(out.fileno())
                    if file_digest(staged)!=job['output_sha256'] or file_digest(source)!=job['source_sha256']:
                        raise ValueError('Yerine geçirme doğrulanamadı; kaynak korundu.')
                    os.replace(staged,source)
                finally:
                    staged.unlink(missing_ok=True)
            if file_digest(source)!=job['output_sha256']:
                raise ValueError('Ana dosya doğrulanamadı; sıkıştırılmış kopya korundu.')
            job.update(status='replaced',replaced=True,replaced_at=time.time())
            _write_job(db,job)
            if destination.resolve()!=source.resolve() and destination.exists() and file_digest(destination)==job['output_sha256']:
                destination.unlink()
            notify({'job':job['id'],'stage':'Ana dosyanın yerine geçirildi','source':str(source)})
    return inspect(batch)
