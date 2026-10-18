"""CycloneDX inventory from the actual staged runtime and pinned asset files."""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata
import json
import uuid

def generate(root):
    root=Path(root).resolve();components=[];seen=set()
    locations=[root/'runtime/Lib/site-packages',*root.glob('runtime/lib/python*/site-packages')]
    for dist in importlib.metadata.distributions(path=[str(p) for p in locations if p.exists()]):
        name=dist.metadata.get('Name','');version=dist.version
        if (name,version) in seen:continue
        seen.add((name,version));component={'type':'library','name':name,'version':version,'purl':f'pkg:pypi/{name.lower()}@{version}'}
        license=dist.metadata.get('License-Expression')
        if license:component['licenses']=[{'expression':license}]
        components.append(component)
    manifest=root/'FILES-SHA256.json'
    if manifest.exists():
        for name,sha in json.loads(manifest.read_text('utf8')).items():
            if name.startswith(('native/','resources/')):
                components.append({'type':'file','name':name,'hashes':[{'alg':'SHA-256','content':sha}]})
    return {'bomFormat':'CycloneDX','specVersion':'1.6','serialNumber':'urn:uuid:'+str(uuid.uuid4()),'version':1,
        'metadata':{'component':{'type':'application','name':'ScanTailor OCR Unified','version':json.loads((root/'release.json').read_text('utf8'))['version']}},'components':components}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root');p.add_argument('output');a=p.parse_args();Path(a.output).write_text(json.dumps(generate(a.root),indent=2),encoding='utf8')
