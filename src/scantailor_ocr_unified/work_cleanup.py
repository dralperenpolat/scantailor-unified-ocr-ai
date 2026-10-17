"""Reclaim only reproducible files of a successfully verified working book."""
from pathlib import Path
import json
import hashlib

def reclaim(folder,output_root,job):
    folder=Path(folder).resolve();root=Path(output_root).resolve()
    if folder==root or not folder.is_relative_to(root):raise ValueError('Çalışma yolu çıktı kökü dışında.')
    if not job.get('verification',{}).get('all_pages_render_identical'):return {'bytes':0,'files':0}
    final=Path(job['pdf']).resolve()
    if final.parent!=folder or not final.is_file():raise ValueError('Doğrulanmış son PDF bulunamadı.')
    with final.open('rb') as stream:
        if hashlib.file_digest(stream,'sha256').hexdigest()!=job.get('pdf_hash'):raise ValueError('PDF değişmiş; ara dosyalar korundu.')
    targets=[];out=(folder/'out').resolve()
    if not out.is_relative_to(folder):raise ValueError('Çıktı klasörü çalışma kitabı dışında.')
    manifest=Path(job['project']+'.outputs.json')
    for name in json.loads(manifest.read_text('utf8'))['pages']:
        for p in (Path(name),Path(str(name)+'.ocr.tif')):
            p=p.resolve()
            # A verified native cache may refer to an older, read-only run.
            # Such files are always retained; only this book's own out is ours.
            if p.parent==out:targets.append(p)
    cache=(folder/'.ocr-cache').resolve()
    if cache==folder or not cache.is_relative_to(folder):raise ValueError('Sayfa önbelleği çalışma kitabı dışında.')
    for page in job.get('pages',[]):
        parent=Path(page['page_pdf']).resolve().parent
        if parent.parent!=cache:raise ValueError('Sayfa önbelleği başka bir klasörde.')
        targets.extend(parent/name for name in ('display.tif','display-final.tif','ocr.png'))
    total=0;count=0
    for path in targets:
        if path.is_file() and not path.is_symlink():
            total+=path.stat().st_size;path.unlink();count+=1
    return {'bytes':total,'files':count,'kept':'Kaynak fotoğraflar, proje, PDF, sayfa PDF’leri, OCR önbelleği ve önizlemeler.'}
