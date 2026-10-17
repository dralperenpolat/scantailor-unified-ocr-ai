"""Verify a second desktop launch forwards a saved run to the existing GUI."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import uuid


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',required=True)
    parser.add_argument('--work',required=True)
    args=parser.parse_args()
    root=Path(args.root).resolve();work=Path(args.work).resolve();work.mkdir(parents=True,exist_ok=True)
    (work/'preferences.json').write_text('{"schema":1,"setup_complete":true}',encoding='utf8')
    saved=work/'saved run.json'
    saved.write_text(json.dumps({'schema':1,'jobs':[{'id':'handoff','title':'Recovered run'}]}),encoding='utf8')
    exe=root/('native/bin/scantailor-ocr-unified'+('.exe' if os.name=='nt' else ''))
    env=dict(os.environ,STOU_ROOT=str(root),STOU_DATA=str(work),STOU_QUEUE_PATH=str(work/'empty.json'),
        STOU_SETTINGS_NAME='scantailor-instance-test-'+uuid.uuid4().hex,STOU_MONITOR_REQUEST='',STOU_SKIP_SETUP='1',
        QT_QPA_PLATFORM='offscreen',STOU_SMOKE_SCREENSHOT=str(work/'handoff.png'))
    events=queue.Queue();lines=[]
    primary=subprocess.Popen([str(exe)],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace')
    def collect():
        for line in primary.stdout:lines.append(line);events.put(line.strip())
    reader=threading.Thread(target=collect,daemon=True);reader.start()
    try:
        for _ in range(200):
            if events.get(timeout=30)=='STOUINSTANCE_READY':break
        else:raise AssertionError('primary GUI did not report ready')
        for arguments in ([str(saved)],[]):
            result=subprocess.run([str(exe),*arguments],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=8)
            assert result.returncode==0,result.stdout
        assert primary.wait(timeout=20)==0
        reader.join(timeout=2)
        received=[line[len('STOUINSTANCE_FILE '):].rstrip('\r\n') for line in lines if line.startswith('STOUINSTANCE_FILE ')]
        assert len(received)==2 and Path(received[0])==saved and received[1]=='',received
        assert (work/'handoff.png').is_file()
        report={'saved_run_forwarded':True,'repeat_launch_forwarded_to_existing_window':True,'second_launch_exits_cleanly':True}
        (work/'result.json').write_text(json.dumps(report,indent=2),encoding='utf8')
        print(json.dumps(report))
    finally:
        if primary.poll() is None:primary.kill();primary.wait()
        (work/'process.log').write_text(''.join(lines),encoding='utf8')


if __name__=='__main__':main()
