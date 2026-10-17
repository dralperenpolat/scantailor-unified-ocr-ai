"""Native desktop JSON service. Long work stays outside Qt's event thread."""
from __future__ import annotations
import json
import os
from pathlib import Path
import sys
import uuid
from .runtime_layout import data_root


def dispatch(request):
    action=request['action'];root=request.get('root');queue=request.get('queue')
    from . import settings,storage
    if action=='settings':return {'settings':settings.load(root),'profiles':settings.book_profiles(root)}
    if action=='settings-save':return {'settings':settings.save(request['settings'],root)}
    if action=='profile-save':return settings.save_profile(request['name'],request['settings'],root)
    if action=='storage':return storage.inventory(root,request.get('output'))
    if action=='cleanup':return storage.apply_cleanup(request['plan'],root)
    if action=='estimate':
        from .project import IMAGE_TYPES
        return storage.estimate(sorted(p for p in Path(request['folder']).iterdir() if p.suffix.lower() in IMAGE_TYPES))
    if action=='pages':
        from .queue_store import read_pages
        return read_pages(queue,request['job'],int(request.get('offset',0)),int(request.get('limit',100)),bool(request.get('problems')))
    if action=='review':
        from .queue_store import review_page
        return review_page(queue,request['job'],request['page'],request.get('sha',''),request.get('reviewed',True),request.get('note',''))
    if action=='pilot':
        import shutil
        from .native_worker import source_paths
        from .project import IMAGE_TYPES,natural_key
        from .queue_store import submit
        job=request['job'];count=max(1,min(10,int(request.get('count',3))))
        files=source_paths(job.get('project') or job['source_project']) if job.get('project') or job.get('source_project') else sorted((p for p in Path(job['folder']).iterdir() if p.suffix.lower() in IMAGE_TYPES),key=natural_key)
        identifier=uuid.uuid4().hex;folder=data_root(root)/'projects/pilot'/identifier;folder.mkdir(parents=True)
        for n,source in enumerate(files[:count],1):
            target=folder/(f'{n:04d}'+source.suffix)
            try:os.link(source,target)
            except OSError:shutil.copyfile(source,target)
        pilot={'id':identifier,'title':job['title']+' · Pilot','folder':str(folder),'split':job.get('split','auto'),
               'status':'Bekliyor','pilot_of':job['id'],'pilot_input_pages':min(len(files),count),'priority':10}
        return dispatch({'action':'enqueue','root':root,'queue':queue,'job':pilot})
    if action in ('enqueue','prioritize','remove'):
        from .queue_store import submit,QueueStore
        from .queue_lock import exclusive_queue
        values={k:v for k,v in request.items() if k not in ('action','queue','root')}
        identifier=submit(queue,action,**values)
        try:
            with exclusive_queue(queue):
                store=QueueStore(queue)
                try:
                    data=store.load(json.loads(Path(queue).read_text('utf8')))
                    _,errors=store.apply_commands(data);store.publish(data)
                    if errors:raise ValueError('; '.join(errors))
                finally:store.close()
        except RuntimeError as exc:
            if 'lock' not in str(exc).lower() and 'kuyruk' not in str(exc).lower():raise
        return {'command':identifier,'submitted':True}
    if action=='preview':
        import fitz
        path=Path(request['pdf']);target=data_root(root)/'resources/preview-cache'/(uuid.uuid4().hex+'.png')
        target.parent.mkdir(parents=True,exist_ok=True)
        with fitz.open(path) as doc:doc[int(request.get('page',1))-1].get_pixmap(dpi=120,alpha=False).save(target)
        storage.register(target,owner=str(path),root=root)
        return {'preview':str(target)}
    if action=='pdf-probe':
        from .smart_pdf import probe
        target=data_root(root)/'resources/preview-cache'/(uuid.uuid4().hex+'.png')
        result=probe(request['source'],request.get('pages',''),request.get('password',''),target)
        storage.register(target,owner=request['source'],root=root);return result
    if action=='pdf-smart':
        from .smart_pdf import process
        return process(request['source'],request['destination'],{**settings.load(root),**request,'root':root})
    if action=='compression-preview':
        from .pdf_profiles import preview
        return preview(request['source'],request['destination'],request.get('pages','1-3'))
    if action=='compress':
        from .pdf_profiles import compress
        return compress(request['source'],request['destination'],request.get('profile','balanced'))
    if action=='pdfa':
        from .archive_pdf import convert_pdfa
        return convert_pdfa(request['source'],request['destination'],{**settings.load(root),'root':root})
    if action=='text-export':
        from .quality import export_text
        return export_text(request['source'],request['destination'])
    if action=='corpus-create':
        from .benchmark import create_corpus
        return create_corpus(queue,request['destination'],int(request.get('count',250)))
    if action=='corpus-open':
        path=Path(request['manifest']);value=json.loads(path.read_text('utf8'))
        for row in value['pages']:
            row['image']=str(path.parent/row['image'])
            reference=path.parent/row['reference'];row['reference_text']=reference.read_text('utf8') if reference.is_file() else ''
        return value
    if action=='corpus-annotate':
        from .benchmark import annotate
        return annotate(request['manifest'],request['id'],request['reference'],request.get('tags',[]),request.get('regions',[]))
    if action=='corpus-evaluate':
        from .benchmark import evaluate
        return evaluate(request['manifest'])
    if action=='optional-probe':
        from .optional_engines import probe
        return probe({**settings.load(root),**request})
    if action=='structure':
        from .optional_engines import structure
        return structure(request['source'],request['destination'],{**settings.load(root),**request})
    raise ValueError('Unknown workspace action: '+action)


def main():
    try:
        request=json.loads(sys.stdin.readline())
        result=dispatch(request)
        print('STOUWORK '+json.dumps({'ok':True,'result':result},ensure_ascii=False),flush=True)
    except Exception as exc:
        print('STOUWORK '+json.dumps({'ok':False,'error':str(exc)},ensure_ascii=False),flush=True)
        return 1
    return 0


if __name__=='__main__':raise SystemExit(main())
