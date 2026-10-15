"""Offline, synthetic acceptance run of the *installed* CPU package.

Run with its bundled interpreter and STOU_ROOT / STOU_DATA set to fresh paths.
No user books or external models are read. Results are written to --work.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',required=True);parser.add_argument('--work',required=True);parser.add_argument('--ai',action='store_true');parser.add_argument('--fast',action='store_true');a=parser.parse_args()
    root=Path(a.root).resolve();work=Path(a.work).resolve();work.mkdir(parents=True,exist_ok=True)
    os.environ.update(STOU_ROOT=str(root),STOU_DATA=str(work/'user'),STOU_CPU_ONLY='1',STOU_SETTINGS_NAME='scantailor-release-cpu-test',CUDA_VISIBLE_DEVICES='-1',PYTHONNOUSERSITE='1',PYTHONPATH=str(root/'src'),OMP_NUM_THREADS='2',OMP_THREAD_LIMIT='2',QT_QPA_PLATFORM='offscreen')
    sys.path.insert(0,str(root/'src'))
    from scantailor_ocr_unified.runtime_layout import initialize
    initialize(root)
    from PIL import Image,ImageDraw,ImageFont
    import fitz
    source=work/'Fotograflar';source.mkdir(exist_ok=True)
    for n in (1,2):
        page=Image.new('RGB',(1240,1754),(242,240,231));draw=ImageDraw.Draw(page)
        title=ImageFont.load_default(size=38);font=ImageFont.load_default(size=24);small=ImageFont.load_default(size=18)
        draw.text((92,90),'ScanTailor OCR Unified',font=title,fill=(25,25,25))
        for line in range(36):draw.text((92,170+line*36),f'This is sample text number {line+1}. The original page is preserved.',font=font,fill=(35,35,35))
        draw.line((92,1520,460,1520),fill=(20,20,20),width=2)
        draw.text((92,1542),'42 A small footnote must stay visible and readable.',font=small,fill=(25,25,25))
        draw.text((610,1650),str(n),font=font,fill=(20,20,20))
        page.save(source/f'page{n}.png')
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()}
    queue=work/'queue.json';control=work/'control.json'
    queue.write_text(json.dumps({'schema':1,'output_root':str(work/'books'),'jobs':[{'id':'cpu-test-043','title':'CPU Deneme','folder':str(source),'split':'1','status':'Bekliyor'}]}),'utf8')
    control.write_text('{"state":"run"}','utf8')
    cli=root/('native/bin/scantailor-experimental-cli'+('.exe' if os.name=='nt' else ''))
    request={'root':str(root),'cli':str(cli),'queue':str(queue),'control':str(control),'mode':'export','language':'eng','appearance':'soft_local','ocr_timing':'during','paper':'a4_text','margin_mm':10,'threads':2,'auto_warp':False,'native_scale':2,'page_cleanup':True,'ocr_engine':'tesseract','ocr_device':'cpu','tess_quality':'best','output_finish':{'lines':True,'white':True,'fine':True}}
    if a.fast:
        from scantailor_ocr_unified.settings import PROFILES
        request.update(PROFILES['cpu_fast'],output_finish={})
    request['reclaim_intermediates']=True
    req=work/'request.json';req.write_text(json.dumps(request),'utf8');start=time.monotonic()
    with (work/'worker.log').open('w',encoding='utf8') as log:
        subprocess.run([sys.executable,'-u','-m','scantailor_ocr_unified.native_worker',str(req)],env=os.environ,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
    data=json.loads(queue.read_text('utf8'));job=data['jobs'][0]
    assert not job.get('error'),job
    assert job['status'] in ('Tamamlandı','Kontrol gerekli'),job['status']
    from scantailor_ocr_unified.queue_store import read_pages
    for row in read_pages(queue,job['id'])['pages']:
        assert hashlib.sha256(Path(row['page_pdf']).read_bytes()).hexdigest()==row['pdf_sha256']
    with fitz.open(job['pdf']) as pdf:
        assert len(pdf)==2
        chars=[len(p.get_text()) for p in pdf]
        assert min(chars)>900,chars
        for n,page in enumerate(pdf):page.get_pixmap(matrix=fitz.Matrix(1,1)).save(work/f'pdf-page-{n+1}.png')
    assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    result={'pdf':job['pdf'],'pages':2,'ocr_chars':chars,'seconds':round(time.monotonic()-start,2),'gpu_enabled':False,'source_unchanged':True,'profile':'cpu_fast' if a.fast else 'quality'}
    if a.fast:
        assert Path(job['visual_pdf']).is_file(),job
        with fitz.open(job['visual_pdf']) as visual:assert len(visual)==2 and not any(p.get_text().strip() for p in visual)
        assert job['verification']['ocr_timing']=='after'
        result['image_pdf_before_ocr']=True
    if a.ai:
        from scantailor_ocr_unified.ocr_backend import PaddleClient
        client=PaddleClient(root,'cpu')
        try:
            recognized=client.recognize(source/'page1.png','en','cpu');assert len(recognized['lines'])>25,recognized
            result['paddle_cpu']={k:recognized.get(k) for k in ('seconds','detector','detector_limit')}
            result['paddle_cpu']['lines']=len(recognized['lines'])
        finally:client.close()
        # Same CPU model as the geometry stage; output is coordinates, never
        # generated letters. Visual review still decides whether to apply it.
        payload={'id':1,'image':str(source/'page1.png'),'device':'cpu'}
        run=subprocess.run([sys.executable,str(root/'src/scantailor_ocr_unified/geometry_runtime.py')],input=json.dumps(payload)+'\n',text=True,encoding='utf8',stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=150,check=True)
        records=[json.loads(l[13:]) for l in run.stdout.splitlines() if l.startswith('STOUGEOMETRY ')]
        assert records and records[-1].get('ok'),(run.stdout,run.stderr)
        result['uvdoc_cpu']={k:records[-1].get(k) for k in ('ok','device','seconds','folds','area_ratio')}
        run=subprocess.run([sys.executable,str(root/'src/scantailor_ocr_unified/paddle_visual_runtime.py')],input=json.dumps({'id':1,'image':str(source/'page1.png'),'device':'cpu','layout':True})+'\n',text=True,encoding='utf8',stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=150,check=True)
        records=[json.loads(l[7:]) for l in run.stdout.splitlines() if l.startswith('STOUAI ')]
        assert records and records[-1].get('ok'),(run.stdout,run.stderr)
        result['layout_cpu']={'boxes':len(records[-1].get('boxes',[])),'regions':len(records[-1].get('regions',[]))}
    (work/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');print(json.dumps(result,ensure_ascii=False))

if __name__=='__main__':main()
