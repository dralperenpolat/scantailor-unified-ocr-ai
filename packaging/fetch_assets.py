"""Fetch pinned public models; verify before replacing any destination file."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,urllib.request

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',default=str(Path(__file__).resolve().parents[1]));parser.add_argument('--verify-only',action='store_true');parser.add_argument('--variant',choices=['full','minimal'],default='full');a=parser.parse_args()
    root=Path(a.root).resolve();manifest=json.loads(Path(__file__).with_name('assets.json').read_text('utf8'))
    for item in manifest['assets']:
        if a.variant=='minimal' and '/tessdata_' not in item['path']:continue
        path=(root/item['path']).resolve()
        if not path.is_relative_to(root):raise ValueError('Asset path escapes root')
        if path.is_file() and digest(path)==item['sha256']:continue
        if a.verify_only:raise RuntimeError('Missing or changed asset: '+item['path'])
        path.parent.mkdir(parents=True,exist_ok=True);partial=path.with_suffix(path.suffix+'.download')
        request=urllib.request.Request(item['url'],headers={'User-Agent':'ScanTailor-Unified/0.4.3'})
        with urllib.request.urlopen(request,timeout=120) as response, partial.open('wb') as stream:shutil.copyfileobj(response,stream)
        if digest(partial)!=item['sha256']:raise RuntimeError('SHA256 mismatch: '+item['path'])
        os.replace(partial,path)
        print(item['path'],flush=True)
    # Catalog itself is part of the source bundle, not a mutable remote URL.
    target=root/'resources/ocr-catalog.json';target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists():shutil.copy2(Path(__file__).with_name('ocr-catalog.json'),target)
    print('All pinned assets verified.')

if __name__=='__main__':main()
