"""Consume immutable editor snapshots at book boundaries, with one queue writer."""
from pathlib import Path
import json,re,os,copy
from .project import file_hash

def accept_revisions(data,queue_path,save):
    inbox=Path(str(queue_path)+'.edits').resolve()
    if not inbox.is_dir():return [],[]
    accepted=[];errors=[]
    for path in sorted(inbox.glob('*.request.json'),key=lambda p:p.stat().st_mtime_ns):
        try:
            request=json.loads(path.read_text('utf8'));token=request['id']
            if not re.fullmatch(r'[A-Za-z0-9-]{1,80}',token):raise ValueError('Geçersiz düzeltme kimliği.')
            job=next((j for j in data['jobs'] if j['id']==request['job_id']),None)
            if job is None:raise ValueError('Düzeltilecek kitap kuyrukta bulunamadı.')
            snapshot=Path(request['snapshot']).resolve()
            if not snapshot.is_relative_to(inbox) or not snapshot.is_file() or file_hash(snapshot)!=request['sha256']:
                raise ValueError('Kaydedilen düzenleme kopyası doğrulanamadı.')
        except (OSError,ValueError,KeyError,TypeError) as exc:
            errors.append(str(exc))
            # Keep invalid input reviewable, without retrying it forever.
            try:os.replace(path,path.with_name(path.stem+'.rejected.json'))
            except OSError:pass
            continue
        if token not in job.get('accepted_edits',[]):
            before=copy.deepcopy(job)
            try:
                # Repeated clicks may submit the same saved project under a new
                # request ID. Acknowledge it without invalidating work again.
                identical=(job.get('edit_snapshot_sha256')==request['sha256'] and
                           job.get('preserve_edit_margins',True)==bool(request.get('preserve_margins',True)) and
                           job.get('edit_export_options',{})==request.get('export_options',{}))
                if identical:
                    job.setdefault('accepted_edits',[]).append(token)
                    save()
                    try:os.replace(path,path.with_name(token+'.accepted.json'))
                    except OSError as exc:errors.append(str(exc))
                    continue
                previous={k:job[k] for k in ('pdf','pdf_hash','project','work','warnings','verification','edit_request_id') if k in job}
                if previous.get('pdf') and not any(p.get('pdf')==previous['pdf'] for p in job.get('previous_editions',[])):
                    job.setdefault('previous_editions',[]).append(previous)
                for key in ('project','work','signature','native_cache','layout_revision','reclaimed','preparing','error','verification'):
                    job.pop(key,None)
                job.update(source_project=str(snapshot),revision_token=token,edit_request_id=token,edit_snapshot_sha256=request['sha256'],
                           preserve_edit_margins=bool(request.get('preserve_margins',True)),
                           edit_run_while_paused=bool(request.get('run_while_paused',True)),
                           edit_export_options=request.get('export_options',{}),
                           status='Düzeltme bekliyor',detail='Kaydedilen düzenleme yeni PDF olarak işlenecek.',completed_pages=0)
                job.setdefault('accepted_edits',[]).append(token)
                save()
            except Exception:
                # Persistence failure must leave a pending request and the old job.
                job.clear();job.update(before)
                raise
            accepted.append(job['id'])
        try:
            os.replace(path,path.with_name(token+'.accepted.json'))
        except OSError as exc:
            errors.append(str(exc))
            # Already durable; a restart can acknowledge the same ID safely.
    return list(dict.fromkeys(accepted)),errors

def revision_export_options(request,job):
    """Preserve the editor's full page canvas instead of cropping its margins again."""
    if not job.get('edit_request_id'):return request
    allowed={'appearance','paper','margin_mm','target_x_mm','content_fit','language','ocr_engine','ocr_device','paddle_language','tess_quality','ocr_timing','output_finish','page_cleanup'}
    options={k:v for k,v in job.get('edit_export_options',{}).items() if k in allowed}
    if options:request={**request,**options}
    if not job.get('preserve_edit_margins',True):return request
    return {**request,'paper':'native' if request.get('paper')=='native' else 'a4',
            'margin_mm':0.,'target_x_mm':0.,'content_fit':False}
