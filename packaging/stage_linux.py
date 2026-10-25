"""Bundle Qt, Python and CPU models on the oldest supported Linux baseline."""
from pathlib import Path
import argparse
import json
import hashlib
import os
import re
import shutil
import subprocess

def main():
    p=argparse.ArgumentParser();p.add_argument('--root',default='.');p.add_argument('--runtime',required=True);p.add_argument('--destination',required=True);p.add_argument('--variant',choices=['full','minimal'],default='full');a=p.parse_args()
    if a.variant=='minimal':subprocess.run([str(Path(a.runtime)/'bin/python3'),'-c','import importlib.util; assert importlib.util.find_spec("paddle") is None; assert importlib.util.find_spec("torch") is None'],check=True)
    root=Path(a.root).resolve();dest=Path(a.destination).resolve();native=dest/'native/bin';native.mkdir(parents=True,exist_ok=True)
    def copy(src,dst):
        dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
    ignore=shutil.ignore_patterns('__pycache__','*.pyc','.git','.cache')
    for name in ('scantailor-ocr-unified','scantailor-experimental-cli','libopencl_plugin.so'):
        copy(root/'native/release-linux'/name,native/name)
    plugins=Path(subprocess.check_output(['qmake','-query','QT_INSTALL_PLUGINS'],text=True).strip())
    for sub in ('platforms','imageformats','styles'):
        if (plugins/sub).exists():shutil.copytree(plugins/sub,native/sub,dirs_exist_ok=True)
    todo=[*native.glob('scantailor-*'),*native.rglob('*.so')];seen=set();libraries={}
    system={'libc.so.6','libm.so.6','libpthread.so.0','libdl.so.2','librt.so.1','libresolv.so.2','libutil.so.1','ld-linux-x86-64.so.2'}
    while todo:
        item=todo.pop()
        if item.name in seen:continue
        seen.add(item.name)
        info=subprocess.check_output(['ldd',str(item)],text=True)
        for name,path in re.findall(r'^\s*(\S+) => (\/\S+) ',info,re.M):
            if name in system:continue
            source=Path(path);target=native/name
            if not target.exists():copy(source,target)
            libraries[name]=path;todo.append(target)
    (native/'qt.conf').write_text('[Paths]\nPlugins=.\nTranslations=translations\n')
    for path in (root/'native/release-linux').rglob('*.qm'):copy(path,native/'translations'/path.name)
    shutil.copytree(root/'src',dest/'src',dirs_exist_ok=True,ignore=ignore)
    assets=json.loads((root/'packaging/assets.json').read_text('utf8'))
    for item in assets['assets']:
        if a.variant=='full' or '/tessdata_' in item['path']:copy(root/item['path'],dest/item['path'])
    copy(root/'resources/ocr-catalog.json',dest/'resources/ocr-catalog.json')
    if a.variant=='full':(dest/'resources/paddle-cpu-ready.json').write_text(json.dumps({'ok':True,'device':'cpu','paddle':'3.3.0','paddleocr':'3.7.0','bundled':True}))
    shutil.copytree(Path(a.runtime),dest/'runtime',dirs_exist_ok=True,ignore=ignore)
    for name in ('LICENSE','README.md','README.tr.md','THIRD_PARTY_NOTICES.md'):
        if (root/name).exists():copy(root/name,dest/name)
    shutil.copytree(root/'packaging',dest/'packaging',dirs_exist_ok=True,ignore=ignore)
    copy(root/'native/scantailor-experimental/LICENSE',dest/'licenses/ScanTailor-GPL-3.0.txt')
    # Debian/Ubuntu copyright files accompany the dynamically linked libraries.
    for library,path in libraries.items():
        result=subprocess.run(['dpkg-query','-S',path],text=True,capture_output=True)
        for line in result.stdout.splitlines():
            package=line.split(': ',1)[0].split(':')[0];notice=Path('/usr/share/doc')/package/'copyright'
            if notice.exists():copy(notice,dest/'licenses/native-dependencies'/(package+'.txt'))
    (dest/'native-libraries.json').write_text(json.dumps(libraries,indent=2))
    (dest/'local-tools.json').write_text(json.dumps({'tessdata':'resources/tessdata_best','jbig2':None,'uvdoc':'resources/UVDoc'}))
    (dest/'release.json').write_text(json.dumps({'version':'0.6.5','platform':'linux-x64','variant':a.variant,'glibc_minimum':'2.35','cpu_bundled':a.variant=='full','native_parallel_workers':2,'native_cli_sha256':hashlib.sha256((native/'scantailor-experimental-cli').read_bytes()).hexdigest()}))
    script='''#!/bin/sh
set -eu
APPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export STOU_ROOT="$APPDIR"
export LD_LIBRARY_PATH="$APPDIR/native/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$APPDIR/src"
export PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1
exec "$APPDIR/native/bin/scantailor-ocr-unified" "$@"
'''
    (dest/'ScanTailor-OCR-Unified').write_text(script);(dest/'ScanTailor-OCR-Unified').chmod(0o755)
    (dest/'install-user.sh').write_text('''#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET="${XDG_DATA_HOME:-$HOME/.local/share}/scantailor-ocr-unified-app"
mkdir -p "$TARGET" "$HOME/.local/share/applications"
if [ "$HERE" != "$TARGET" ]; then cp -a "$HERE/." "$TARGET/"; fi
cat > "$HOME/.local/share/applications/scantailor-ocr-unified.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=ScanTailor OCR Unified
Comment=Kitap fotoğraflarını düzenle ve OCR içeren PDF oluştur
Exec="$TARGET/ScanTailor-OCR-Unified" %f
Terminal=false
Categories=Graphics;Office;
EOF
printf '%s\\n' 'Kurulum tamamlandı. Uygulama menüsünden ScanTailor OCR Unified açılabilir.'
''');(dest/'install-user.sh').chmod(0o755)
    for action in ('install','uninstall'):
        entry=dest/(action+'-user.sh')
        entry.write_text('#!/bin/sh\nset -eu\nHERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$HERE/runtime/bin/python3" "$HERE/packaging/deploy_linux.py" '+action+'\n')
        entry.chmod(0o755)
    files={p.relative_to(dest).as_posix():hashlib.file_digest(p.open('rb'),'sha256').hexdigest() for p in dest.rglob('*') if p.is_file() and p.name!='FILES-SHA256.json'}
    (dest/'FILES-SHA256.json').write_text(json.dumps(files,indent=2))
    print(json.dumps({'files':len(files),'native_libraries':len(libraries),'destination':str(dest)}))

if __name__=='__main__':main()
