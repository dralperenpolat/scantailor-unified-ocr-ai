"""Disk budgeting and explicitly registered, reproducible cache files.

Unregistered files (including historical/manual reviews) are always protected.
Cleanup plans are bound to file identities, not a recursive folder deletion.
"""
from __future__ import annotations
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import tempfile
import time
from .runtime_layout import data_root

GIB = 1024**3

class StoragePressure(OSError):
    def __init__(self, path, free, required):
        self.path, self.free, self.required = str(path), free, required
        super().__init__(f'Disk alanı yetersiz: {path} · {free/GIB:.2f} GiB boş, {required/GIB:.2f} GiB gerekli. Yer açıp devam edin.')

def existing_parent(path):
    path = Path(path).resolve()
    while not path.exists(): path = path.parent
    return path

def ensure_space(paths, needed=0, reserve_gib=2):
    seen = set()
    for raw in paths:
        path = existing_parent(raw)
        device = path.stat().st_dev
        if device in seen: continue
        seen.add(device)
        free = shutil.disk_usage(path).free
        required = int(needed + float(reserve_gib)*GIB)
        if free < required: raise StoragePressure(path, free, required)

def temp_root(conf=None):
    from .settings import load
    conf = conf or {}; prefs = load(conf.get('root'))
    selected = conf.get('temp_root') or os.environ.get('STOU_TEMP_DIR') or prefs.get('temp_root')
    root = Path(selected) if selected else Path(tempfile.gettempdir())/'stou-work'
    root = root.resolve(); root.mkdir(parents=True, exist_ok=True)
    # A short ASCII path avoids Paddle's native Windows path limitations.
    if os.name == 'nt' and (not str(root).isascii() or len(str(root)) > 120):
        raise ValueError('Geçici klasör için kısa, Türkçe karakter içermeyen bir yol seçin (ör. I:/STOU_Work).')
    return root

@contextmanager
def workspace(conf=None, needed=0):
    from .settings import load
    conf = conf or {}; root = temp_root(conf)
    reserve = conf.get('reserve_gib', load(conf.get('root'))['reserve_gib'])
    ensure_space([root], needed, reserve)
    with tempfile.TemporaryDirectory(prefix='stou_pdf_', dir=root) as folder:
        folder = Path(folder).resolve()
        if folder.parent != root: raise ValueError('Invalid temporary workspace')
        (folder/'.stou-owner.json').write_text(json.dumps({'pid':os.getpid(),'created':time.time(),'kind':'transient'}))
        owner = conf.get('cache_owner')
        if owner:
            # Only a workflow-created project may own a transient directory.
            # Record the exact folder before work starts, including cancellation.
            owner = Path(owner).resolve()
            marker = owner/'.stou-cache-owner.json'
            if marker.is_file() and json.loads(marker.read_text('utf8')).get('project') == owner.as_posix():
                from .settings import atomic_json
                atomic_json(folder/'.stou-cache-owner.json', {'project':owner.as_posix(), 'role':'temporary'})
                manifest = owner/'.project-cache.json'
                record = json.loads(manifest.read_text('utf8')) if manifest.exists() else {}
                roots = record.setdefault('roots', [])
                entry = {'path':folder.as_posix(), 'role':'temporary'}
                if entry not in roots: roots.append(entry)
                atomic_json(manifest, record)
        yield folder

def estimate(images, sample=8):
    from PIL import Image
    images = list(images); sizes = []
    for path in images[:sample]:
        with Image.open(path) as im: sizes.append(im.width*im.height)
    pixels = sum(sizes)/len(sizes) if sizes else 0
    # Planning estimate, not a promise about compression or native scale.
    return {'pages':len(images),'sampled':len(sizes),'estimated_work_bytes':int(pixels*len(images)*5),
            'estimated_page_peak_bytes':int(pixels*24),'estimate':True}

@contextmanager
def _connect(root=None):
    path = data_root(root)/'storage-index.sqlite3'; path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path, timeout=10)
    db.execute('CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY, kind TEXT, bytes INTEGER, modified INTEGER, accessed REAL, pinned INTEGER, owner TEXT)')
    try:
        with db:yield db
    finally:db.close()

