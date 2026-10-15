"""Text-preserving PDF route for pages that need no geometry changes."""
from __future__ import annotations
import io
from pathlib import Path
import re
import uuid
import fitz
from pypdf import PdfReader,PdfWriter
from .runtime_layout import resource_path,native_model_directory
from .storage import workspace,ensure_space
from .pdf_profiles import publish


def page_range(value,count,clip=False):
    if not value or str(value).strip().lower() in ('all','tümü'):return list(range(count))
    selected=set()
    for token in str(value).split(','):
        token=token.strip()
        if not re.fullmatch(r'\d+(?:\s*-\s*\d+)?',token):raise ValueError('Sayfa aralığı örneği: 1-5,8,12-20')
        parts=[int(x) for x in token.split('-')];start=parts[0];end=parts[-1]
        if start<1 or end<start or start>count or end>count and not clip:raise ValueError('Sayfa aralığı PDF dışında.')
        selected.update(range(start-1,min(end,count)))
    return sorted(selected)


def copy_navigation(before,after,indices):
    after.set_metadata(before.metadata)
    if before.get_xml_metadata():after.set_xml_metadata(before.get_xml_metadata())
    mapping={old+1:new+1 for new,old in enumerate(indices)}
    toc=[];last=0
    for level,title,page in before.get_toc():
        if page in mapping:
            level=min(level,last+1);toc.append([level,title,mapping[page]]);last=level
    if toc:after.set_toc(toc)
    labels=[]
    for index,old in enumerate(indices):
        label=before[old].get_label()
        if label:labels.append({'startpage':index,'prefix':label,'style':'','firstpagenum':1})
    if labels:after.set_page_labels(labels)


def probe(source,pages='',password='',preview=None):
    with fitz.open(source) as doc:
        if doc.needs_pass and not doc.authenticate(password):raise ValueError('PDF şifresi gerekli veya yanlış.')
        indices=page_range(pages,len(doc));rows=[]
        for index in indices:
            page=doc[index];visible=any(item[0] in ('fill-text','stroke-text') for item in page.get_bboxlog())
            rows.append({'page':index+1,'characters':len(page.get_text().strip()),'visible_text':visible,
                'images':len(page.get_image_info()),'vectors':len(page.get_drawings()),'label':page.get_label(),
                'recommended_route':'preserve' if page.get_text().strip() else 'ocr_overlay'})
        if preview and indices:
            Path(preview).parent.mkdir(parents=True,exist_ok=True);doc[indices[0]].get_pixmap(dpi=100,alpha=False).save(preview)
        return {'total':len(doc),'selected':len(indices),'pages':rows,'preview':str(preview) if preview else ''}


def process(source,destination,conf=None,notify=lambda value:None):
    conf=conf or {};source,destination=Path(source).resolve(),Path(destination).resolve()
    if destination.exists() or destination==source:raise FileExistsError(destination)
    destination.parent.mkdir(parents=True,exist_ok=True);ensure_space([destination.parent],reserve_gib=conf.get('reserve_gib',2))
    staged=destination.with_name('.smart-'+uuid.uuid4().hex+'.pdf');reports=[]
    try:
        with fitz.open(source) as before,fitz.open() as after,workspace(conf) as work:
            if before.needs_pass and not before.authenticate(conf.get('password','')):raise ValueError('PDF şifresi gerekli veya yanlış.')
            indices=page_range(conf.get('pages',''),len(before))
            for number,index in enumerate(indices,1):
                ensure_space([destination.parent,work],needed=64*1024**2,reserve_gib=conf.get('reserve_gib',2))
                page=before[index];after.insert_pdf(before,from_page=index,to_page=index)
                target=after[-1];text=page.get_text().strip();method='preserved'
                if not text:
                    from .pdf_tools import render_page
                    dpi=min(int(conf.get('dpi',300)),max(72,int(float(conf.get('ocr_max_edge',6000))*72/max(page.rect.width,page.rect.height))))
                    pix,_=render_page(page,dpi,index+1);pix.set_dpi(dpi,dpi)
                    if conf.get('ocr_engine') in ('paddle','kraken'):
                        from PIL import Image
                        from .ocr_backend import recognize_pdf
                        image=Image.frombytes('RGB',(pix.width,pix.height),pix.samples)
                        data=recognize_pdf(image,conf,work)
                    else:
                        tessdata=resource_path('tessdata_'+conf.get('tess_quality','best'),conf.get('root'))
                        data=pix.pdfocr_tobytes(language=conf.get('language','deu'),tessdata=native_model_directory(tessdata))
                    writer=PdfWriter();writer.clone_document_from_reader(PdfReader(io.BytesIO(data)));writer.remove_images()
                    stream=io.BytesIO();writer.write(stream)
                    rotation=target.rotation;target.set_rotation(0)
                    with fitz.open(stream=stream.getvalue(),filetype='pdf') as layer:target.show_pdf_page(target.rect,layer,0,rotate=rotation)
                    target.set_rotation(rotation)
                    method='ocr_overlay'
                if page.get_pixmap(dpi=120,alpha=False).samples!=target.get_pixmap(dpi=120,alpha=False).samples:raise ValueError(f'PDF görünümü değişti: {index+1}')
                reports.append({'source_page':index+1,'output_page':number,'method':method,'render_identical_120dpi':True})
                notify({'stage':'PDF metni korunuyor' if method=='preserved' else 'Tarama sayfasına OCR ekleniyor','page':number,'total':len(indices)})
                fitz.TOOLS.store_shrink(100)
            copy_navigation(before,after,indices);after.save(staged,garbage=4,deflate=True,use_objstms=1)
        publish(staged,destination)
        from .settings import atomic_json
        result={'ok':True,'mode':'smart','pdf':str(destination),'pages':reports,'source':str(source),'digital_content_preserved':True}
        atomic_json(destination.with_suffix('.verification.json'),result)
        return result
    finally:staged.unlink(missing_ok=True)
