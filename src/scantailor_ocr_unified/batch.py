"""Persistent, serial multi-book queue. Sources and earlier PDFs are preserved."""
from pathlib import Path
from datetime import datetime
import copy
import hashlib
import json
import os
import re
import time
import uuid
from . import engine
from .project import APP_ROOT,IMAGE_TYPES,natural_key,new_project,open_project,save_project,page_entry,read_rgb,file_hash

IGNORED={'out','output','outputs','.unified-cache','imported_pdf','node_modules','.git','.venv'}
STATUS={'pending':'Bekliyor','preparing':'Hazırlanıyor','ready':'Önizleme hazır','review':'Kontrol gerekli',
        'exporting':'PDF oluşturuluyor','done':'Tamamlandı','done_review':'Tamamlandı · kontrol','paused':'Durduruldu','error':'Hata'}

def stamp():return datetime.now().isoformat(timespec='seconds')
def new_queue(path=None,output_root=None):
    return {'queue_schema':1,'queue_file':str(Path(path or APP_ROOT/'projects/Toplu_Kitaplar/kuyruk.json').resolve()),
            'output_root':str(Path(output_root or APP_ROOT/'output/Kitaplar').resolve()),'jobs':[],'updated':stamp(),'run_status':'idle'}
def save_queue(data):
    path=Path(data['queue_file']);path.parent.mkdir(parents=True,exist_ok=True);data['updated']=stamp()
    temp=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    temp.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8');os.replace(temp,path)
def load_queue(path):
    path=Path(path).resolve();data=json.loads(path.read_text(encoding='utf8'))
    if data.get('queue_schema')!=1 or not isinstance(data.get('jobs'),list):raise ValueError('Bu dosya bir çoklu kitap kuyruğu değil.')
    data['queue_file']=str(path)
    for job in data['jobs']:
        if job['status'] in ('preparing','exporting'):job['status']='paused';job['progress']='Önceki işlem yarıda kalmış; devam edilebilir.'
    data['run_status']='idle';return data
def source_files(folder,include_pdf=False):
    types=IMAGE_TYPES|({'.pdf'} if include_pdf else set())
    return sorted((p for p in Path(folder).iterdir() if p.is_file() and p.suffix.lower() in types),key=natural_key)
def discover_books(parent,recursive=True,include_pdf=False,exclude=()):
    parent=Path(parent).resolve();found=[];excluded=[Path(p).resolve() for p in exclude]
    for directory,dirs,files in os.walk(parent,followlinks=False):
        here=Path(directory)
        dirs[:]=[d for d in dirs if d.casefold() not in IGNORED and not d.startswith('.')
                 and not (here/d).is_symlink() and not (getattr((here/d),'is_junction',lambda:False)())
                 and not any((here/d).resolve().is_relative_to(p) for p in excluded)]
        if here!=parent and source_files(here,include_pdf):found.append(here)
        if not recursive and here!=parent:dirs[:]=[]
    return sorted(found,key=natural_key)
def add_folder(data,folder,options):
    folder=Path(folder).resolve()
    if any(j.get('folder','').casefold()==str(folder).casefold() for j in data['jobs']):return False
    files=source_files(folder,options.get('include_pdf',False))
    if not files:return False
    data['jobs'].append({'id':uuid.uuid4().hex[:12],'title':folder.name,'folder':str(folder),
        'files':[str(p) for p in files],'options':copy.deepcopy(options),'status':'pending','pages':0,'warnings':0,'error':'','progress':''})
    return True
def add_project(data,path):
    path=Path(path).resolve()
    if any(j.get('from_project','').casefold()==str(path).casefold() for j in data['jobs']):return False
    project=open_project(path)
    data['jobs'].append({'id':uuid.uuid4().hex[:12],'title':project['title'],'from_project':str(path),
        'options':{},'status':'pending','pages':len(project['pages']),'warnings':0,'error':'','progress':''})
    return True
def safe_name(name):
    text=re.sub(r'[<>:"/\\|?*\x00-\x1f]','_',name).strip(' .')[:75] or 'Kitap'
    if text.split('.')[0].upper() in {'CON','PRN','AUX','NUL',*(f'COM{i}' for i in range(1,10)),*(f'LPT{i}' for i in range(1,10))}:text='Kitap_'+text
    return text
