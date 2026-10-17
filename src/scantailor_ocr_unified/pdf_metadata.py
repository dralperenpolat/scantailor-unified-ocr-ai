"""Restore imported PDF navigation using explicit source-to-output mappings."""
import json
from pathlib import Path
import re
import fitz


def restore_import(source,destination,import_folder,page_records):
    folder=Path(import_folder);manifest=json.loads((folder/'import.json').read_text('utf8'))
    selected={r['output_page']:r['source_page'] for r in manifest.get('page_mapping',[])}
    mapping={};labels=[]
    for output,row in enumerate(page_records,1):
        match=re.search(r'page-(\d+)',Path(row.get('source','')).stem)
        if not match:continue
        imported=int(match[1]);original=selected.get(imported,imported)
        mapping.setdefault(original,output)
        receipt=folder/f'page-{imported:05d}.json'
        if receipt.is_file():
            label=json.loads(receipt.read_text('utf8')).get('label','')
            if label:labels.append({'startpage':output-1,'prefix':label,'style':'','firstpagenum':1})
    toc=[];last=0
    for level,title,page in manifest.get('toc',[]):
        if page in mapping:
            level=min(level,last+1);toc.append([level,title,mapping[page]]);last=level
    with fitz.open(source) as doc:
        doc.set_metadata(manifest.get('metadata',{}))
        if toc:doc.set_toc(toc)
        if labels:doc.set_page_labels(labels)
        doc.save(destination,garbage=4,deflate=True,use_objstms=1)
    return {'source_to_output':mapping,'bookmarks':len(toc),'labels':len(labels),'metadata_preserved':True,
            'note':'Bookmarks target the first output of a split page; repeated labels preserve the printed source identity.'}
