"""Per-user install/upgrade/uninstall of manifest-owned files only."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import uuid


def files(root):
    result=json.loads((root/'FILES-SHA256.json').read_text('utf8'))
    for name in result:
        if Path(name).is_absolute() or '..' in Path(name).parts or not (root/name).resolve().is_relative_to(root):
            raise ValueError('Unsafe installation manifest path')
    return result


def remove_owned(root,manifest,keep=()):
    for name,sha in manifest.items():
        path=root/name
        if name not in keep and path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:unchanged=hashlib.file_digest(stream,'sha256').hexdigest()==sha
            if unchanged:path.unlink()
    for path in sorted((p for p in root.rglob('*') if p.is_dir() and not p.is_symlink()),key=lambda p:len(p.parts),reverse=True):
        try:path.rmdir()
        except OSError:pass


def main():
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['install','uninstall']);args=parser.parse_args()
    base=Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share')).resolve()
    target=(base/'scantailor-ocr-unified-app').resolve();source=Path(__file__).resolve().parents[1]
    if target.parent!=base or target.name!='scantailor-ocr-unified-app':raise ValueError('Unexpected installation target')
    desktop=base/'applications/scantailor-ocr-unified.desktop'
    if args.action=='uninstall':
        if source!=target or not (target/'release.json').is_file():raise ValueError('Run uninstall from the installed application')
        remove_owned(target,files(target));(target/'FILES-SHA256.json').unlink(missing_ok=True)
        if desktop.is_file() and str(target) in desktop.read_text('utf8'):desktop.unlink()
        try:target.rmdir()
        except OSError:pass
        print('Application removed; user projects, models and outputs retained.');return
    manifest=files(source)
    if source!=target:
        if target.exists() and any(target.iterdir()) and not (target/'release.json').is_file():raise ValueError('Target is not a recognized installation')
        previous=files(target) if (target/'FILES-SHA256.json').is_file() else {}
        target.mkdir(parents=True,exist_ok=True)
        for name,sha in manifest.items():
            original=source/name
            with original.open('rb') as stream:
                if hashlib.file_digest(stream,'sha256').hexdigest()!=sha:raise ValueError('Corrupt package: '+name)
            dest=target/name;dest.parent.mkdir(parents=True,exist_ok=True)
            temporary=dest.with_name('.stou-install-'+uuid.uuid4().hex)
            try:shutil.copy2(original,temporary);os.replace(temporary,dest)
            finally:temporary.unlink(missing_ok=True)
        remove_owned(target,previous,keep=manifest)
        shutil.copy2(source/'FILES-SHA256.json',target/'FILES-SHA256.json')
    desktop.parent.mkdir(parents=True,exist_ok=True)
    desktop.write_text('[Desktop Entry]\nType=Application\nName=ScanTailor OCR Unified\nExec="'+str(target/'ScanTailor-OCR-Unified')+'" %f\nTerminal=false\nCategories=Graphics;Office;\n','utf8')
    print(target)


if __name__=='__main__':main()
