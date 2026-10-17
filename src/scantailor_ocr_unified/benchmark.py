"""Human-verified ground truth only. No OCR output becomes reference text."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import random
import re
import unicodedata
import fitz
from .settings import atomic_json


def distance(reference, hypothesis):
    # Exact Myers bit-vector Levenshtein; Python big integers avoid an O(n*m)
    # Python loop for full book pages. Works for characters and word tokens.
    if len(reference)>len(hypothesis):reference,hypothesis=hypothesis,reference
    length=len(reference)
    if not length:return len(hypothesis)
    matches={}
    for index,value in enumerate(reference):matches[value]=matches.get(value,0)|(1<<index)
    mask=(1<<length)-1;top=1<<(length-1);positive=mask;negative=0;score=length
    for value in hypothesis:
        equal=matches.get(value,0);vertical=equal|negative
        horizontal=(((equal&positive)+positive)^positive)|equal
        plus=negative|~(horizontal|positive);minus=positive&horizontal
        if plus&top:score+=1
        elif minus&top:score-=1
        plus=(plus<<1)|1;minus<<=1
        positive=(minus|~(vertical|plus))&mask;negative=(plus&vertical)&mask
    return score


def scores(reference, hypothesis):
    reference=unicodedata.normalize('NFC',reference);hypothesis=unicodedata.normalize('NFC',hypothesis)
    ref_words=reference.split();hyp_words=hypothesis.split()
    chars=distance(reference,hypothesis);words=distance(ref_words,hyp_words)
    return {'character_errors':chars,'reference_characters':len(reference),'CER':chars/len(reference) if reference else None,
            'word_errors':words,'reference_words':len(ref_words),'WER':words/len(ref_words) if ref_words else None,
            'normalization':'NFC; case, punctuation and character whitespace retained; WER splits whitespace'}


def create_corpus(queue, destination, count=250, seed=50):
    from .queue_store import read_pages
    destination=Path(destination).resolve()
    if destination.exists() and any(destination.iterdir()):raise FileExistsError('Derlem için boş veya yeni klasör seçin.')
    destination.mkdir(parents=True,exist_ok=True)
    data=json.loads(Path(queue).read_text('utf8'));groups={}
    sqlite=Path(str(queue)+'.sqlite3').is_file()
    for job in data['jobs']:
        rows=[] if sqlite else job.get('pages',[]);offset=0
        while sqlite:
            batch=read_pages(queue,job['id'],offset,500);rows.extend(batch['pages']);offset+=len(batch['pages'])
            if offset>=batch['total'] or not batch['pages']:break
        final=job.get('pdf')
        final=final if final and Path(final).is_file() else None
        for row in rows:
            if final or row.get('page_pdf'):
                reason=','.join(sorted(r['code'] for r in row.get('reasons',[]))) or ('warning' if row.get('warning') else 'ordinary')
                groups.setdefault((job['id'],reason),[]).append((job,row,final))
    rng=random.Random(seed)
    for group in groups.values():rng.shuffle(group)
    selected=[]
    while groups and len(selected)<max(1,min(1000,count)):
        for key in list(groups):
            selected.append(groups[key].pop())
            if not groups[key]:del groups[key]
            if len(selected)>=count:break
    pages=[];hashes={};mappings={}
    for index,(job,row,final) in enumerate(selected,1):
        identifier=f'{index:04d}';source=Path(final or row['page_pdf']).resolve();page_index=int(row['page'])-1 if final else 0
        if not source.is_file():continue
        with fitz.open(source) as doc:
            if page_index>=len(doc):continue
            hypothesis=doc[page_index].get_text();doc[page_index].get_pixmap(dpi=120,alpha=False).save(destination/(identifier+'.png'))
        if source not in hashes:
            with source.open('rb') as stream:hashes[source]=hashlib.file_digest(stream,'sha256').hexdigest()
        original=row.get('source','')
        project=job.get('project')
        if not original and project:
            if project not in mappings:
                try:
                    from .quality import source_mapping
                    mappings[project]=source_mapping(project)
                except (OSError,ValueError,KeyError):mappings[project]=[]
            original=next((p['source'] for p in mappings[project] if p['page']==row['page']),'')
        (destination/(identifier+'.ocr.txt')).write_text(hypothesis,encoding='utf8')
        pages.append({'id':identifier,'book':job['title'],'job_id':job['id'],'page':row['page'],'pdf':str(source),
            'pdf_sha256':hashes[source],'pdf_page_index':page_index,'image':identifier+'.png',
            'original_source':original,
            'hypothesis':identifier+'.ocr.txt','reference':identifier+'.reference.txt','verified':False,
            'reasons':row.get('reasons',[]),'tags':[],'regions':[]})
    manifest={'schema':1,'seed':seed,'requested':count,'sampled':len(pages),'pages':pages,
              'reference_policy':'Reference text must be transcribed and verified by a human; OCR is only a hypothesis.'}
    atomic_json(destination/'corpus.json',manifest)
    return {'manifest':str(destination/'corpus.json'),'sampled':len(pages),'verified':0}


def annotate(manifest, identifier, reference, tags=(), regions=()):
    manifest=Path(manifest);data=json.loads(manifest.read_text('utf8'))
    row=next(r for r in data['pages'] if r['id']==identifier)
    if hashlib.sha256(Path(row['pdf']).read_bytes()).hexdigest()!=row['pdf_sha256']:raise ValueError('Örnek PDF değişmiş.')
    # Revisions of reference text are retained, so annotation can be audited.
    import time
    path=manifest.parent/(identifier+f'.reference-{time.time_ns()}.txt')
    path.write_text(reference,encoding='utf8')
    row.update(reference=path.name,verified=True,tags=list(tags),regions=list(regions))
    atomic_json(manifest,data)
    return row


def evaluate(manifest):
    manifest=Path(manifest);data=json.loads(manifest.read_text('utf8'));rows=[]
    for row in data['pages']:
        if not row.get('verified'):continue
        if hashlib.sha256(Path(row['pdf']).read_bytes()).hexdigest()!=row['pdf_sha256']:raise ValueError('Derlem PDF değişmiş: '+row['id'])
        reference=(manifest.parent/row['reference']).read_text('utf8');hypothesis=(manifest.parent/row['hypothesis']).read_text('utf8')
        rows.append({'id':row['id'],'book':row['book'],'tags':row.get('tags',[]),**scores(reference,hypothesis)})
    totals={key:sum(row[key] for row in rows) for key in ('character_errors','reference_characters','word_errors','reference_words')}
    totals['CER']=totals['character_errors']/totals['reference_characters'] if totals['reference_characters'] else None
    totals['WER']=totals['word_errors']/totals['reference_words'] if totals['reference_words'] else None
    manual={tag:sum(tag in row['tags'] for row in rows) for tag in ('clipped_text','missing_footnote','wrong_split','wrong_order')}
    return {'sampled':len(data['pages']),'verified':len(rows),'coverage':len(rows)/len(data['pages']) if data['pages'] else 0,'totals':totals,'pages':rows,
            'manual_page_defects':manual,'manual_defect_scope':'Counts on verified pages only; original source must be inspected for omissions.'}
