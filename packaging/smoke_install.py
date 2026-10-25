"""Run on disposable CI runners: install, upgrade, uninstall, preserve user data."""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import os
import subprocess


def shortcuts(app, create=False, desktop=None):
    script=r'''
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false)
$ws=New-Object -ComObject WScript.Shell
$desktop=$env:STOU_TEST_DESKTOP
if (!$desktop) { $desktop=[Environment]::GetFolderPath('Desktop') }
if ($env:STOU_TEST_CREATE_LINKS -eq '1') {
    foreach ($name in @('ScanTailor OCR Unified.lnk','ScanTailor - Previous work.lnk')) {
        $link=$ws.CreateShortcut((Join-Path $desktop $name))
        $link.TargetPath=Join-Path $env:STOU_TEST_INSTALL_APP 'native\bin\scantailor-ocr-unified.exe'
        $link.WorkingDirectory=$env:STOU_TEST_INSTALL_APP
        $link.Save()
    }
}
$items=@(Get-ChildItem -LiteralPath $desktop -Filter '*Scan*Tailor*.lnk' | ForEach-Object {
    $link=$ws.CreateShortcut($_.FullName)
    [pscustomobject]@{name=$_.Name;target=$link.TargetPath}
})
ConvertTo-Json -InputObject $items -Compress
'''
    # -Command - reads stdin interactively and may leave multiline blocks unevaluated.
    command=base64.b64encode(script.encode('utf-16-le')).decode('ascii')
    result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-EncodedCommand',command],text=True,encoding='utf8',capture_output=True,check=True,timeout=30,
                          env={**os.environ,'STOU_TEST_INSTALL_APP':str(app),'STOU_TEST_CREATE_LINKS':'1' if create else '0','STOU_TEST_DESKTOP':str(desktop) if desktop is not None else ''},creationflags=subprocess.CREATE_NO_WINDOW)
    if not result.stdout.strip():raise RuntimeError('Shortcut inspection returned no JSON: '+result.stderr.strip())
    return json.loads(result.stdout.lstrip('\ufeff').strip())


def verify(app):
    manifest=json.loads((app/'FILES-SHA256.json').read_text('utf8'))
    for name,expected in manifest.items():
        with (app/name).open('rb') as stream:assert hashlib.file_digest(stream,'sha256').hexdigest()==expected,name
    assert json.loads((app/'release.json').read_text('utf8'))['version']=='0.6.5'
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
        def install(path,check=True,log=None):
            return subprocess.run([str(path),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/LANG=english',f'/DIR={app}',*([f'/LOG={log}'] if log else [])],env=env,check=check,timeout=600,creationflags=subprocess.CREATE_NO_WINDOW)
        install(a.previous)
        shortcuts(app,True)
        import winreg
        saved=marker.parent/'Native_Kitaplar/kuyruk.json';saved.parent.mkdir();saved.write_text(json.dumps({'schema':2,'jobs':[{'id':'preserve-book','status':'Düzenlemeye hazır'}]}),encoding='utf8')
        queue_bytes=saved.read_bytes()
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\Scan Tailor\scantailor-ocr-unified-063\unified') as key:
            winreg.SetValueEx(key,'last_queue',0,winreg.REG_SZ,str(saved))
            winreg.SetValueEx(key,'ocr_languages',0,winreg.REG_SZ,'deu+eng')
        binary=app/'native/bin/scantailor-ocr-unified.exe';before=hashlib.sha256(binary.read_bytes()).hexdigest()
        worker=subprocess.Popen([str(app/'runtime/python.exe'),'-u','-c',"import sys; print('ready',flush=True); sys.stdin.read()"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            assert worker.stdout.readline().strip()==b'ready'
            blocked=work/'running-upgrade.log'
            assert install(a.installer,False,blocked).returncode!=0,'Running installation must block replacement'
            assert 'running from this installation folder' in blocked.read_text('utf8',errors='replace')
            assert worker.poll() is None and hashlib.sha256(binary.read_bytes()).hexdigest()==before
        finally:
            worker.terminate();worker.wait(timeout=10);worker.stdout.close();worker.stdin.close()
        install(a.installer,log=work/'upgrade.log')
        count=verify(app)
        links=shortcuts(app)
        assert len(links)==1 and links[0]['name']=='ScanTailor OCR Unified.lnk',links
        assert Path(links[0]['target'])==binary,links
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\Scan Tailor\scantailor-ocr-unified\unified') as key:
            assert Path(winreg.QueryValueEx(key,'data_root')[0])==work/'user-data'
            assert Path(winreg.QueryValueEx(key,'last_queue')[0])==saved
            assert winreg.QueryValueEx(key,'ocr_languages')[0]=='deu+eng'
        assert saved.read_bytes()==queue_bytes
        install(a.installer,log=work/'repeat-upgrade.log')
        assert len(shortcuts(app))==1,'Repeated installation must not duplicate shortcuts'
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
    if os.name=='nt':result.update(single_desktop_shortcut=True,repeated_upgrade=True,running_worker_protected=True,legacy_queue_and_settings_preserved=True)
    (work/'install-result.json').write_text(json.dumps(result,indent=2),'utf8');print(json.dumps(result))


if __name__=='__main__':main()
