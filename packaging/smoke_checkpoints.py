"""Exercise real native pause/cancel durability with throttled project saves."""
from pathlib import Path
import argparse,json,os,subprocess,sys,threading,time


def main():
    p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--work',required=True);a=p.parse_args()
    root=Path(a.root).resolve();work=Path(a.work).resolve();work.mkdir(parents=True,exist_ok=True)
    sys.path.insert(0,str(root/'src'))
    from PIL import Image,ImageDraw
    from scantailor_ocr_unified.native_resume import saved_checkpoint
    from scantailor_ocr_unified.native_worker import write_json as atomic_json
    image=Image.new('RGB',(1000,1500),'white');draw=ImageDraw.Draw(image)
    for i in range(35):draw.text((90,90+35*i),'Durable checkpoint page text.',fill='black',font_size=24)
    images=[]
    for i in range(30):
        path=work/f'page-{i:03}.png';image.save(path,dpi=(150,150));images.append(path)
    inputs=work/'inputs.json';atomic_json(inputs,list(map(str,images)))
    project=work/'book.ScanTailor';out=work/'out';out.mkdir(exist_ok=True);control=work/'control.json'
    env={**os.environ,'STOU_ROOT':str(root),'STOU_DATA':str(work/'data'),'QT_QPA_PLATFORM':'offscreen',
         'STOU_SETTINGS_NAME':'scantailor-checkpoint-test','STOU_CPU_ONLY':'1','STOU_PROGRESS':'1',
         'STOU_CHECKPOINT_MS':'60000','STOU_CHECKPOINT_TOKEN':'test-token','STOU_CONTROL_FILE':str(control),
         'OMP_NUM_THREADS':'2','OMP_THREAD_LIMIT':'2'}
    cli=root/('native/bin/scantailor-experimental-cli'+('.exe' if os.name=='nt' else ''))
    def run(cancel=False):
        atomic_json(control,{'state':'run'})
        args=[str(cli),'--end-filter=1','--layout=1','--output-project='+str(project)]
        args += [str(project),str(out)] if cancel else ['--input-list='+str(inputs),str(out)]
        child=subprocess.Popen(args,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                               creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        timer=threading.Timer(60,child.kill);timer.start();requested=False;paused=False;last=None;events=[]
        try:
            for raw in iter(child.stdout.readline,b''):
                line=raw.decode('utf8',errors='replace').strip()
                if not line.startswith('STOU '):continue
                event=json.loads(line[5:]);events.append(event)
                if event.get('stage')=='Fix Orientation' and not requested:
                    atomic_json(control,{'state':'cancel' if cancel else 'pause'});requested=True
                if event.get('stage')=='Duraklatıldı':
                    last=saved_checkpoint(project)
                    assert last and last['stage']==0 and 0<=last['page']<30,last
                    assert last['token']=='test-token',last
                    snapshot=project.read_bytes();time.sleep(.2);assert snapshot==project.read_bytes()
                    paused=True;atomic_json(control,{'state':'run'})
            code=child.wait(timeout=10)
        finally:
            timer.cancel()
            if child.poll() is None:child.kill();child.wait()
            child.stdout.close()
        cursor=saved_checkpoint(project)
        if cancel:
            assert code!=0 and requested and cursor['page']<30,(code,cursor)
        else:
            assert code==0 and paused and cursor['page']==30,(code,paused,cursor,events)
            assert sum('checkpoint' in e for e in events)<10,'Full project still saved per page'
        return cursor
    complete=run();cancelled=run(True)
    print(json.dumps({'pause_durable':True,'cancel_durable':True,'completed':complete,'cancelled':cancelled}))


if __name__=='__main__':main()
