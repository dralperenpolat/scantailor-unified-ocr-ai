"""Optional local engines. No model download is hidden in a normal export."""
import json
from pathlib import Path
import shutil
import subprocess
import os
import xml.etree.ElementTree as ET


def probe(conf):
    result={}
    for key in ('kraken','ocrmypdf','verapdf'):
        raw=conf.get(key) or shutil.which(key)
        result[key]={'available':bool(raw and Path(raw).is_file()),'path':str(raw or '')}
    result['kraken_model']={'available':Path(conf.get('kraken_model','')).is_file(),'path':conf.get('kraken_model','')}
    result['structure_python']={'available':Path(conf.get('structure_python','')).is_file(),'path':conf.get('structure_python','')}
    return result


def kraken_lines(image,conf,work):
    command=conf.get('kraken') or shutil.which('kraken');model=conf.get('kraken_model','')
    if not command or not Path(command).is_file() or not Path(model).is_file():raise FileNotFoundError('Kraken ve doğrulanmış model dosyası seçilmedi.')
    source=Path(work)/'kraken.png';target=Path(work)/'kraken.alto.xml';image.save(source)
    run=subprocess.run([command,'-i',str(source),str(target),'-a','segment','-bl','ocr','-m',model],
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace',timeout=1800,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if run.returncode:raise RuntimeError(run.stdout[-3000:])
    lines=[]
    for node in ET.parse(target).getroot().iter():
        if node.tag.split('}')[-1]!='TextLine':continue
        strings=[s for s in node.iter() if s.tag.split('}')[-1]=='String']
        if not strings:continue
        boxes=[(float(s.get('HPOS',0)),float(s.get('VPOS',0)),float(s.get('WIDTH',0)),float(s.get('HEIGHT',0))) for s in strings]
        lines.append({'text':' '.join(s.get('CONTENT','') for s in strings),
            'box':[min(b[0] for b in boxes),min(b[1] for b in boxes),max(b[0]+b[2] for b in boxes),max(b[1]+b[3] for b in boxes)],
            'score':sum(float(s.get('WC',0)) for s in strings)/len(strings)})
    return {'lines':lines,'engine':'kraken','model':str(Path(model).resolve()),'confidence_kind':'engine score; not calibrated accuracy'}


def structure(source,destination,conf):
    python=conf.get('structure_python','')
    if not Path(python).is_file():raise FileNotFoundError('PP-StructureV3 için ayrı Python ortamı seçin.')
    target=Path(destination)
    if target.exists() and any(target.iterdir()):raise FileExistsError('Yeni yapı analizi klasörü seçin.')
    target.mkdir(parents=True,exist_ok=True)
    request={'source':str(Path(source).resolve()),'destination':str(target.resolve()),'device':conf.get('ocr_device','cpu')}
    run=subprocess.run([python,str(Path(__file__).with_name('structure_runtime.py'))],input=json.dumps(request),
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace',timeout=7200,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if run.returncode:raise RuntimeError(run.stdout[-4000:])
    return {'folder':str(target),'files':[str(p) for p in target.rglob('*') if p.is_file()],
            'method':'PP-StructureV3; inferred layout requires review'}
