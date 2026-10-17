"""Explainable review hints. Heuristics never certify OCR correctness."""
from __future__ import annotations
from collections import Counter, defaultdict
from pathlib import Path
import statistics
import xml.etree.ElementTree as ET
import fitz


def source_mapping(project):
    project=Path(project);tree=ET.parse(project).getroot()
    directories={e.get('id'):(project.parent/e.get('path')).resolve() for e in tree.findall('./directories/directory')}
    files={e.get('id'):str(directories[e.get('dirId')]/e.get('name')) for e in tree.findall('./files/file')}
    images={e.get('id'):files[e.get('fileId')] for e in tree.findall('./images/image')}
    return [{'page':n,'source':images.get(e.get('imageId'),''),'logical_id':e.get('id'),
             'sub_page':e.get('subPage','single')} for n,e in enumerate(tree.findall('./pages/page'),1)]


def reasons(*, chars=0, expected_text=True, geometry='', placement='', uncertain=(), confidence=None, threshold=.85):
    result=[]
    def add(code,severity,detail):result.append({'code':code,'severity':severity,'detail':detail})
    if expected_text and chars<15:add('little_text','warning','OCR metni çok kısa; boş veya resimli sayfa olabilir.')
    if geometry:add('geometry','warning',geometry)
    if placement:add('placement','warning',placement)
    if uncertain:add('cleanup','warning',f'{len(uncertain)} temizleme bölgesi belirsiz.')
    if confidence is not None and confidence<threshold:
        add('low_confidence','warning',f'OCR motor güveni {confidence:.1%}; bu değer doğruluk oranı değildir.')
    return result


def inspect_page(page):
    words=page.get_text('words');w,h=page.rect.width,page.rect.height
    clipped=[row[4] for row in words if row[0]<-.5 or row[1]<-.5 or row[2]>w+.5 or row[3]>h+.5]
    result=[]
    if clipped:result.append({'code':'text_outside_page','severity':'error','detail':f'{len(clipped)} metin kutusu sayfa dışında.'})
    return result


def integrity(pages, expected=None):
    result=[];seen={};numbers=[p['page'] for p in pages]
    if numbers!=list(range(1,len(pages)+1)):
        result.append({'code':'page_order','severity':'error','detail':'Çıktı sayfa sırası kesintili veya yinelenmiş.'})
    if expected is not None and len(pages)!=expected:
        result.append({'code':'page_count','severity':'error','detail':f'Beklenen {expected}, bulunan {len(pages)} sayfa.'})
    for row in pages:
        stamp=row.get('pixels_sha256')
        if stamp and stamp in seen:
            result.append({'code':'duplicate_image','severity':'warning','page':row['page'],
                           'detail':f'Görüntü {seen[stamp]}. sayfayla aynı; boş sayfa veya kasıtlı tekrar olabilir.'})
        if stamp:seen[stamp]=row['page']
    sources=Counter(p.get('source') for p in pages if p.get('source'))
    for source,count in sources.items():
        if count>2:result.append({'code':'unexpected_split','severity':'warning','detail':f'{Path(source).name}: {count} çıktı sayfası.'})
    return result


def page_structure(page):
    """Conservative geometry ordering; retains all boxes and marks inferred roles."""
    rows=[];heights=[]
    for block in page.get_text('dict')['blocks']:
        for line in block.get('lines',[]):
            text=''.join(span['text'] for span in line['spans']).strip()
            if not text:continue
            box=list(line['bbox']);height=box[3]-box[1];heights.append(height)
            rows.append({'text':text,'box':box,'height':height})
    median=statistics.median(heights) if heights else 0
    w,h=page.rect.width,page.rect.height
    left=[r for r in rows if r['box'][2]<w*.53 and r['box'][0]<w*.4]
    right=[r for r in rows if r['box'][0]>w*.47 and r['box'][2]>w*.6]
    columns=len(left)>=4 and len(right)>=4
    for row in rows:
        box=row['box'];row['role']='body';row['column']=0
        if box[1]>h*.72 and row['height']<median*.88:row['role']='footnote'
        if box[1]>h*.9 and row['text'].strip().isdigit():row['role']='page_number'
        elif box[3]<h*.13:row['role']='header'
        if columns:row['column']=1 if row in right else 0
    # Full-width headings stay before columns. Footnotes follow body text.
    rows.sort(key=lambda r:({'header':0,'body':1,'footnote':2,'page_number':3}[r['role']],r['column'],r['box'][1],r['box'][0]))
    for index,row in enumerate(rows):row.update(order=index,role_inferred=True)
    return {'width':w,'height':h,'lines':rows,'columns_inferred':2 if columns else 1,
            'requires_layout_review':columns,'method':'geometry-v1','table_semantics':'not_inferred'}


def export_text(pdf, destination):
    """TXT, Markdown, ALTO and box JSON, all from the actual final PDF."""
    from .settings import atomic_json
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    stem=Path(pdf).stem
    targets={extension:destination/(stem+'.'+extension) for extension in ('txt','md','alto.xml','layout.json')}
    if any(path.exists() for path in targets.values()):raise FileExistsError('Metin çıktısı zaten var; yeni klasör seçin.')
    ns='http://www.loc.gov/standards/alto/ns-v4#';ET.register_namespace('',ns)
    tag=lambda name:'{'+ns+'}'+name
    alto=ET.Element(tag('alto'));description=ET.SubElement(alto,tag('Description'))
    ET.SubElement(description,tag('MeasurementUnit')).text='pixel'
    layout=ET.SubElement(alto,tag('Layout'));pages=[];texts=[];markdown=[]
    with fitz.open(pdf) as doc:
        for n,page in enumerate(doc,1):
            value=page_structure(page);value['page']=n;pages.append(value)
            lines=value['lines'];texts.append('\n'.join(r['text'] for r in lines))
            markdown.append(f'<!-- Page {n}; geometry-based reading order -->\n\n'+'\n\n'.join(r['text'] for r in lines))
            node=ET.SubElement(layout,tag('Page'),ID=f'p{n}',PHYSICAL_IMG_NR=str(n),WIDTH=str(round(page.rect.width)),HEIGHT=str(round(page.rect.height)))
            space=ET.SubElement(node,tag('PrintSpace'),HPOS='0',VPOS='0',WIDTH=str(round(page.rect.width)),HEIGHT=str(round(page.rect.height)))
            for i,row in enumerate(lines):
                x,y,x1,y1=row['box'];attrs=dict(HPOS=str(round(x)),VPOS=str(round(y)),WIDTH=str(round(x1-x)),HEIGHT=str(round(y1-y)))
                block=ET.SubElement(space,tag('TextBlock'),ID=f'p{n}b{i}',**attrs)
                line=ET.SubElement(block,tag('TextLine'),ID=f'p{n}l{i}',**attrs)
                ET.SubElement(line,tag('String'),CONTENT=row['text'],**attrs)
    targets['txt'].write_text('\n\f\n'.join(texts),encoding='utf8')
    targets['md'].write_text('\n\n---\n\n'.join(markdown),encoding='utf8')
    ET.ElementTree(alto).write(targets['alto.xml'],encoding='utf-8',xml_declaration=True)
    atomic_json(targets['layout.json'],{'schema':1,'source':str(Path(pdf).resolve()),'coordinates':'PDF points; ALTO uses an equivalent 72 DPI pixel grid','pages':pages})
    return {key:str(value) for key,value in targets.items()}
