"""Migrate legacy portable profiles into the stable installed application.

Only settings are copied. Existing queues, projects, PDFs and running workers
stay where they are. The native EXE and Python bridge share the data_root setting.
"""
from __future__ import annotations
import argparse
import base64
import datetime
import json
import os
from pathlib import Path
import re

APP = 'scantailor-ocr-unified'
REGISTRY = r'Software\Scan Tailor'


def read_json(path):
    try:
        value=json.loads(Path(path).read_text('utf8'))
        return value if isinstance(value,dict) else {}
    except (OSError,ValueError,TypeError):
        return {}


def valid_queue(path):
    value=read_json(path)
    return value.get('schema') in (1,2) and isinstance(value.get('jobs'),list)


def data_directory(profile, base):
    values=profile['values'];saved=values.get(r'unified\data_root','')
    if saved and Path(saved).is_absolute():return Path(saved)
    queue=Path(values.get(r'unified\last_queue',''))
    if queue.is_absolute() and queue.parent.parent.name.lower()=='projects':
        return queue.parent.parent.parent
    candidate=base/profile['name']
    return candidate if candidate.is_dir() else base/APP


def recover_run(profile, base, alive):
    values=dict(profile['values']);data=data_directory(profile,base)
    explicit=values.get(r'unified\last_request','')
    paths=[Path(explicit)] if explicit else []
    folder=data/'projects/Native_Kitaplar'
    if folder.is_dir():
        sessions=sorted(folder.glob('request-*.json.session.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:10]
        paths.extend(Path(str(p).removesuffix('.session.json')) for p in sessions)
    active=[]
    for path in dict.fromkeys(paths):
        request=read_json(path);state=read_json(str(path)+'.session.json')
        queue=request.get('queue','')
        if not queue:continue
        queue=(path.parent/queue).resolve()
        if not valid_queue(queue):continue
        if state.get('state') in ('running','resource_pause') and alive(state.get('pid',0)):
            active.append((float(state.get('updated',0)),str(path.resolve()),str(queue)))
    if active:
        updated,request,queue=max(active)
        values.update({r'unified\last_request':request,r'unified\last_queue':queue})
    else:updated=0
    return {**profile,'values':values,'data_root':str(data),'active_at':updated}


def select_profile(profiles, base, alive):
    candidates=[recover_run(p,Path(base),alive) for p in profiles]
    stable=next((p for p in candidates if p['name']==APP),None)
    if stable and stable['values'].get(r'unified\installed_profile_migrated'):
        return stable
    active=[p for p in candidates if p['active_at']]
    if active:return max(active,key=lambda p:(p['active_at'],p['name']==APP))
    if stable and valid_queue(stable['values'].get(r'unified\last_queue','')):return stable
    saved=[p for p in candidates if valid_queue(p['values'].get(r'unified\last_queue',''))]
    if saved:return max(saved,key=lambda p:p.get('modified',0))
    return stable or max(candidates,key=lambda p:p.get('modified',0),default={'name':APP,'values':{},'data_root':str(Path(base)/APP),'active_at':0})


def process_running(pid):
    if not isinstance(pid,(int,float)) or pid<=0:return False
    import ctypes
    from ctypes import wintypes
    dll=ctypes.WinDLL('kernel32',use_last_error=True)
    dll.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];dll.OpenProcess.restype=wintypes.HANDLE
    dll.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)]
    dll.QueryFullProcessImageNameW.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)]
    dll.CloseHandle.argtypes=[wintypes.HANDLE]
    handle=dll.OpenProcess(0x1000,False,int(pid))
    if not handle:return False
    try:
        code=wintypes.DWORD();size=wintypes.DWORD(32768);name=ctypes.create_unicode_buffer(size.value)
        return bool(dll.GetExitCodeProcess(handle,ctypes.byref(code)) and code.value==259
                    and dll.QueryFullProcessImageNameW(handle,0,name,ctypes.byref(size))
                    and Path(name.value).name.lower() in ('python.exe','pythonw.exe'))
    finally:dll.CloseHandle(handle)


def registry_profiles():
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,REGISTRY) as key:
            names=[winreg.EnumKey(key,n) for n in range(winreg.QueryInfoKey(key)[0])]
    except FileNotFoundError:return []
    profiles=[]
    for name in names:
        if name!=APP and not re.fullmatch(re.escape(APP)+r'-\d+',name):continue
        values={};types={}
        def visit(relative=''):
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,REGISTRY+'\\'+name+('\\'+relative if relative else '')) as key:
                count,total,_=winreg.QueryInfoKey(key)
                for i in range(total):
                    field,value,kind=winreg.EnumValue(key,i);full=(relative+'\\' if relative else '')+field
                    values[full]=value;types[full]=kind
                children=[winreg.EnumKey(key,i) for i in range(count)]
            for child in children:visit((relative+'\\' if relative else '')+child)
        visit()
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,REGISTRY+'\\'+name) as key:modified=winreg.QueryInfoKey(key)[2]
        profiles.append({'name':name,'values':values,'types':types,'modified':modified})
    return profiles


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--plan',action='store_true');args=parser.parse_args()
    if os.name!='nt':raise RuntimeError('Windows installation migration only')
    import winreg
    base=Path(os.environ['LOCALAPPDATA']);profiles=registry_profiles()
    chosen=select_profile(profiles,base,process_running)
    summary={'source_profile':chosen['name'],'data_root':chosen['data_root'],
             'queue_restored':valid_queue(chosen['values'].get(r'unified\last_queue','')),
             'active_run_preserved':bool(chosen['active_at'])}
    if not args.plan:
        backup=base/APP/'upgrade-backups';backup.mkdir(parents=True,exist_ok=True)
        stamp=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        def encode(value):
            if isinstance(value,bytes):return {'base64':base64.b64encode(value).decode('ascii')}
            raise TypeError(type(value).__name__)
        (backup/(stamp+'-settings.json')).write_text(json.dumps(profiles,ensure_ascii=False,indent=2,default=encode),encoding='utf8')
        values=dict(chosen['values']);values[r'unified\data_root']=chosen['data_root'];values[r'unified\installed_profile_migrated']=1
        for field,value in values.items():
            relative,_,name=field.rpartition('\\')
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER,REGISTRY+'\\'+APP+('\\'+relative if relative else '')) as key:
                kind=chosen.get('types',{}).get(field,winreg.REG_DWORD if isinstance(value,int) else winreg.REG_SZ)
                winreg.SetValueEx(key,name,0,kind,value)
        (backup/(stamp+'-result.json')).write_text(json.dumps(summary,indent=2),encoding='utf8')
    print(json.dumps(summary,ensure_ascii=False))


if __name__=='__main__':main()
