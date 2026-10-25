"""Assemble a clean, self-contained CPU distribution; never copy user books."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
PREFIX=ROOT/'.build-tools/msys64/ucrt64'

def copy(src,dest):
    dest.parent.mkdir(parents=True,exist_ok=True)
    if dest.is_file() and src.stat().st_size==dest.stat().st_size and src.stat().st_mtime_ns==dest.stat().st_mtime_ns:return
    shutil.copy2(src,dest)

def tree(src,dest):
    for source in src.rglob('*'):
        if source.is_file() and not any(part in ('__pycache__','.git') for part in source.relative_to(src).parts) and source.suffix not in ('.pyc','.pyo'):copy(source,dest/source.relative_to(src))

def main():
    global PREFIX
    p=argparse.ArgumentParser();p.add_argument('--destination',required=True);p.add_argument('--prefix',default=str(PREFIX));p.add_argument('--runtime',default=str(ROOT/'output/release043-build/windows-runtime/python'));p.add_argument('--build-dir',default=str(ROOT/'native/release063'));p.add_argument('--assets',default=str(ROOT));p.add_argument('--variant',choices=['full','minimal'],default='full');a=p.parse_args();PREFIX=Path(a.prefix).resolve()
    assets=Path(a.assets).resolve();build=Path(a.build_dir).resolve()
    def asset(relative):
        local=ROOT/'resources'/relative
        return local if local.exists() else assets/'resources'/relative
    required=['tessdata_best','tessdata_fast']
    models=('PP-OCRv6_small_det','PP-OCRv6_small_rec','PP-OCRv5_mobile_det','PP-OCRv5_server_det','latin_PP-OCRv5_mobile_rec','PP-DocLayout-S')
    if a.variant=='full':required+=['UVDoc/model/best_model.pkl',*('paddle-models/official_models/'+name+'/inference.pdiparams' for name in models)]
    for path in required:
        if not asset(path).exists():raise FileNotFoundError('Required package asset: '+path)
    if a.variant=='minimal':
        subprocess.run([str(Path(a.runtime)/'python.exe'),'-c','import importlib.util; assert importlib.util.find_spec("paddle") is None; assert importlib.util.find_spec("torch") is None'],check=True)
    dest=Path(a.destination).resolve();dest.mkdir(parents=True,exist_ok=True)
    native=dest/'native/bin';native.mkdir(parents=True,exist_ok=True)
    for name in ('scantailor-ocr-unified.exe','scantailor-experimental-cli.exe','opencl_plugin.dll'):
        copy(build/name,native/name)
    plugins=PREFIX/'share/qt5/plugins'
    for group in ('platforms','imageformats','styles'):
        tree(plugins/group,native/group)
    # Complete transitive DLL closure, including plugins, without PATH reliance.
    pending=list(native.rglob('*.exe'))+list(native.rglob('*.dll'));seen=set();dlls=[]
    while pending:
        binary=pending.pop()
        if binary.name.lower() in seen:continue
        seen.add(binary.name.lower())
        result=subprocess.check_output([str(PREFIX/'bin/objdump.exe'),'-p',str(binary)],text=True,errors='replace')
        for name in re.findall(r'DLL Name:\s*(\S+)',result):
            source=PREFIX/'bin'/name
            if source.exists():
                target=native/name;copy(source,target);pending.append(target);dlls.append(name)
    (native/'qt.conf').write_text('[Paths]\nPlugins=.\nTranslations=translations\n','utf8')
    for qm in build.rglob('*.qm'):copy(qm,native/'translations'/qm.name)
    tree(Path(a.runtime),dest/'runtime')
    tree(ROOT/'src/scantailor_ocr_unified',dest/'src/scantailor_ocr_unified')
    tree(asset('tessdata_best'),dest/'resources/tessdata_best')
    tree(asset('tessdata_fast'),dest/'resources/tessdata_fast')
    copy(ROOT/'packaging/ocr-catalog.json',dest/'resources/ocr-catalog.json')
    for name in (('model.py','LICENSE','model/best_model.pkl') if a.variant=='full' else ()):
        copy(asset('UVDoc/'+name),dest/'resources/UVDoc'/name)
    for model in (models if a.variant=='full' else ()):
        folder=asset('paddle-models/official_models/'+model)
        for source in folder.rglob('*'):
            if source.is_file() and not any(part.startswith('.') for part in source.relative_to(folder).parts):
                copy(source,dest/'resources/paddle-models/official_models'/model/source.relative_to(folder))
    # CCITT (binary) and Flate (gray/color) are lossless and require no optional
    # external JBIG2 encoder. Native experimental binarizers remain available.
    (dest/'local-tools.json').write_text(json.dumps({'tessdata':'resources/tessdata_best','jbig2':None,'uvdoc':'resources/UVDoc'}),'utf8')
    (dest/'release.json').write_text(json.dumps({'version':'0.6.3','platform':'windows-x64','variant':a.variant,'cpu_bundled':a.variant=='full','python':'3.12.15','native_parallel_workers':2,'native_cli_sha256':hashlib.sha256((native/'scantailor-experimental-cli.exe').read_bytes()).hexdigest()},indent=2),'utf8')
    if a.variant=='full':(dest/'resources/paddle-cpu-ready.json').write_text(json.dumps({'ok':True,'device':'cpu','paddle':'3.3.0','paddleocr':'3.7.0','bundled':True}),'utf8')
    copy(ROOT/'LICENSE',dest/'LICENSE')
    copy(ROOT/'native/scantailor-experimental/LICENSE',dest/'licenses/ScanTailor-GPL-3.0.txt')
    # Keep every library's upstream notice. The source archive accompanies the
    # installer in the same release, with upstream source locations recorded.
    if (PREFIX/'share/licenses').exists():tree(PREFIX/'share/licenses',dest/'licenses/native-dependencies')
    for name in ('README.md','README.tr.md','THIRD_PARTY_NOTICES.md'):
        if (ROOT/name).is_file():copy(ROOT/name,dest/name)
    tree(ROOT/'packaging',dest/'packaging')
    (dest/'Baslat.cmd').write_text('@echo off\r\nstart "" "%~dp0native\\bin\\scantailor-ocr-unified.exe" %*\r\n','ascii')
    files={str(p.relative_to(dest)).replace('\\','/'):hashlib.file_digest(p.open('rb'),'sha256').hexdigest() for p in dest.rglob('*') if p.is_file() and p.name!='FILES-SHA256.json'}
    (dest/'FILES-SHA256.json').write_text(json.dumps(files,indent=2),'utf8')
    print(json.dumps({'files':len(files),'bytes':sum(p.stat().st_size for p in dest.rglob('*') if p.is_file()),'destination':str(dest),'native_dlls':len(set(dlls))}))

if __name__=='__main__':main()
