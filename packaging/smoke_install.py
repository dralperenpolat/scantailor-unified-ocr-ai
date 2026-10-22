"""Run on disposable CI runners: install, upgrade, uninstall, preserve user data."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess


def verify(app):
    manifest=json.loads((app/'FILES-SHA256.json').read_text('utf8'))
    for name,expected in manifest.items():
        with (app/name).open('rb') as stream:assert hashlib.file_digest(stream,'sha256').hexdigest()==expected,name
    assert json.loads((app/'release.json').read_text('utf8'))['version']=='0.6.2'
    return len(manifest)


def main():
    p=argparse.ArgumentParser();p.add_argument('--work',required=True);p.add_argument('--stage');p.add_argument('--installer');p.add_argument('--previous',required=True);a=p.parse_args()
    work=Path(a.work).resolve();work.mkdir(parents=True,exist_ok=True)
    marker=work/'user-data/projects/keep.ScanTailor';marker.parent.mkdir(parents=True,exist_ok=True);marker.write_text('user-owned project','utf8')
    env={**os.environ,'STOU_DATA':str(work/'user-data'),'STOU_SKIP_SETUP':'1','PYTHONDONTWRITEBYTECODE':'1'}
    if os.name=='nt':
        # CI only: the release AppId modifies the runner's installer registry.
        if not os.environ.get('CI'):raise RuntimeError('Windows installer smoke test requires an isolated CI runner')
        app=work/'installed'
        for installer in (a.previous,a.installer):
            subprocess.run([installer,'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',f'/DIR={app}'],env=env,check=True,timeout=600,creationflags=subprocess.CREATE_NO_WINDOW)
        count=verify(app)
        subprocess.run([str(app/'unins000.exe'),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART'],env=env,check=True,timeout=300,creationflags=subprocess.CREATE_NO_WINDOW)
    else:
        env['HOME']=str(work/'home');Path(env['HOME']).mkdir(exist_ok=True)
        env['XDG_DATA_HOME']=str(work/'xdg');app=work/'xdg/scantailor-ocr-unified-app'
        subprocess.run(['sh',str(Path(a.previous)/'install-user.sh')],env=env,check=True)
        assert (app/'release.json').is_file()
        subprocess.run(['sh',str(Path(a.stage)/'install-user.sh')],env=env,check=True)
        count=verify(app)
        subprocess.run(['sh',str(app/'uninstall-user.sh')],env=env,check=True)
    assert marker.read_text('utf8')=='user-owned project'
    assert not (app/'native/bin'/('scantailor-ocr-unified.exe' if os.name=='nt' else 'scantailor-ocr-unified')).exists()
    result={'install':True,'upgrade_from':'0.4.5','uninstall':True,'user_project_preserved':True,'manifest_files_verified':count}
    (work/'install-result.json').write_text(json.dumps(result,indent=2),'utf8');print(json.dumps(result))


if __name__=='__main__':main()