def prepare_project(data,job,cancel,notify):
    if not job.get('project_file'):
        folder=Path(data['output_root'])/(safe_name(job['title'])+'_'+job['id'][:6])
        job['project_file']=str(folder/'kitap.json');save_queue(data)
    path=Path(job['project_file'])
    if path.exists():return open_project(path)
    if job.get('from_project'):
        project=open_project(job['from_project']);project['project_file']=str(path)
    else:
        opt=job['options'];project=new_project(path,job['title'])
        project.update({k:opt[k] for k in ('language','output_dpi','output_mode','normalize_text') if k in opt})
        for i,filename in enumerate(job['files']):
            engine.check_cancel(cancel);source=Path(filename);notify(f'İçe alınıyor {i+1}/{len(job["files"])}')
            if source.suffix.lower()=='.pdf':
                import fitz
                folder=path.parent/'imported_pdf'/file_hash(source)[:16];folder.mkdir(parents=True,exist_ok=True)
                with fitz.open(source) as pdf:
                    for n,p in enumerate(pdf):
                        engine.check_cancel(cancel);pic=folder/f'{n+1:05d}.png'
                        if not pic.exists():
                            zoom=min(200/72,6000/max(p.rect.width,p.rect.height));p.get_pixmap(matrix=fitz.Matrix(zoom,zoom),alpha=False).save(pic)
                        entry=page_entry(pic,f'{source.stem} · {n+1}',[[0,0],[1,0],[1,1],[0,1]])
                        entry.update(method='none',pdf_source=str(source),pdf_page=n);project['pages'].append(entry)
            else:
                image=read_rgb(source);sha=file_hash(source)
                for side in (('left','right') if opt.get('split',True) else ('whole',)):
                    entry=page_entry(source,source.stem+({'left':' · sol','right':' · sağ','whole':''}[side]),engine.auto_quad(image,side),sha)
                    entry.update(method=opt.get('method','auto'),threshold=opt.get('threshold','otsu'),corners_estimated=True)
                    project['pages'].append(entry)
    if not project['pages']:raise ValueError('Kitapta işlenecek sayfa yok.')
    save_project(project);return project
def signature(project):
    book={k:project.get(k) for k in ('language','output_dpi','output_mode','normalize_text','target_body_px')}
    pages=[]
    for page in project['pages']:
        row={k:page.get(k) for k in ('quad','method','rotation','content','white_zones','threshold','threshold_adjust','normalize_light','body_override','allow_empty_ocr')}
        row['source']=file_hash(page['source']);pages.append(row)
    return hashlib.sha256(json.dumps([engine.ENGINE_VERSION,book,pages],sort_keys=True).encode()).hexdigest()
def complete_and_unchanged(job):
    try:
        return (job['status'] in ('done','done_review') and file_hash(job['output_file'])==job['pdf_sha256']
                and signature(open_project(job['project_file']))==job['input_signature'])
    except (OSError,KeyError,ValueError):return False
def next_pdf(job):
    folder=Path(job['project_file']).parent;stem=safe_name(job['title']);path=folder/(stem+'.pdf');version=2
    while path.exists():path=folder/f'{stem}_v{version}.pdf';version+=1
    return path

def run_queue(data,mode='export',cancel=None,notify=lambda *args:None,only_ids=None):
    """Resume cache-backed pages; keep other books moving after one book fails."""
    if mode not in ('prepare','export'):raise ValueError('Bilinmeyen toplu işlem.')
    data['run_status']='running';save_queue(data)
    for job in data['jobs']:
        if only_ids is not None and job['id'] not in only_ids:continue
        if cancel is not None and cancel.is_set():data['run_status']='paused';break
        def progress(message):
            job['progress']=message;notify(copy.deepcopy(job))
        if complete_and_unchanged(job):progress('Tamamlanmış PDF korundu; atlandı.');continue
        started=time.monotonic();job.update(status='preparing',error='');save_queue(data);progress('Kitap hazırlanıyor')
        try:
            project=prepare_project(data,job,cancel,progress);job['pages']=len(project['pages'])
            pairs=[]
            for i,page in enumerate(project['pages']):
                engine.check_cancel(cancel);progress(f'Geometri {i+1}/{job["pages"]}')
                result=engine.geometry(project,page,cancel);pairs.append((page,result))
                page['analysis']={k:result[k] for k in ('size','content','body_px')}
                page['warnings']=list(result['warnings']);page['status']='Kontrol' if page['warnings'] else 'Hazır'
                save_project(project)
            job['warnings']=sum(bool(p['warnings']) for p in project['pages'])
            if mode=='prepare':
                target=engine.fit_target(project,pairs)
                for i,(page,result) in enumerate(pairs):
                    engine.check_cancel(cancel);progress(f'Önizleme {i+1}/{job["pages"]}');engine.render_page(project,page,result,target,cancel)
                job['status']='review' if job['warnings'] else 'ready';progress('Proje hazır; düzenleyicide açılabilir.')
            else:
                job.update(status='exporting',input_signature=signature(project),output_file=str(next_pdf(job)));save_queue(data)
                def exported(stage,i,total,label):progress(f'{stage} · {label}' if stage=='OCR / PDF' else f'{stage} {i+1}/{total}')
                result=engine.export_book(project,job['output_file'],cancel,exported)
                if signature(project)!=job['input_signature']:raise ValueError('İşlem sırasında kaynak değişti; çıktı yeniden kontrol edilmeli.')
                job.update(status='done_review' if job['warnings'] else 'done',bytes=result['bytes'],pdf_sha256=file_hash(job['output_file']))
                progress(f'PDF hazır · {result["bytes"]/1024:.0f} KB')
        except engine.Cancelled:
            job.update(status='paused',progress='Durduruldu; önbellekten devam edilebilir.');data['run_status']='paused'
            save_queue(data);notify(copy.deepcopy(job));break
        except Exception as exc:
            job.update(status='error',error=str(exc),progress=str(exc));notify(copy.deepcopy(job))
        finally:
            job['seconds']=round(time.monotonic()-started,2);save_queue(data)
    if data['run_status']=='running':data['run_status']='finished'
    save_queue(data);return data
