"""Verify native page resume and PDF revisions in a synthetic preparation queue."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',required=True);parser.add_argument('--work',required=True);a=parser.parse_args()
    root=Path(a.root).resolve();work=Path(a.work).resolve();work.mkdir(parents=True,exist_ok=True)
    os.environ.update(STOU_ROOT=str(root),STOU_DATA=str(work/'user'),STOU_CPU_ONLY='1',QT_QPA_PLATFORM='offscreen',
        STOU_SETTINGS_NAME='scantailor-resume-test',PYTHONPATH=str(root/'src'),PYTHONNOUSERSITE='1',OMP_NUM_THREADS='2')
    sys.path.insert(0,str(root/'src'))
    from PIL import Image,ImageDraw
    from scantailor_ocr_unified.native_worker import load_queue,write_json
    from scantailor_ocr_unified.project import file_hash
    source=work/'images';source.mkdir(exist_ok=True)
    for number in range(3):
        image=Image.new('RGB',(640,900),'white');draw=ImageDraw.Draw(image)
        for line in range(24):draw.text((70,70+line*28),f'Resumed book page {number+1}. Saved text line {line+1}.',fill='black',font_size=18)
        image.save(source/f'{number+1}.png',dpi=(150,150))
    original={p:file_hash(p) for p in source.iterdir()}
    queue=work/'queue.json';control=work/'control.json';req=work/'request.json'
    write_json(queue,{'schema':1,'output_root':str(work/'books'),'jobs':[{'id':'resume-book','title':'Resume test','folder':str(source),'split':'1','status':'Bekliyor'}]})
    write_json(control,{'state':'run'})
    cli=root/('native/bin/scantailor-experimental-cli'+('.exe' if os.name=='nt' else ''))
    request={'root':str(root),'cli':str(cli),'queue':str(queue),'control':str(control),'mode':'prepare',
        'language':'eng','ocr_engine':'tesseract','ocr_device':'cpu','ocr_timing':'none','appearance':'soft_local',
        'paper':'native','threads':2,'auto_warp':False,'page_cleanup':False,'reclaim_intermediates':False}
    def run(name):
        write_json(req,request)
        with (work/(name+'.log')).open('w',encoding='utf8') as log:
            subprocess.run([sys.executable,'-u','-m','scantailor_ocr_unified.native_worker',str(req)],
                stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
        return [json.loads(line) for line in (work/(name+'.log')).read_text('utf8').splitlines() if line.startswith('{')]
    events=run('prepare');job=load_queue(queue)['jobs'][0];project=Path(job['project'])
    assert job['status']=='Düzenlemeye hazır' and not job.get('pdf'),job
    assert [e['page'] for e in events if e.get('stage')=='Fix Orientation']==[0,1,1,2,2,3],events
    request.update(resume_at_job=job['id'],resume_native={'job_id':job['id'],'project':str(project),'sha256':file_hash(project),'stage':3,'page':1})
    events=run('resume')
    assert not any(e.get('stage') in ('Fix Orientation','Split Pages','Deskew') for e in events),events
    assert [e['page'] for e in events if e.get('stage')=='Select Content']==[1,2,2,3],events
    assert [e['page'] for e in events if e.get('stage')=='Margins']==[0,1,1,2,2,3],events
    inbox=Path(str(queue)+'.edits');inbox.mkdir(exist_ok=True);snapshot=inbox/'manual.ScanTailor';shutil.copy2(project,snapshot)
    write_json(inbox/'manual.request.json',{'id':'manual','job_id':job['id'],'snapshot':str(snapshot),'sha256':file_hash(snapshot),'export_options':{'ocr_timing':'none'}})
    request.pop('resume_native');request.pop('resume_at_job');request['revision_only']=True
    run('revision');revised=load_queue(queue)['jobs'][0]
    assert revised['edit_completed_id']=='manual' and Path(revised['pdf']).is_file(),revised
    import fitz
    with fitz.open(revised['pdf']) as pdf:assert len(pdf)==3
    assert all(file_hash(p)==digest for p,digest in original.items())
    print(json.dumps({'prepare_passes':1,'resumed_stage':'Select Content','skipped_pages':1,'following_stage_pages':3,
        'prepare_revision_pdf_pages':3,'source_unchanged':True}))

if __name__=='__main__':main()