def register(path, kind='preview', *, owner='', pinned=False, root=None):
    path = Path(path).absolute()
    if path.is_symlink() or any(p.is_symlink() for p in path.parents) or not path.is_file(): return
    path = path.resolve()
    st = path.stat()
    with _connect(root) as db:
        db.execute('INSERT OR REPLACE INTO files VALUES(?,?,?,?,?,?,?)',
                   (str(path),kind,st.st_size,st.st_mtime_ns,time.time(),int(pinned),str(owner)))

def pin(path, value=True, root=None):
    with _connect(root) as db: db.execute('UPDATE files SET pinned=? WHERE path=?',(int(value),str(Path(path).resolve())))

def cleanup_plan(root=None, quota_gib=None):
    from .settings import load
    quota = float(load(root)['cache_quota_gib'] if quota_gib is None else quota_gib)*GIB
    if quota < 0: raise ValueError('Invalid quota')
    with _connect(root) as db:
        rows = db.execute('SELECT path,kind,bytes,modified,pinned,owner FROM files ORDER BY accessed').fetchall()
    valid=[]
    for path,kind,size,modified,pinned,owner in rows:
        p=Path(path)
        if p.is_file() and not p.is_symlink():
            st=p.stat()
            if (st.st_size,st.st_mtime_ns)==(size,modified):valid.append((path,kind,size,modified,pinned,owner))
    total=sum(r[2] for r in valid); wanted=max(0,total-quota); selected=[]; freed=0
    for path,kind,size,modified,pinned,owner in valid:
        if freed>=wanted:break
        # Page PDFs, OCR receipts, sources and manual edits are never candidates.
        if pinned or kind not in ('preview','reproducible_analysis'):continue
        if owner and not Path(owner).is_file():continue
        selected.append({'path':path,'bytes':size,'modified':modified,'kind':kind,'owner':owner});freed+=size
    token=hashlib.sha256(json.dumps(selected,sort_keys=True).encode()).hexdigest()
    return {'registered_bytes':total,'quota_bytes':int(quota),'reclaimable_bytes':freed,'files':selected,'token':token}

def apply_cleanup(plan, root=None):
    current=cleanup_plan(root,plan['quota_bytes']/GIB)
    if current['token']!=plan['token']:raise ValueError('Dosyalar değişti; temizlik planını yenileyin.')
    freed=count=0
    with _connect(root) as db:
        for row in current['files']:
            path=Path(row['path'])
            try:st=path.lstat()
            except FileNotFoundError:continue
            if any(p.is_symlink() for p in path.parents):continue
            if path.is_symlink() or (st.st_size,st.st_mtime_ns)!=(row['bytes'],row['modified']):continue
            try:path.unlink()
            except PermissionError:continue
            freed+=st.st_size;count+=1
            db.execute('DELETE FROM files WHERE path=?',(str(path),))
    return {'bytes':freed,'files':count}

def inventory(root=None, output=None):
    from .runtime_layout import app_root
    base=data_root(root)
    candidates={'models':base/'resources/paddle-models','runtimes':base/'resources/runtimes',
                'reviews_protected':base/'resources/reviews','projects_protected':base/'projects'}
    if output:candidates['book_outputs_protected']=Path(output)
    app=Path(root) if root else app_root()
    candidates['application_runtime']=app/'runtime'
    if app.resolve()!=base.resolve():
        candidates['bundled_models']=app/'resources/paddle-models'
        candidates['bundled_languages']=app/'resources/tessdata_best'
    candidates['fast_languages']=base/'resources/tessdata_fast'
    result={}
    for kind,folder in candidates.items():
        total=count=0
        if folder.exists():
            for directory,dirs,files in os.walk(folder,followlinks=False):
                dirs[:]=[d for d in dirs if not (Path(directory)/d).is_symlink()]
                for name in files:
                    p=Path(directory)/name
                    try:
                        if not p.is_symlink():total+=p.stat().st_size;count+=1
                    except OSError:pass
        result[kind]={'path':str(folder),'bytes':total,'files':count}
    return {'categories':result,'cleanup':cleanup_plan(root),'temporary_root':str(temp_root({'root':root})),
            'free_bytes':shutil.disk_usage(existing_parent(output or base)).free}
