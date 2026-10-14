"""Content-addressed per-page export; originals are never modified.

Cache entries are checked against the current inputs and their own PDF digest.
Changing one page only invalidates that page. A cached result contains verified
display pixels and an invisible OCR layer; caching does not certify OCR accuracy.
"""
from pathlib import Path
import hashlib, importlib.metadata, json, time, os, uuid
from pypdf import PdfReader, PdfWriter
from .pdf_export import export
from . import __version__

def sha_file(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def assemble(project,output,cache_dir,notify=print,cancel=None):
    project=Path(project).resolve();output=Path(output).resolve();cache_dir=Path(cache_dir).resolve()
    if output.exists():raise FileExistsError(output)
    conf=json.loads(project.read_text(encoding='utf8'))
    if not conf.get('pages'):raise ValueError('Project has no pages')
    conf=dict(conf)
    for key in ['tessdata','jbig2']:
        if conf.get(key):conf[key]=str((project.parent/conf[key]).resolve())
    language=conf.get('language','deu')
    model_hashes={lang:sha_file(Path(conf['tessdata'])/(lang+'.traineddata')) for lang in language.split('+')}
    versions={k:importlib.metadata.version(k) for k in ['PyMuPDF','pypdf','Pillow','img2pdf']}
    versions['unified']=__version__
    versions['export_code']=sha_file(Path(__file__).with_name('pdf_export.py'))
    if conf.get('jbig2'):versions['jbig2_binary']=sha_file(conf['jbig2'])
    cache_dir.mkdir(parents=True,exist_ok=True)
    writer=PdfWriter();results=[];started=time.monotonic()
    for index,entry in enumerate(conf['pages'],1):
        if cancel is not None and cancel.is_set():
            from .engine import Cancelled
            raise Cancelled('PDF oluşturma durduruldu.')
        page=dict(entry)
        for key in ['display_image','ocr_image']:
            if page.get(key):page[key]=str((project.parent/page[key]).resolve())
        image_hashes={key:sha_file(page[key]) for key in ['display_image','ocr_image'] if page.get(key)}
        one={k:v for k,v in conf.items() if k not in ['pages','notes','title']};one['pages']=[page]
        cache_key=hashlib.sha256(json.dumps({'project':one,'inputs':image_hashes,'models':model_hashes,'versions':versions},
                                    sort_keys=True,ensure_ascii=False).encode('utf8')).hexdigest()
        directory=cache_dir/cache_key;directory.mkdir(exist_ok=True)
        record_path=directory/'record.json';pdf=directory/'page.pdf'
        hit=False
        if record_path.exists() and pdf.exists():
            try:
                record=json.loads(record_path.read_text(encoding='utf8'))
                hit=record.get('sha256')==sha_file(pdf)
            except (ValueError,OSError):
                hit=False
        if not hit:
            # Never replace a suspect cache file in place; preserve it for diagnosis.
            if pdf.exists():
                directory=cache_dir/(cache_key+'_'+str(time.time_ns()));directory.mkdir()
                pdf=directory/'page.pdf';record_path=directory/'record.json'
            manifest=directory/'project.json';manifest.write_text(json.dumps(one,ensure_ascii=False,indent=2),encoding='utf8')
            check=export(manifest,pdf,notify=lambda msg:None)
            record={'sha256':sha_file(pdf),'verification':check,'key':cache_key}
            record_path.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf8')
        writer.add_page(PdfReader(pdf).pages[0])
        results.append({'page':index,'id':entry.get('id',str(index)),'cache_hit':hit,'key':cache_key,'pdf_sha256':record['sha256']})
        notify(f'{index}/{len(conf["pages"])} '+('önbellekten' if hit else 'işlendi')+' — '+entry.get('id',str(index)))
    output.parent.mkdir(parents=True,exist_ok=True)
    if cancel is not None and cancel.is_set():
        from .engine import Cancelled
        raise Cancelled('PDF oluşturma durduruldu.')
    temp=output.with_name('.'+output.name+'.'+uuid.uuid4().hex+'.tmp')
    with temp.open('xb') as f:writer.write(f)
    if len(PdfReader(temp).pages)!=len(results):raise ValueError('Page count mismatch after assembly')
    # Same-directory hard link atomically publishes without replacing any file.
    # Windows NTFS and normal local Linux filesystems support this operation.
    os.link(temp,output)
    temp.unlink()
    return {'output':str(output),'bytes':output.stat().st_size,'pages':len(results),
            'seconds':round(time.monotonic()-started,3),'cache_hits':sum(p['cache_hit'] for p in results),
            'checks':results,'versions':versions}
