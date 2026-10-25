"""Native Experimental queue and verified PDF bridge. JSON lines are UI events.

Existing projects, photos and PDFs are never overwritten. Only snapshots and
new per-book working directories are mutated. Pause is cooperative per page.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import queue as thread_queue
import threading
import uuid
import xml.etree.ElementTree as ET
from PIL import Image
from .runtime_layout import resource_path
from .project import natural_key, file_hash, IMAGE_TYPES

class Cancelled(Exception): pass
class RevisionPending(Exception): pass

class NativeTimeout(TimeoutError):
    def __init__(self,message,event):
        super().__init__(message);self.event=dict(event)


class NativeCrash(RuntimeError):
    def __init__(self, code, event, detail=''):
        self.code=code;self.event=dict(event)
        super().__init__(f'Experimental motoru hata {code}: {detail}')


def use_rotation_after_timeout(project,event):
    """Change only the timed-out automatic warp page in a working copy."""
    if event.get('stage')!='Geometric Distortions':return None
    project=Path(project);tree=ET.parse(project);root=tree.getroot()
    pages=root.findall('./pages/page');index=event.get('page')
    if not isinstance(index,int) or not 0<=index<len(pages):return None
    page=pages[index]
    images={e.get('id'):e.get('fileId') for e in root.findall('./images/image')}
    files={e.get('id'):e.get('name') for e in root.findall('./files/file')}
    if files.get(images.get(page.get('imageId')))!=event.get('detail'):return None
    deskew=root.find('./filters/deskew')
    if deskew is None:return None
    node=deskew.find(f"page[@id='{page.get('id')}']")
    params=node.find('params') if node is not None else None
    if params is not None:
        kind=params.get('distortionType')
        active=params.find({'warp':'warp','perspective':'perspective'}.get(kind,'rotation'))
        if kind not in ('warp','perspective') or active is not None and active.get('mode')=='manual':return None
    backup=project.with_name(project.stem+f'.before-geometry-{index+1}-{time.time_ns()}'+project.suffix)
    shutil.copyfile(project,backup)
    if node is None:node=ET.SubElement(deskew,'page',{'id':page.get('id')})
    if params is None:params=ET.SubElement(node,'params')
    params.set('distortionType','rotation')
    params.set('unifiedWarning','Otomatik eğrilik süre sınırını aştı; bu sayfada eğiklik düzeltmesi kullanıldı. Kontrol edin.')
    rotation=params.find('rotation')
    if rotation is None:ET.SubElement(params,'rotation',{'mode':'auto'})
    temp=project.with_name('.geometry-'+uuid.uuid4().hex[:12]+'.tmp')
    tree.write(temp,encoding='utf-8',xml_declaration=True);os.replace(temp,project)
    return {'page':index+1,'source':event['detail'],'backup':str(backup),'reason':'Otomatik eğrilik zaman aşımı; yalnız bu sayfada eğiklik düzeltmesi.'}

def write_json(path, data, *, retry_seconds=5, compact=False):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temp=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    try:
        try:
            # Fast path for frequent page checkpoints. Under memory pressure,
            # stream small chunks instead of allocating another complete JSON.
            temp.write_text(json.dumps(data,ensure_ascii=False,indent=None if compact else 2),encoding='utf8')
        except MemoryError:
            with temp.open('w',encoding='utf8') as stream:
                json.dump(data,stream,ensure_ascii=False,indent=None if compact else 2)
        deadline=time.monotonic()+retry_seconds
        while True:
            try:
                os.replace(temp,path)
                break
            except PermissionError:
                # A Windows reader can briefly deny replacement. Keep the old
                # complete JSON visible and retry; never truncate it in place.
                if time.monotonic()>=deadline:raise
                time.sleep(.025)
    finally:
        temp.unlink(missing_ok=True)

def load_queue(path):
    """Recover a newer durable checkpoint if a Windows reader blocked publish."""
    path=Path(path);pending=path.with_name(path.name+'.pending.json')
    if pending.is_file() and (not path.exists() or pending.stat().st_mtime_ns>path.stat().st_mtime_ns):
        return json.loads(pending.read_text(encoding='utf8'))
    return json.loads(path.read_text(encoding='utf8'))


def save_queue(path,data):
    """Keep completed work durable without waiting for an open GUI handle."""
    path=Path(path);pending=path.with_name(path.name+'.pending.json')
    try:
        write_json(path,data,retry_seconds=.15,compact=True)
    except PermissionError:
        # This private sidecar is never opened by the GUI. The next save will
        # publish the latest state; a restarted worker reads it first as well.
        write_json(pending,data,compact=True)
        return False
    pending.unlink(missing_ok=True)
    return True

def safe_name(name):
    name=re.sub(r'[<>:"/\\|?*\x00-\x1f]','_',name).strip(' .')[:64] or 'Kitap'
    if name.split('.')[0].upper() in {'CON','PRN','AUX','NUL',*(f'COM{i}' for i in range(1,10)),*(f'LPT{i}' for i in range(1,10))}:name='Kitap_'+name
    return name

def next_path(directory,name,suffix):
    p=Path(directory)/(name+suffix);n=2
    while p.exists():p=Path(directory)/(f'{name}_v{n}'+suffix);n+=1
    return p

def source_paths(project_path):
    p=Path(project_path);root=ET.parse(p).getroot()
    dirs={e.attrib['id']:(p.parent/e.attrib['path']).resolve() for e in root.findall('./directories/directory')}
    return [(dirs[e.attrib['dirId']]/e.attrib['name']).resolve() for e in root.findall('./files/file')]

def clone_project(source,destination,output):
    source=Path(source);tree=ET.parse(source);root=tree.getroot()
    Path(output).mkdir(parents=True,exist_ok=True)
    root.set('outputDirectory',str(Path(output).resolve()).replace('\\','/'))
    for d in root.findall('./directories/directory'):
        d.set('path',str((source.parent/d.attrib['path']).resolve()).replace('\\','/'))
    # Cached output metadata refers to the original output dir; re-evaluate it.
    for page in root.findall('./filters/output/page'):
        for element in list(page):
            if element.tag in ('output-params','outputParams'):page.remove(element)
    Path(destination).parent.mkdir(parents=True,exist_ok=True)
    tree.write(destination,encoding='utf-8',xml_declaration=True)

def set_native_scale(project,scale):
    tree=ET.parse(project);output=tree.getroot().find('./filters/output')
    if output is not None:
        output.set('scalingFactor',str(float(scale)))
        # XML parsers require the canonical UTF-8 name. ElementTree's "utf8"
        # declaration breaks parsing as soon as a Turkish path is encountered.
        tree.write(project,encoding='utf-8',xml_declaration=True)

def fingerprint(project,settings):
    tree=ET.parse(project)
    # Output cache metadata changes as pages are processed; it is not a setting.
    root=tree.getroot()
    for e in root.iter():
        for child in list(e):
            if child.tag in ('output-params','outputParams','batch-checkpoint'):e.remove(child)
        e.attrib.pop('selected',None)
    data=[ET.tostring(root,encoding='unicode'),settings,[(str(p),file_hash(p)) for p in source_paths(project)]]
    return hashlib.sha256(json.dumps(data,sort_keys=True).encode()).hexdigest()

def geometry_warnings(project):
    root=ET.parse(project).getroot()
    decisions={e.attrib['id']:e.find('params').get('unifiedWarning','')
               for e in root.findall('./filters/deskew/page') if e.find('params') is not None}
    return {n:decisions.get(page.get('id'),'')
            for n,page in enumerate(root.findall('./pages/page'),1) if decisions.get(page.get('id'))}

def native_execution(root,cli,request):
    """Use a locally verified execution-only build without invalidating OCR."""
    cli=Path(cli);record=Path(root)/'local-native-performance.json'
    explicit=bool(request.get('native_variant'))
    variant=request.get('native_variant')
    if not variant and record.is_file():
        variant=json.loads(record.read_text(encoding='utf8'))
    if not variant:
        release=Path(root)/'release.json'
        if release.is_file():
            bundled=json.loads(release.read_text(encoding='utf8'))
            if bundled.get('native_cli_sha256')==file_hash(cli):
                return cli,max(1,min(4,int(request.get('threads',4)),int(request.get('native_output_workers',bundled.get('native_parallel_workers',1)))))
        return cli,1
    candidate=Path(root)/variant['path']
    if candidate.is_file() and variant.get('base_sha256')==file_hash(cli) and file_hash(candidate)==variant.get('sha256'):
        workers=max(1,min(4,int(request.get('threads',4)),int(request.get('native_output_workers',variant.get('workers',2)))))
        return candidate,workers
    if explicit:raise ValueError('Paralel motorun doğrulanmış sürümü uyuşmuyor.')
    return cli,1

class Worker:
    def __init__(self,request):
        from .settings import load
        request={**load(request.get('root')),**request}
        self.request=request;self.queue_path=Path(request['queue']);self.control=Path(request['control'])
        self.data=load_queue(self.queue_path);self.job=None
        from .queue_store import QueueStore
        self.store=QueueStore(self.queue_path);self.data=self.store.load(self.data)
        self.root=Path(request['root']);self.cli=Path(request['cli'])
        self.settings={k:request.get(k) for k in ('language','appearance','paper','margin_mm','target_x_mm','ocr_engine','ocr_device','paddle_language','tess_quality')}
        timing=request.get('ocr_timing','during')
        if timing not in ('during','after','none'):raise ValueError('Bilinmeyen OCR zamanı: '+str(timing))
        if timing!='during':self.settings['ocr_timing']=timing
        finish=request.get('output_finish',{})
        if any(finish.get(k,False) for k in ('lines','white','fine')):
            self.settings['output_finish']={k:bool(finish.get(k)) for k in ('lines','white','fine')}
            self.settings['output_finish_code']=file_hash(Path(__file__).with_name('output_finish.py'))
        if self.settings.get('appearance')=='soft_local':
            from .output_appearance import LOCAL_SOFT_VERSION
            self.settings['local_soft_profile']=LOCAL_SOFT_VERSION
        self.settings.update(pipeline='native-bridge-8',native_sha256=file_hash(self.cli),
            page_cleanup=request.get('page_cleanup',True),
            processing_code={name:file_hash(Path(__file__).with_name(name+'.py')) for name in ('book_layout','page_cleanup','engine','pdf_export')})
        from .pdf_export import PIXEL_PIPELINE_ID
        self.settings['processing_code']['pdf_export']=PIXEL_PIPELINE_ID
        from .book_layout import LAYOUT_PIPELINE_ID
        self.settings['processing_code']['book_layout']=LAYOUT_PIPELINE_ID
        self.execution_cli,self.native_output_workers=native_execution(self.root,self.cli,request)
        self.session=Path(str(request.get('request_path',self.queue_path))+'.session.json')
        self.event_log=Path(request['event_log']) if request.get('event_log') else None
        self.session_state='running'
        from .telemetry import Metrics
        self.metrics=Metrics()
        self._pending_events=[]
        quality=request.get('tess_quality','best')
        self.settings['ocr_models']={code:file_hash(path) for code in request.get('language','deu').split('+')
            if (path:=resource_path('tessdata_'+quality,self.root)/(code+'.traineddata')).is_file()}
        self.settings.update({k:request[k] for k in ('paddle_version','ocr_compare','ocr_confidence_threshold','ocr_max_edge','kraken','kraken_model') if k in request})
    def save(self,force=False):
        if getattr(self,'store',None):
            self.store.save(self.data,dirty_jobs=None if force or not self.job else {self.job['id']})
            now=time.monotonic()
            if force or now-getattr(self,'_last_publish',0)>=1 or self.session_state!='running':
                try:self.store.publish(self.data);self._last_publish=now
                except PermissionError:pass
            return
        published=save_queue(self.queue_path,self.data)
        if not published and not getattr(self,'_queue_deferred',False):
            print('Arayüz kuyruk dosyasını kullanıyor; ilerleme güvenli yedekte saklandı, işlem sürüyor.',file=sys.stderr,flush=True)
        self._queue_deferred=not published
    def emit(self,stage,detail='',**kwargs):
        if getattr(self,'metrics',None):self.metrics.event(stage)
        event={'job':self.job['id'] if self.job else '', 'book':self.job['title'] if self.job else '', 'stage':stage,'detail':detail,**kwargs,'time':time.time()}
        if self.job:self.job.update(status=stage,detail=detail)
        try:write_json(self.session,{'pid':os.getpid(),'state':self.session_state,'updated':time.time(),'event':event})
        except OSError as exc:
            # Telemetry must not abort OCR or lose a completed page. The queue
            # remains authoritative and its save errors are still fatal.
            print(f'İlerleme durum dosyası geçici olarak yazılamadı: {exc}',file=sys.stderr,flush=True)
        if self.event_log:
            self.event_log.parent.mkdir(parents=True,exist_ok=True)
            pending=getattr(self,'_pending_events',[])
            pending.append(json.dumps(event,ensure_ascii=False)+'\n')
            try:
                if self.event_log.exists() and self.event_log.stat().st_size>16*1024**2:
                    # Bounded telemetry; queue checkpoints and reviews are separate.
                    for index in (2,1):
                        old=self.event_log.with_name(self.event_log.name+f'.{index}')
                        if old.exists():os.replace(old,self.event_log.with_name(self.event_log.name+f'.{index+1}'))
                    os.replace(self.event_log,self.event_log.with_name(self.event_log.name+'.1'))
                with self.event_log.open('a',encoding='utf8') as log:log.writelines(pending)
                pending=[];self._log_deferred=False
            except OSError as exc:
                if not getattr(self,'_log_deferred',False):
                    print(f'İlerleme günlüğü meşgul; kuyruk ve canlı durum korunuyor, işlem sürüyor: {exc}',file=sys.stderr,flush=True)
                self._log_deferred=True
            # The authoritative queue is durable; bounded telemetry buffering
            # cannot stop processing or exhaust RAM if a GUI keeps its log open.
            self._pending_events=pending[-1000:]
        print(json.dumps(event,ensure_ascii=False),flush=True)
    def control_state(self):
        try:return json.loads(self.control.read_text(encoding='utf8')).get('state','run')
        except (OSError,ValueError):return 'run'
    def revision_in_progress(self):
        job=getattr(self,'job',None) or {}
        return bool(job.get('edit_request_id') and job.get('edit_completed_id')!=job['edit_request_id'])
    def revision_waiting(self):
        # An explicit editor save takes priority at the next saved page. Never
        # interrupt one revision with another, or restart an identical revision.
        if self.revision_in_progress():return False
        now=time.monotonic()
        if now-getattr(self,'_revision_checked',0)<.5:return False
        self._revision_checked=now
        for path in Path(str(self.queue_path)+'.edits').glob('*.request.json'):
            try:
                r=json.loads(path.read_text('utf8'))
                job=next((j for j in self.data['jobs'] if j['id']==r.get('job_id')),None)
                if job and (job.get('edit_snapshot_sha256')!=r.get('sha256') or
                            job.get('preserve_edit_margins',True)!=bool(r.get('preserve_margins',True))):return True
            except (OSError,ValueError):pass
        return False
    def effective_control_state(self):
        state=self.control_state()
        if state!='pause':self._revision_pause_bypass=False
        if state=='pause' and self.revision_in_progress() and getattr(self,'_revision_pause_bypass',False):return 'run'
        return state
    def checkpoint(self):
        paused=False
        while True:
            state=self.effective_control_state()
            if state=='cancel':raise Cancelled()
            if self.revision_waiting():raise RevisionPending()
            if state!='pause' and self.resources_ready():return
            if state!='pause':
                time.sleep(.5);continue
            if not paused:self.emit('Duraklatıldı','Devam et düğmesiyle sürdürülebilir.');paused=True
            time.sleep(.15)
    def resources_ready(self):
        from .storage import ensure_space, temp_root, StoragePressure
        from .runtime_layout import data_root
        now=time.monotonic()
        if now-getattr(self,'_space_checked',0)<2:return not getattr(self,'_space_pressure',False)
        self._space_checked=now
        try:
            ensure_space([self.data.get('output_root',self.queue_path.parent),self.request.get('work_root') or self.data.get('output_root',self.queue_path.parent),self.queue_path.parent,temp_root(self.request),data_root(self.request.get('root'))],
                needed=64*1024**2,reserve_gib=self.request.get('reserve_gib',2))
        except StoragePressure as exc:
            if not getattr(self,'_space_pressure',False):
                self.session_state='resource_pause';self.emit('Disk alanı bekleniyor',str(exc));self.save()
            self._space_pressure=True;return False
        if getattr(self,'_space_pressure',False):
            self.session_state='running';self.emit('Devam ediliyor','Disk alanı yeniden yeterli.')
        self._space_pressure=False;return True
    def native(self,args):
        # Native checkpoint control is private to this invocation: requesting a
        # revision can stop the current pass without cancelling the whole queue.
        native_control=self.control.with_name(self.control.name+'.native.json')
        last_control=self.effective_control_state();write_json(native_control,{'state':last_control})
        env=os.environ.copy();env.update(STOU_CONTROL_FILE=str(native_control),STOU_PROGRESS='1')
        env['STOU_CHECKPOINT_TOKEN']=uuid.uuid4().hex
        env.pop('STOU_RESUME_PAGE',None)
        resume=self.request.get('resume_native')
        if resume and not getattr(self,'_native_resumed',False) and resume.get('job_id')==self.job['id']:
            from .native_resume import resume_native_args
            args,page=resume_native_args(args,resume,self.job,self.request)
            env['STOU_RESUME_PAGE']=str(page)
            self._native_resumed=True
        from .telemetry import resources
        budget=resources(self.request.get('resource_profile','balanced'),self.request.get('threads',4))
        env['OMP_NUM_THREADS']=str(budget['threads']);env['OMP_THREAD_LIMIT']=env['OMP_NUM_THREADS']
        env['STOU_AUTO_WARP']='1' if self.request.get('auto_warp',False) else '0'
        from .memory_budget import bounded_workers
        env['STOU_OUTPUT_WORKERS']=str(min(budget['workers'],bounded_workers(getattr(self,'native_output_workers',1))))
        proc=subprocess.Popen([str(getattr(self,'execution_cli',self.cli)),*map(str,args)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                              env=env,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        recent=[];messages=thread_queue.Queue()
        def read_output():
            try:
                for raw in iter(proc.stdout.readline,b''):messages.put(raw)
            finally:messages.put(None)
        reader=threading.Thread(target=read_output,daemon=True);reader.start()
        last_progress=time.monotonic();cancel_since=None;last_page='';last_event={};priority=False
        timeout=float(self.request.get('native_page_timeout',180))
        cancel_grace=30 if any(str(a).startswith('--output-project=') for a in args) else 2
        try:
            while True:
                state=self.effective_control_state()
                if state=='run' and not self.resources_ready():state='pause'
                if state!='cancel' and self.revision_waiting():priority=True
                native_state='cancel' if priority else state
                if native_state!=last_control:write_json(native_control,{'state':native_state});last_control=native_state
                try:raw=messages.get(timeout=.2)
                except thread_queue.Empty:
                    now=time.monotonic()
                    if native_state=='cancel':
                        if cancel_since is None:cancel_since=now
                        # A large atomic project save must finish before the
                        # owned native child is stopped at a page boundary.
                        if now-cancel_since>=cancel_grace:
                            if priority and state!='cancel':raise RevisionPending()
                            raise Cancelled()
                    if state=='pause':last_progress=now
                    elif now-last_progress>timeout:
                        raise NativeTimeout(f'Experimental {timeout:g} saniye ilerleme bildirmedi: {last_page}. Son kaydedilen proje korundu; bu sayfayı kontrol edin.',last_event)
                    continue
                if raw is None:break
                last_progress=time.monotonic()
                line=raw.decode('utf8',errors='replace').strip()
                if line.startswith('STOU '):
                    try:event=json.loads(line[5:])
                    except ValueError:continue
                    if 'checkpoint' in event:
                        self.job['native_checkpoint']={**event['checkpoint'],'token':env['STOU_CHECKPOINT_TOKEN']}
                        timings=self.job.setdefault('native_checkpoint_timing',{'count':0,'seconds':0})
                        timings['count']+=1;timings['seconds']+=event.get('checkpoint_seconds',0)
                        self.save();continue
                    last_event=dict(event)
                    stage=event.pop('stage','İşleniyor');detail=event.pop('detail','')
                    last_page=f'{stage} · {detail} · sayfa {event.get("page", "?")}'
                    self.emit(stage,detail,**event)
                elif line:
                    recent.append(line);recent=recent[-12:]
            proc.wait(timeout=10)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()
            reader.join(timeout=2);proc.stdout.close()
        code=proc.wait()
        if priority and self.control_state()!='cancel':raise RevisionPending()
        self.checkpoint()
        if code:raise NativeCrash(code,last_event,'\n'.join(recent)[-1500:])
    def native_resilient(self,args,project):
        serial_retry=False
        for attempt in range(4):
            try:return self.native(args)
            except NativeCrash as exc:
                # A process access violation is not a bad user page setting.
                # Retry the saved project once, serially, without editing its
                # manual geometry. Already valid output is reused by the CLI.
                crash=exc.code in (3221225477,-1073741819,-11,139)
                if serial_retry or not crash or self.native_output_workers<=1 or exc.event.get('stage')!='Output':raise
                self.checkpoint();serial_retry=True;self.native_output_workers=1
                self.job.setdefault('native_recoveries',[]).append({'code':exc.code,'event':exc.event,'action':'serial-output'})
                self.emit('Motor yeniden deneniyor','Paralel çıktı kesildi; kayıtlı proje tek sayfa işlenerek sürdürülüyor.')
                self.save()
            except NativeTimeout as exc:
                self.checkpoint()
                fallback=use_rotation_after_timeout(project,exc.event) if attempt<3 and Path(project).is_file() else None
                if not fallback:raise
                self.job.setdefault('geometry_timeout_fallbacks',[]).append(fallback)
                self.emit('Geometri yedek yöntem',f'{fallback["page"]}. sayfa · eğiklik düzeltmesiyle yeniden deneniyor.')
                self.save()
    def prepare(self):
        self._prepared_in_pass=False
        job=self.job
        folder=self.book_folder(working=True);folder.mkdir(parents=True,exist_ok=True)
        if job.get('revision_token'):
            token=job['revision_token']
            if not re.fullmatch(r'[A-Za-z0-9-]{1,80}',token):raise ValueError('Geçersiz düzeltme klasörü.')
            folder=folder/'revisions'/token;folder.mkdir(parents=True,exist_ok=True)
        project=folder/'kitap.ScanTailor';out=folder/'out';out.mkdir(exist_ok=True)
        # Moving the working disk never moves originals or drops saved edits.
        # Resolve source paths before writing the new copy on the faster disk.
        previous=Path(job['project']) if job.get('project') else None
        if not project.exists() and previous and previous.is_file() and not job.get('revision_token'):
            resume=self.request.get('resume_native')
            if resume and resume.get('job_id')==job['id']:
                if Path(resume.get('project','')).resolve()!=previous.resolve() or file_hash(previous)!=resume.get('sha256'):
                    raise ValueError('Taşınacak devam kaydı doğrulanamadı; önceki proje korundu.')
            clone_project(previous,project,out)
            if resume and resume.get('job_id')==job['id']:
                self.request={**self.request,'resume_native':{**resume,'project':str(project),'sha256':file_hash(project)}}
            job['work_migration']={'source_project':str(previous),'sha256':file_hash(previous),'time':time.time()}
            job.pop('native_cache',None)
        initializing=not project.exists() or job.get('preparing',False)
        job.update(project=str(project),work=str(folder));self.save(force=True)
        if not project.exists():
            if job.get('source_project'):
                clone_project(job['source_project'],project,out)
            else:
                files=sorted((p for p in Path(job['folder']).iterdir() if p.is_file() and p.suffix.lower() in IMAGE_TYPES),key=natural_key)
                if not files:raise ValueError('Kitap klasöründe resim bulunamadı.')
                layout=job.get('split','auto');args=['--end-filter=5','--output-project='+str(project)]
                if layout in ('1','2'):args.append('--layout='+layout)
                job['preparing']=True;self.save()
                native_args=[*args,*files,out]
                if len(subprocess.list2cmdline([str(self.cli),*map(str,native_args)]))>26000:
                    inputs=folder/'input-files.json';write_json(inputs,[str(p.resolve()) for p in files])
                    native_args=[*args,'--input-list='+str(inputs),out]
                self.native_resilient(native_args,project)
                self._prepared_in_pass=True
                job.pop('preparing',None);self.save()
        elif job.get('preparing'):
            # Cancellation may leave an initial checkpoint before page splitting.
            # Reapply the initial choice until preparation finishes, then retain
            # all subsequent manual edits without overriding their page settings.
            args=['--end-filter=5','--output-project='+str(project)]
            if job.get('split') in ('1','2'):args.append('--layout='+job['split'])
            self.native_resilient([*args,project,out],project);self._prepared_in_pass=True
            job.pop('preparing',None);self.save()
        else:
            # A prepared project's manual adjustments always take precedence.
            self.emit('Proje açıldı','Kaydedilmiş sayfa ve geometri ayarları korunuyor.')
        if initializing and self.request.get('native_scale') and not job.get('edit_request_id'):
            # Only the new working copy; also applied after an interrupted
            # initial preparation resumes successfully.
            set_native_scale(project,self.request['native_scale'])
        return folder,project,out
    def book_folder(self,working=False):
        base=(self.request.get('work_root') if working else None) or self.data['output_root']
        return Path(base)/(safe_name(self.job['title'])+'_'+self.job['id'][:8])
    def publish_book(self,folder):
        from .work_storage import publish_book
        target=self.book_folder()
        if self.job.get('revision_token'):target=target/'revisions'/self.job['revision_token']
        publish_book(self.job,folder,target,checkpoint=self.checkpoint)
    def export(self,folder,project,out):
        from .pdf_export import export, verify_page, ocr_protection
        from .engine import soft_monochrome
        from pypdf import PdfReader,PdfWriter
        import numpy as np
        import fitz
        self.checkpoint()
        current=fingerprint(project,{**self.settings,'cleanup_overrides':self.job.get('cleanup_overrides',{})})
        if self.job.get('signature')==current and self.job.get('pdf'):
            p=Path(self.job['pdf'])
            if p.exists() and file_hash(p)==self.job.get('pdf_hash'):
                if self.request.get('replay_cached_pages',True):
                    for page in self.job.get('pages',[]):self.emit('Sayfa hazır','Önbellekten',**page)
                self.emit('Kontrol gerekli' if self.job.get('warnings') else 'Tamamlandı','Değişmeyen, doğrulanmış PDF korundu.',preview=str(p));return
        # CLI preserves all filter settings from this copy. It also emits the
        # authoritative page sequence, independent of width/height UI sorting.
        # Prepared books have geometry for every logical page. The content
        # pipeline still validates its upstream dependencies; then the separate
        # margin pass refreshes the whole-book size before output is written.
        prepared=ET.parse(project).getroot()
        page_ids={p.get('id') for p in prepared.findall('./pages/page')}
        has_geometry=bool(page_ids) and page_ids <= {p.get('id') for p in prepared.findall('./filters/deskew/page') if p.find('params') is not None}
        native_signature=fingerprint(project,{'cli':self.settings['native_sha256']})
        reused=self.job.get('native_cache',{})
        reuse_ok=reused.get('signature')==native_signature and bool(reused.get('files'))
        if reuse_ok:
            reuse_ok=all(Path(p).is_file() and file_hash(p)==sha for p,sha in reused['files'].items())
        if reuse_ok:self.emit('Geometri önbellekten','Doğrulanmış geometrik çıktılar yeniden kullanılıyor.')
        else:self.native_resilient(['--start-filter='+('4' if has_geometry else '1'),'--end-filter=6','--output-project='+str(project),project,out],project)
        manifest=Path(str(project)+'.outputs.json')
        outputs=json.loads(manifest.read_text(encoding='utf8'))['pages']
        if not outputs:raise ValueError('Experimental çıktı sayfası üretmedi.')
        if not reuse_ok:
            self.job['native_cache']={'signature':fingerprint(project,{'cli':self.settings['native_sha256']}),
                'files':{str(p):file_hash(p) for name in outputs for p in (Path(name),Path(name+'.ocr.tif'))}}
            self.save()
        from .book_layout import plan
        def layout_progress(n,total):
            self.checkpoint();self.emit('Yazı ölçeği ölçülüyor',f'{n}/{total}',page=n,total=total)
        layout=plan(outputs,self.request.get('paper','a4'),self.request.get('margin_mm',10.),self.request.get('target_x_mm',0.),notify=layout_progress,
                    workers=self.request.get('layout_workers',min(4,int(self.request.get('threads',4)))))
        write_json(folder/'layout-plan.json',layout)
        write_json(folder/'native-layout-plan.json',layout)
        work=folder/'.ocr-cache';work.mkdir(exist_ok=True)
        # Recognition is addressed by image pixels and engine settings. Reuse
        # unchanged pages across revisions, without copying or trusting names.
        text_cache=(folder.parent.parent if self.job.get('revision_token') else folder)/'.ocr-text-cache'
        timing=self.request.get('ocr_timing','during')
        deferred=[]
        page_pdfs=[];decisions=geometry_warnings(project)
        decisions.update({r['page']:r['reason'] for r in self.job.get('geometry_timeout_fallbacks',[])})
        warnings=list(decisions);count=len(outputs)
        from .quality import source_mapping, reasons, inspect_page, integrity
        mapping=source_mapping(project)
        self.job['pages']=[]
        tools=json.loads((self.root/'local-tools.json').read_text(encoding='utf8'))
        for n,source_name in enumerate(outputs,1):
            page_started=time.monotonic()
            self.checkpoint();source=Path(source_name);gray_path=Path(str(source)+'.ocr.tif')
            placement=layout['pages'][n-1];pxmm=placement['pxmm']
            if n in decisions:self.emit('Geometriyi kontrol edin',decisions[n],page=n,total=count)
            if placement['warning']:warnings.append(n);self.emit('Yerleşimi kontrol edin',placement['warning'],page=n,total=count)
            if not gray_path.exists():raise FileNotFoundError('Eşikleme öncesi OCR kopyası eksik: '+str(gray_path))
            mode=self.request.get('appearance','native')
            override=self.job.get('cleanup_overrides',{}).get(str(n),{})
            key=hashlib.sha256(json.dumps([file_hash(source),file_hash(gray_path),self.settings,placement['image_mm'],override,'native-bridge-5']).encode()).hexdigest()
            cache=work/key if (work/key).exists() else work/key[:32]
            cache.mkdir(exist_ok=True)
            visible=cache/'display.tif';ocr=cache/'ocr.png';page_pdf=cache/'page.pdf';preview=cache/'preview.png';receipt=cache/'verified.json'
            valid=False
            if receipt.exists():
                record=json.loads(receipt.read_text(encoding='utf8'))
                page_pdf=Path(record.get('pdf',str(page_pdf)))
                valid=page_pdf.exists() and file_hash(page_pdf)==record.get('sha256') and record.get('cache_key',key)==key
            self.emit('OCR hazırlanıyor' if timing=='during' else 'Görüntü hazırlanıyor',source.name,page=n-1,total=count)
            if not valid:
                if page_pdf.exists():page_pdf=next_path(cache,'page','.pdf')
                with Image.open(gray_path) as im:gray=im.convert('L')
                finish=self.request.get('output_finish',{});finish_report={};native_finished=None
                if finish.get('lines'):
                    from .output_finish import geometry_finish
                    with Image.open(source) as im:original=im.convert('RGB') if im.mode=='RGB' else im.convert('L')
                    gray,native_finished,finish_report=geometry_finish(gray,original,finish)
                    decision=finish_report.get('lines',{})
                    if not decision.get('applied'):native_finished=None
                    self.emit('Son satır düzeltme',decision.get('reason',''),page=n-1,total=count)
                recognition_gray=gray.copy()
                cleanup={'removed':[],'uncertain':[]}
                if self.request.get('page_cleanup',True) and mode!='native' and timing=='during':
                    from .page_cleanup import analyze,apply
                    if not (placement['body_measurement'] or {}).get('sideways'):
                        self.emit('Başlık / numara korunuyor',source.name,page=n-1,total=count)
                        protection_conf={'tessdata':str(resource_path('tessdata_'+self.request.get('tess_quality','best'),self.root)),
                            'ocr_cache':str(text_cache),'root':str(self.root),
                            'ocr_engine':self.request.get('ocr_engine','tesseract'),'ocr_device':self.request.get('ocr_device','cpu'),
                            'paddle_language':self.request.get('paddle_language','de'),'language':self.request['language']}
                        protection_conf.update({k:self.request[k] for k in ('temp_root','reserve_gib','paddle_version','kraken','kraken_model') if k in self.request})
                        boxes=ocr_protection(recognition_gray,protection_conf,notify=lambda message:self.emit('Başlık / numara korunuyor',message,page=n-1,total=count))
                        mask,cleanup=analyze(np.asarray(gray),allow_closed_frame=override.get('allow_closed_frame',False),protected_boxes=boxes,
                                             reviewed_components=override.get('reviewed_components',[]))
                        cleanup['ocr_protected_regions']=len(boxes)
                        if override.get('verified_blank'):
                            mask[:]=255;cleanup['verified_blank']=True
                        Image.fromarray(mask).save(cache/'cleanup-mask.png')
                        gray=Image.fromarray(apply(np.asarray(gray),mask))
                    else:cleanup['retained_sideways_form']=True
                    write_json(cache/'cleanup.json',cleanup)
                if mode=='native':
                    if native_finished is not None:display=native_finished
                    else:
                        with Image.open(source) as im:display=im.copy()
                    if display.mode not in ('1','L','RGB'):display=display.convert('RGB')
                else:
                    # Source output remains intact. This is explicitly an export
                    # rendition at up to 300 DPI, not a claim of photo losslessness.
                    display=gray.copy();display.thumbnail((round(display.width*pxmm*300/25.4),round(display.height*pxmm*300/25.4)),Image.Resampling.LANCZOS)
                    if mode=='soft':display=Image.fromarray(soft_monochrome(np.asarray(display)))
                    elif mode=='soft_local':
                        from .output_appearance import local_soft_monochrome
                        display=Image.fromarray(local_soft_monochrome(np.asarray(display)))
                if finish.get('white') or finish.get('fine'):
                    from .output_finish import tonal_finish
                    display,finish_report['tone']=tonal_finish(display.convert('RGB') if display.mode=='RGB' else display.convert('L'),finish.get('white',False),finish.get('fine',False))
                    display=Image.fromarray(display)
                if finish_report:write_json(cache/'output-finish.json',finish_report)
                display.save(visible,compression='group4' if display.mode=='1' else 'tiff_deflate',dpi=(300,300))
                # OCR uses the unthresholded, uncleaned geometry-identical copy;
                # recognition protects text before display-only debris removal.
                if override.get('verified_blank'):recognition_gray=Image.new('L',recognition_gray.size,255)
                max_edge=int(self.request.get('ocr_max_edge',4200))
                recognition_gray.thumbnail((max_edge,max_edge),Image.Resampling.LANCZOS);recognition_gray.save(ocr,dpi=(300,300),compress_level=1)
                # White canvas gives all A4 pages the SAME mm/pixel scale. No
                # binarization setting can alter it.
                image_mm=placement['image_mm'];page_mm=placement['page_mm']
                quality=self.request.get('tess_quality','best')
                if quality not in ('best','fast'):raise ValueError('Bilinmeyen OCR kalite paketi')
                conf={'tessdata':str(resource_path('tessdata_'+quality,self.root)),'jbig2':str((self.root/tools['jbig2']).resolve()) if tools.get('jbig2') else None,
                      'ocr_cache':str(text_cache),
                      'root':str(self.root),'ocr_engine':self.request.get('ocr_engine','tesseract'),'ocr_device':self.request.get('ocr_device','cpu'),'paddle_language':self.request.get('paddle_language','de'),
                      'language':self.request['language'],'pages':[{'id':str(n),'display_image':str(visible),'ocr_image':str(ocr),
                      'page_mm':page_mm,'image_mm':image_mm,'allow_empty_ocr':True,
                      'ocr_rotations':[0,90,270] if (placement['body_measurement'] or {}).get('sideways') else [0]}]}
                config=cache/'export.json';write_json(config,conf)
                if timing!='during':
                    visual_conf={**conf,'ocr_engine':'none'}
                    config=cache/'visual-export.json';write_json(config,visual_conf)
                self.emit('OCR / PDF' if timing=='during' else 'Görüntülü PDF',source.name,page=n-1,total=count,preview=str(visible))
                conf.update({k:self.request[k] for k in ('temp_root','reserve_gib','ocr_compare','ocr_confidence_threshold','paddle_version','ocr_max_edge','kraken','kraken_model') if k in self.request})
                write_json(cache/'export.json',conf)
                write_json(config,{**conf,'ocr_engine':'none'} if timing!='during' else conf)
                report=export(config,page_pdf,notify=lambda message:self.emit('OCR / PDF' if timing=='during' else 'Görüntülü PDF',message,page=n-1,total=count))
                chars=report['checks'][0]['ocr_characters']
                count_estimate=(placement['body_measurement'] or {}).get('components',0)
                weak_ocr=timing=='during' and (chars<15 or (count_estimate>200 and chars<count_estimate*.35))
                if weak_ocr:warnings.append(n)
                with fitz.open(page_pdf) as doc:doc[0].get_pixmap(dpi=100,alpha=False).save(preview)
                record={'sha256':file_hash(page_pdf),'cache_key':key,'chars':chars,'pdf':str(page_pdf),'weak_ocr':weak_ocr,
                        'pixels_sha256':report['checks'][0]['pixels_sha256'],
                        'confidence':report['checks'][0].get('ocr_metadata',{}).get('confidence'),
                        'comparison':report['checks'][0].get('ocr_metadata',{}).get('comparison'),
                        'ocr_pending':timing=='after','ocr_timing':timing,
                        'cleanup':cleanup,'cleanup_mask':str(cache/'cleanup-mask.png')};write_json(receipt,record)
            else:
                page_pdf=Path(record.get('pdf',str(page_pdf)))
                if record.get('weak_ocr') or (timing=='during' and record.get('chars',0)<15):warnings.append(n)
                if not preview.is_file():
                    with fitz.open(page_pdf) as doc:doc[0].get_pixmap(dpi=100,alpha=False).save(preview)
            from .storage import register
            register(preview,owner=str(page_pdf),root=self.root)
            page_pdfs.append(page_pdf)
            uncertain=record.get('cleanup',{}).get('uncertain',[])
            if uncertain:warnings.append(n)
            placement['cleanup']=record.get('cleanup',{})
            page_event={'page':n,'total':count,'preview':str(preview),'page_pdf':str(page_pdf),'warning':n in warnings,'chars':record['chars'],
                        'cleaned_regions':len(record.get('cleanup',{}).get('removed',[])),'cleanup_mask':record.get('cleanup_mask','')}
            page_event.update(mapping[n-1] if n<=len(mapping) else {})
            page_event.update(pixels_sha256=record.get('pixels_sha256'),confidence=record.get('confidence'),
                seconds=round(time.monotonic()-page_started,3),pdf_sha256=record['sha256'])
            page_event['reasons']=reasons(chars=record['chars'],expected_text=timing=='during',geometry=decisions.get(n,''),
                placement=placement.get('warning',''),uncertain=uncertain,confidence=record.get('confidence'),
                threshold=self.request.get('ocr_confidence_threshold',.85))
            if record.get('comparison',{}):
                comparison=record['comparison'];page_event['ocr_comparison']=comparison
                if comparison.get('agreement',1)<.8:
                    page_event['reasons'].append({'code':'engine_disagreement','severity':'warning','detail':'Paddle ve Tesseract metinleri belirgin farklı.'})
            with fitz.open(page_pdf) as document:page_event['reasons'].extend(inspect_page(document[0]))
            if page_event['reasons']:warnings.append(n);page_event['warning']=True
            self.job['pages'].append(page_event)
            pending=timing=='after' and record.get('ocr_pending',False)
            if pending:deferred.append((n,cache,record,placement))
            self.emit('Görüntü hazır · OCR bekliyor' if pending else 'Sayfa hazır',f'{n}/{count} · '+('OCR kapalı' if timing=='none' else f'{record["chars"]} OCR karakteri')+(' · kontrol edin' if n in warnings else ''),**page_event)
            self.job['completed_pages']=n;self.save()
            delay=int(self.request.get('delay_ms',0));
            if delay:time.sleep(delay/1000)
        # Deferred mode finishes every visible page before any recognition.
        if deferred and not self.job.get('visual_pdf_signature')==current:
            visual=next_path(folder,safe_name(self.job['title'])+' - OCR Bekliyor','.pdf')
            private=folder/('.visual-'+uuid.uuid4().hex+'.pdf')
            try:
                writer=PdfWriter()
                for path in page_pdfs:writer.append(path)
                with private.open('xb') as stream:writer.write(stream)
                del writer
                with fitz.open(private) as doc:
                    if len(doc)!=count:raise ValueError('Görüntülü PDF sayfa sayısı uyuşmuyor.')
                from .pdf_profiles import publish
                publish(private,visual)
            finally:private.unlink(missing_ok=True)
            self.job.update(visual_pdf=str(visual),visual_pdf_signature=current)
            self.emit('Görüntülü PDF hazır','OCR en son yapılacak; bu PDF şimdi incelenebilir.',preview=str(visual));self.save()
        # Completed OCR receipts survive interruption; the next run resumes
        # only pending pages and never repeats native geometry or display work.
        for n,cache,record,placement in deferred:
            ocr_started=time.monotonic()
            self.checkpoint();conf=json.loads((cache/'export.json').read_text(encoding='utf8'))
            self.emit('Çıktı sonrası OCR',f'{n}/{count}',page=n-1,total=count)
            if self.request.get('page_cleanup',True) and self.request.get('appearance')!='native' and not (placement.get('body_measurement') or {}).get('sideways'):
                from .page_cleanup import analyze,apply
                entry=conf['pages'][0]
                with Image.open(entry['ocr_image']) as im:recognition=im.convert('L')
                boxes=ocr_protection(recognition,conf,notify=lambda message:self.emit('Çıktı sonrası OCR',message,page=n-1,total=count))
                override=self.job.get('cleanup_overrides',{}).get(str(n),{})
                mask,cleanup=analyze(np.asarray(recognition),protected_boxes=boxes,allow_closed_frame=override.get('allow_closed_frame',False),reviewed_components=override.get('reviewed_components',[]))
                cleanup['ocr_protected_regions']=len(boxes);Image.fromarray(mask).save(cache/'cleanup-mask.png')
                with Image.open(entry['display_image']) as im:display=Image.fromarray(apply(np.asarray(im.convert('L')),mask))
                final_display=cache/'display-final.tif';display.save(final_display,compression='tiff_deflate',dpi=(300,300))
                entry['display_image']=str(final_display);record['cleanup']=cleanup;placement['cleanup']=cleanup
                if cleanup.get('uncertain'):warnings.append(n)
            config=cache/'after-export.json';write_json(config,conf)
            page_pdf=next_path(cache,'page-ocr','.pdf')
            report=export(config,page_pdf,notify=lambda message:self.emit('Çıktı sonrası OCR',message,page=n-1,total=count))
            chars=report['checks'][0]['ocr_characters'];estimate=(placement.get('body_measurement') or {}).get('components',0)
            weak=chars<15 or (estimate>200 and chars<estimate*.35)
            if weak:warnings.append(n)
            record.update(sha256=file_hash(page_pdf),chars=chars,pdf=str(page_pdf),weak_ocr=weak,ocr_pending=False)
            record.update(confidence=report['checks'][0].get('ocr_metadata',{}).get('confidence'),
                comparison=report['checks'][0].get('ocr_metadata',{}).get('comparison'),pixels_sha256=report['checks'][0]['pixels_sha256'])
            write_json(cache/'verified.json',record);page_pdfs[n-1]=page_pdf
            with fitz.open(page_pdf) as doc:doc[0].get_pixmap(dpi=100,alpha=False).save(cache/'preview.png')
            event=self.job['pages'][n-1];event.update(page_pdf=str(page_pdf),chars=chars,warning=n in warnings,cleaned_regions=len(record.get('cleanup',{}).get('removed',[])))
            event.update(pdf_sha256=record['sha256'],confidence=record.get('confidence'),pixels_sha256=record['pixels_sha256'],
                seconds=event.get('seconds',0)+round(time.monotonic()-ocr_started,3),
                reasons=reasons(chars=chars,confidence=record.get('confidence'),geometry=decisions.get(n,''),placement=placement.get('warning',''),
                    uncertain=record.get('cleanup',{}).get('uncertain',[]),threshold=self.request.get('ocr_confidence_threshold',.85)))
            if record.get('comparison'):
                event['ocr_comparison']=record['comparison']
                if record['comparison'].get('agreement',1)<.8:event['reasons'].append({'code':'engine_disagreement','severity':'warning','detail':'Paddle ve Tesseract metinleri belirgin farklı.'})
            with fitz.open(page_pdf) as doc:event['reasons'].extend(inspect_page(doc[0]))
            if event['reasons']:warnings.append(n);event['warning']=True
            register(cache/'preview.png',owner=str(page_pdf),root=self.root)
            self.emit('Sayfa hazır',f'{n}/{count} · {chars} OCR karakteri',**event);self.save()
        self.checkpoint();self.emit('PDF birleştiriliyor',f'{count} sayfa',page=count,total=count)
        write_json(folder/'layout-plan.json',layout)
        write_json(folder/'native-layout-plan.json',layout)
        destination=next_path(folder,safe_name(self.job['title']),'.pdf');temp=folder/('.'+uuid.uuid4().hex+'.pdf')
        writer=PdfWriter()
        for index,p in enumerate(page_pdfs,1):
            self.checkpoint();writer.append(str(p))
            if index%20==0 or index==count:
                self.emit('PDF birleştiriliyor',f'{index}/{count} sayfa eklendi',page=index,total=count)
        with temp.open('xb') as f:writer.write(f)
        # Free the merged in-memory PDF before another complete writer is
        # constructed by lossless packing.
        del writer
        import gc
        gc.collect()
        with fitz.open(temp) as final:
            if len(final)!=count:raise ValueError('PDF sayfa sayısı uyuşmuyor.')
            for index,p in enumerate(page_pdfs):
                self.checkpoint()
                with fitz.open(p) as reference:
                    if final[index].get_pixmap(dpi=72,alpha=False).samples!=reference[0].get_pixmap(dpi=72,alpha=False).samples:
                        raise ValueError(f'Birleştirmede sayfa görünümü değişti: {index+1}')
                if (index+1)%20==0 or index+1==count:
                    self.emit('PDF kontrol ediliyor',f'{index+1}/{count} sayfanın görüntüsü doğrulandı',page=index+1,total=count)
                    fitz.TOOLS.store_shrink(100)
        from .pdf_optimize import compact
        self.emit('PDF küçültülüyor','Görüntü pikselleri ve OCR metni değiştirilmeden.',page=count,total=count)
        packed=folder/('.'+uuid.uuid4().hex+'.pdf')
        def packing_progress(message):
            self.checkpoint()
            self.emit('PDF küçültülüyor',message,page=count,total=count)
        from .memory_budget import bounded_workers
        packing=compact(temp,packed,notify=packing_progress,workers=bounded_workers(4,256*1024**2))
        self.checkpoint()
        temp.unlink();temp=packed
        self.job['lossless_packing']=packing
        if self.job.get('source_text') and (Path(self.job['source_text'])/'import.json').is_file():
            from .pdf_metadata import restore_import
            annotated=folder/('.metadata-'+uuid.uuid4().hex+'.pdf')
            self.job['source_navigation']=restore_import(temp,annotated,self.job['source_text'],self.job['pages'])
            temp.unlink();temp=annotated
        os.link(temp,destination);temp.unlink()
        warnings=sorted(set(warnings))
        self.job.update(pdf=str(destination),pdf_hash=file_hash(destination),signature=fingerprint(project,{**self.settings,'cleanup_overrides':self.job.get('cleanup_overrides',{})}),warnings=warnings)
        self.job['verification']={'pages':count,'all_pages_render_identical':True,'image_pixels_verified':True,
                                  'packing_verified':packing,'checked_at':time.time(),'ocr_timing':timing}
        self.job['integrity']=integrity(self.job['pages'],len(mapping))
        for issue in self.job['integrity']:
            if issue.get('page'):
                row=self.job['pages'][issue['page']-1]
                row.update(reasons=[*row.get('reasons',[]),issue],warning=True)
                warnings.append(issue['page'])
        self.job['warnings']=sorted(set(warnings))
        self.emit('Kontrol gerekli' if warnings else 'Tamamlandı',f'{count} sayfa · {destination.stat().st_size/1024/1024:.2f} MB'+(f' · {len(warnings)} sayfada OCR / yerleşim kontrolü' if warnings else ''),page=count,total=count,preview=str(destination))
    def finalize_layout(self,folder):
        """Cheap, reversible paper placement after the expensive OCR pipeline."""
        import fitz
        from .pdf_layout import plan,reframe,signature
        paper=self.request.get('paper','a4')
        enabled=self.request.get('content_fit',True) and paper in ('a4_text','a4_fill')
        job=self.job;revision=job.get('layout_revision',{})
        continuing=revision.get('pdf_hash')==job.get('pdf_hash')
        base=Path(revision['base_pdf']) if continuing else Path(job['pdf'])
        base_hash=revision['base_hash'] if continuing else job['pdf_hash']
        base_warnings=revision.get('base_warnings',[]) if continuing else job.get('warnings',[])
        settings=signature(paper,self.request.get('margin_mm',10),self.request.get('target_x_mm',0))
        if continuing and revision.get('enabled')==enabled and revision.get('settings')==settings:
            return
        if not enabled and not continuing:
            return
        if not base.is_file() or file_hash(base)!=base_hash:
            raise ValueError('Önceki PDF yerleşiminin kaynak dosyası doğrulanamadı.')
        native_plan=folder/'native-layout-plan.json'
        if not native_plan.exists():
            write_json(native_plan,json.loads((folder/'layout-plan.json').read_text('utf8')))
        old_layout=json.loads(native_plan.read_text('utf8'))
        base_pages=[]
        for event in job.get('pages',[]):
            row=dict(event)
            row['page_pdf']=row.pop('base_page_pdf',row['page_pdf'])
            row['preview']=row.pop('base_preview',row['preview'])
            if Path(row['page_pdf']).is_file():row['pdf_sha256']=file_hash(row['page_pdf'])
            row['reasons']=row.pop('base_reasons',row.get('reasons',[]))
            row.pop('layout_scale',None)
            row['warning']=row['page'] in base_warnings
            base_pages.append(row)
        if not enabled:
            job.update(pdf=str(base),pdf_hash=base_hash,pages=base_pages,warnings=base_warnings)
            write_json(folder/'layout-plan.json',old_layout)
            job['layout_revision']={**revision,'enabled':False,'pdf_hash':base_hash,'settings':settings}
            self.save();return
        def measuring(n,total):
            self.checkpoint()
            if n%10==0 or n==total:self.emit('Beyaz kenarlar ölçülüyor',f'{n}/{total} · sayfa numaraları ve dipnotlar dahil',page=n,total=total)
        layout=plan(base,old_layout,paper,self.request.get('margin_mm',10),self.request.get('target_x_mm',0),measuring)
        destination=next_path(folder,safe_name(job['title'])+' - Dar Kenar','.pdf')
        def placing(n,total):
            self.checkpoint()
            if n%20==0 or n==total:self.emit('Sayfa yerleşimi düzeltiliyor',f'{n}/{total} · görüntü ve OCR birlikte taşınıyor',page=n,total=total)
        check=reframe(base,destination,layout,placing)
        revision_key=hashlib.sha256(json.dumps([base_hash,settings]).encode()).hexdigest()[:10]
        events=[];warnings=set(base_warnings)
        with fitz.open(destination) as doc:
            if len(base_pages)!=len(doc):raise ValueError('Tamamlanan sayfa kayıtları PDF sayfa sayısıyla uyuşmuyor.')
            for n,(page,row,event) in enumerate(zip(doc,layout['pages'],base_pages),1):
                self.checkpoint()
                parent=Path(event['page_pdf']).parent
                single=parent/f'layout-{revision_key}.pdf'
                preview=parent/f'layout-{revision_key}.png'
                if not single.exists():
                    with fitz.open() as one:
                        one.insert_pdf(doc,from_page=n-1,to_page=n-1)
                        one.save(single,garbage=4,deflate=True,use_objstms=1)
                with fitz.open(single) as one:
                    if one[0].get_text()!=page.get_text():raise ValueError(f'Önizleme OCR metni uyuşmuyor: {n}')
                    pix=one[0].get_pixmap(dpi=100,alpha=False)
                    if pix.samples!=page.get_pixmap(dpi=100,alpha=False).samples:raise ValueError(f'Önizleme görünümü uyuşmuyor: {n}')
                    pix.save(preview)
                if row['warning']:warnings.add(n)
                updated={**event,'base_page_pdf':event['page_pdf'],'base_preview':event['preview'],
                         'base_reasons':event.get('reasons',[]),'page_pdf':str(single),'pdf_sha256':file_hash(single),
                         'preview':str(preview),'layout_scale':row['scale'],'warning':n in warnings}
                from .quality import inspect_page
                updated['reasons']=[*event.get('reasons',[]),*inspect_page(page)]
                if row['warning']:updated['reasons'].append({'code':'placement','severity':'warning','detail':row['warning']})
                from .storage import register
                register(preview,owner=str(single),root=self.root)
                events.append(updated)
                self.emit('Sayfa hazır',f'{n}/{len(doc)} · içerik ölçeği ×{row["scale"]:.2f}',**updated)
                fitz.TOOLS.store_shrink(100)
        # Preserve the prior PDF and its page caches. Only the queue's selected
        # result changes after all layout and thumbnail checks have passed.
        job.update(pdf=str(destination),pdf_hash=file_hash(destination),pages=events,warnings=sorted(warnings))
        job['layout_revision']={'enabled':True,'settings':settings,'base_pdf':str(base),'base_hash':base_hash,
                                'base_warnings':base_warnings,'pdf_hash':job['pdf_hash'],'verification':check}
        job.setdefault('verification',{})['layout']=check
        write_json(folder/'layout-plan.json',layout)
        self.emit('Kontrol gerekli' if warnings else 'Tamamlandı',f'{len(events)} sayfa · dar kenarlı A4 · görüntü pikselleri ve OCR korundu.',preview=str(destination))
        self.save()
    def run(self):
        errors=0
        jobs=list(self.data['jobs'])
        resume=self.request.get('resume_at_job')
        if resume and not self.request.get('revision_only'):
            start=next(i for i,j in enumerate(jobs) if j['id']==resume)
            preceding=jobs[:start]
            if self.request.get('mode')=='prepare':
                complete=lambda j: bool(j.get('error') or j.get('pdf') or
                    (j.get('status')=='Düzenlemeye hazır' and j.get('project') and Path(j['project']).is_file()))
                errors=sum(bool(j.get('error')) for j in preceding)
            else:
                complete=lambda j: bool(j.get('pdf') and not j.get('error'))
            if any(not complete(j) for j in preceding):
                raise ValueError('Devam noktasından önce tamamlanmamış kitap var.')
            jobs=jobs[start:]
        pending_edits=[j for j in self.data['jobs'] if j.get('edit_request_id') and j.get('edit_completed_id')!=j['edit_request_id']]
        if self.request.get('revision_only'):jobs=pending_edits
        else:
            pending_ids={j['id'] for j in pending_edits}
            jobs=pending_edits+[j for j in jobs if j['id'] not in pending_ids]
        from .revision_queue import accept_revisions,revision_export_options
        while True:
            if getattr(self,'store',None):
                added,command_errors=self.store.apply_commands(self.data)
                present={j['id'] for j in self.data['jobs']};jobs=[j for j in jobs if j['id'] in present]
                queued={j['id'] for j in jobs}
                jobs.extend(j for j in self.data['jobs'] if j['id'] in added and j['id'] not in queued)
                jobs.sort(key=lambda j:-j.get('priority',0))
                for error in command_errors:self.emit('Kuyruk isteği hatası',error)
            revised,revision_errors=accept_revisions(self.data,self.queue_path,(lambda:self.save(force=True)) if getattr(self,'store',None) else self.save)
            for error in revision_errors:self.emit('Düzeltme isteği hatası',error)
            if revised:
                jobs=[j for j in self.data['jobs'] if j['id'] in revised]+[j for j in jobs if j['id'] not in revised]
            if not jobs:break
            job=jobs.pop(0)
            self.job=job
            if getattr(self,'metrics',None):
                from .telemetry import Metrics
                self.metrics=Metrics()
            self._revision_pause_bypass=(self.revision_in_progress() and self.control_state()=='pause' and job.get('edit_run_while_paused',True))
            original_request,original_settings=self.request,self.settings
            self.request=revision_export_options(original_request,job)
            if self.request is not original_request:
                self.settings={**original_settings,**{k:self.request[k] for k in ('paper','margin_mm','target_x_mm','language','appearance','ocr_engine','ocr_device','paddle_language','tess_quality','page_cleanup') if k in self.request}}
                timing=self.request.get('ocr_timing','during')
                if timing not in ('during','after','none'):raise ValueError('Bilinmeyen OCR zamanı')
                self.settings.pop('ocr_timing',None)
                if timing!='during':self.settings['ocr_timing']=timing
                self.settings.pop('output_finish',None);self.settings.pop('output_finish_code',None)
                finish=self.request.get('output_finish',{})
                if any(finish.get(k,False) for k in ('lines','white','fine')):
                    self.settings['output_finish']={k:bool(finish.get(k)) for k in ('lines','white','fine')}
                    self.settings['output_finish_code']=file_hash(Path(__file__).with_name('output_finish.py'))
                self.settings.pop('local_soft_profile',None)
                if self.request.get('appearance')=='soft_local':
                    from .output_appearance import LOCAL_SOFT_VERSION
                    self.settings['local_soft_profile']=LOCAL_SOFT_VERSION
                if hasattr(self,'root'):
                    self.settings['ocr_models']={code:file_hash(path) for code in self.request.get('language','deu').split('+') if (path:=resource_path('tessdata_'+self.request.get('tess_quality','best'),self.root)/(code+'.traineddata')).is_file()}
            try:
                job.pop('error',None)
                self.checkpoint();self.emit('Hazırlanıyor',job['title']);self.save()
                Path(self.data['output_root']).mkdir(parents=True,exist_ok=True)
                self.checkpoint()
                folder,project,out=self.prepare()
                before={str(p):file_hash(p) for p in source_paths(project)}
                if self.request['mode']=='prepare':
                    if not getattr(self,'_prepared_in_pass',False):
                        self.native(['--end-filter=5','--output-project='+str(project),project,out])
                    self.emit('Düzenlemeye hazır','Bölme, geometri, içerik ve kenarlar Experimental içinde düzenlenebilir.')
                else:
                    self.export(folder,project,out)
                    self.finalize_layout(folder)
                    self.finish_book(folder)
                    self.publish_book(folder)
                if any(file_hash(p)!=sha for p,sha in before.items()):raise ValueError('İşlem sırasında kaynak dosya değişmiş.')
                if self.request.get('reclaim_intermediates',False) and job.get('pdf'):
                    from .work_cleanup import reclaim
                    job['reclaimed']=reclaim(folder,self.request.get('work_root') or self.data['output_root'],job)
                if job.get('edit_request_id') and self.request['mode']!='prepare':
                    job['edit_completed_id']=job['edit_request_id']
                job.pop('resource_retry',None)
                # Optional first-book QA checkpoint. Pause only after the
                # complete PDF, layout verification and durable page receipts.
                if self.request.get('pause_after_job')==job['id'] and not getattr(self,'_qa_pause_done',False):
                    self._qa_pause_done=True
                    write_json(self.control,{'state':'pause'})
                    self.emit('Kitap kontrol için hazır','PDF tamamlandı. Sonraki kitap öncesinde kuyruk duraklatıldı.')
            except RevisionPending:
                self.emit('PDF düzeltmesi öncelikli','Tamamlanan sayfalar saklandı; kaydettiğiniz düzenleme şimdi işlenecek.')
                jobs.insert(0,job)
            except Cancelled:
                self.session_state='stopped';self.emit('Durduruldu','Tamamlanan sayfalar korunuyor.');self.save();return 2
            except Exception as exc:
                from .memory_budget import is_memory_failure
                from .recovery import classify
                if classify(exc)=='disk':
                    self._space_checked=0;self._space_pressure=False
                    job['resource_retry']=True;jobs.insert(0,job)
                    self.emit('Disk alanı bekleniyor',str(exc))
                    continue
                if is_memory_failure(exc):
                    # Resource exhaustion affects every later book as well.
                    # Stop at this checkpoint instead of marking the rest of
                    # the library failed in a cascade.
                    import gc
                    gc.collect()
                    self.session_state='stopped'
                    job['resource_retry']=True
                    self.emit('Bellek için durdu','Bellek yetersiz kaldı; tamamlanan sayfalar korundu. Bellek boşalınca PDF oluştur / devam et ile bu kitaptan sürdürülebilir.')
                    job['error']=str(exc) or type(exc).__name__
                    return 3
                errors+=1;self.emit('Hata',str(exc));job['error']=str(exc)
            finally:
                self.request,self.settings=original_request,original_settings
                self.save()
        self.session_state='finished';self.job=None;self.emit('Kuyruk tamamlandı',f'{len(self.data["jobs"])} kitap · {errors} hata')
        return 1 if errors else 0

    def finish_book(self,folder):
        """Optional derivatives never replace the verified reading PDF."""
        job=self.job
        if not job.get('pdf') or not job.get('verification'):return
        from .pdf_integrity import file_digest
        if file_digest(job['pdf'])!=job.get('pdf_hash'):
            raise ValueError('Doğrulanmış PDF uygulama dışında değişti; PDF çıktısını yeniden oluşturun.')
        if self.request.get('structured_text',True) and not job.get('text_exports',{}).get('pdf_hash')==job.get('pdf_hash'):
            from .quality import export_text
            target=Path(folder)/('text-'+job['pdf_hash'][:12])
            if target.exists():target=Path(folder)/('text-'+uuid.uuid4().hex[:12])
            self.emit('Metin dışa aktarılıyor','TXT, Markdown, ALTO ve okuma sırası kutuları.')
            job['text_exports']={'pdf_hash':job['pdf_hash'],**export_text(job['pdf'],target)}
        profile=self.request.get('compression_profile','lossless')
        from .pdf_profiles import compress,options_signature
        compression_options=self.request.get('compression_options') or {}
        needs_copy=profile!='lossless' or compression_options.get('recompress_flate',False)
        signature=options_signature(profile,compression_options)
        previous=job.get('compressed',{})
        reusable=(previous.get('source_hash')==job['pdf_hash'] and previous.get('options_signature')==signature
                  and Path(previous.get('pdf','')).is_file())
        if reusable:
            reusable=file_digest(previous['pdf'])==previous.get('output_sha256')
        if needs_copy and not reusable:
            target=next_path(folder,safe_name(job['title'])+' - '+profile,'.pdf')
            def compression_checkpoint():
                self.checkpoint()
                return False
            def compression_progress(event):
                self.emit(event.get('stage','Sıkıştırılıyor'),f"{event.get('page',0)}/{event.get('total',0)}")
            job['compressed']={**compress(job['pdf'],target,profile,compression_progress,
                                          options=compression_options,cancelled=compression_checkpoint),
                               'source_hash':job['pdf_hash']}
        job['reading_pdf']=job['compressed']['pdf'] if needs_copy else job['pdf']
        if self.request.get('output_format')=='pdfa' and job.get('pdfa',{}).get('source_hash')!=job['pdf_hash']:
            from .archive_pdf import convert_pdfa
            target=next_path(folder,safe_name(job['title'])+' - PDFA','.pdf')
            job['pdfa']={**convert_pdfa(job['pdf'],target,self.request),'source_hash':job['pdf_hash']}
        if getattr(self,'metrics',None):
            job['metrics']=self.metrics.report();write_json(Path(folder)/'metrics.json',job['metrics'])
        from .storage import cleanup_plan,apply_cleanup
        try:
            cleanup=cleanup_plan(getattr(self,'root',None))
            if cleanup['files']:job['cache_cleanup']=apply_cleanup(cleanup,getattr(self,'root',None))
        except (OSError,ValueError) as exc:job['cache_cleanup_note']=str(exc)
        self.emit('Kontrol gerekli' if job.get('warnings') else 'Tamamlandı',
            f'{len(job.get("pages",[]))} sayfa · PDF ve seçilen ek çıktılar hazır.',preview=job['reading_pdf'])

def main():
    request=json.loads(Path(sys.argv[1]).read_text(encoding='utf8'))
    request['request_path']=str(Path(sys.argv[1]).resolve())
    from .queue_lock import exclusive_queue
    with exclusive_queue(request['queue']):
        worker=Worker(request)
        try:return worker.run()
        finally:
            try:worker.store.publish(worker.data)
            finally:worker.store.close()

if __name__=='__main__':raise SystemExit(main())
