"""Explicitly lossy size profiles; binary scans and text remain untouched."""
from __future__ import annotations
import io
import os
from pathlib import Path
import shutil
import uuid
import fitz
from PIL import Image, ImageChops, ImageStat


def publish(temporary,destination):
    temporary,destination=Path(temporary),Path(destination)
    if os.name=='nt':os.rename(temporary,destination)
    else:os.link(temporary,destination)


def compress(source,destination,profile='balanced',notify=lambda value:None):
    if profile=='lossless':
        from .pdf_tools import compact_pdf
        return compact_pdf(source,destination,notify)
    if profile not in ('balanced','small'):raise ValueError('Unknown compression profile')
    source,destination=Path(source).resolve(),Path(destination).resolve()
    if destination.exists() or source==destination:raise FileExistsError(destination)
    destination.parent.mkdir(parents=True,exist_ok=True)
    temporary=destination.with_name('.compress-'+uuid.uuid4().hex+'.pdf')
    dpi,quality=(300,92) if profile=='balanced' else (200,80)
    changed=0;seen=set();differences=[]
    try:
        with fitz.open(source) as doc:
            placements={}
            for page in doc:
                for item in page.get_images(full=True):
                    placements.setdefault(item[0],[]).extend(page.get_image_rects(item[0]))
            for number,page in enumerate(doc,1):
                for image in page.get_images(full=True):
                    xref,mask,width,height,bpc=image[:5]
                    if xref in seen or mask or bpc==1:continue
                    seen.add(xref)
                    rects=page.get_image_rects(xref)
                    if not rects:continue
                    pix=fitz.Pixmap(doc,xref)
                    if pix.alpha or pix.n not in (1,3) or width*height<65536:continue
                    im=Image.frombytes('RGB' if pix.n==3 else 'L',(width,height),pix.samples)
                    # Shared images use their largest placement in the document.
                    all_rects=placements[xref]
                    max_width=max(r.width for r in all_rects);max_height=max(r.height for r in all_rects)
                    target=(max(1,round(max_width*dpi/72)),max(1,round(max_height*dpi/72)))
                    im.thumbnail(target,Image.Resampling.LANCZOS)
                    stream=io.BytesIO();im.save(stream,format='JPEG',quality=quality,optimize=True)
                    old=doc.extract_image(xref)
                    if len(stream.getvalue())>=len(old['image']):continue
                    page.replace_image(xref,stream=stream.getvalue());changed+=1
                notify({'stage':'Boyut profili hazırlanıyor','page':number,'total':len(doc)})
            doc.save(temporary,garbage=4,deflate=True,use_objstms=1)
        with fitz.open(source) as before,fitz.open(temporary) as after:
            if len(before)!=len(after):raise ValueError('Compression changed page count')
            for number,(a,b) in enumerate(zip(before,after),1):
                if a.get_text()!=b.get_text() or a.rect!=b.rect:raise ValueError(f'Compression changed text or dimensions: {number}')
                # Measured display change is reported, never called lossless.
                pa=a.get_pixmap(dpi=72,alpha=False);pb=b.get_pixmap(dpi=72,alpha=False)
                ia=Image.frombytes('RGB',(pa.width,pa.height),pa.samples);ib=Image.frombytes('RGB',(pb.width,pb.height),pb.samples)
                differences.append(sum(ImageStat.Stat(ImageChops.difference(ia,ib)).mean)/3/255)
        if temporary.stat().st_size>=source.stat().st_size:
            shutil.copyfile(source,temporary);changed=0;differences=[0.]
        publish(temporary,destination)
        return {'pdf':str(destination),'profile':profile,'before_bytes':source.stat().st_size,'after_bytes':destination.stat().st_size,
            'images_changed':changed,'lossless':changed==0,'text_identical':True,'max_render_mean_absolute_error_72dpi':max(differences,default=0),
            'jpeg_quality':quality,'image_dpi_cap':dpi,'note':'Lossy image changes require visual review; no lossy JBIG2.'}
    finally:temporary.unlink(missing_ok=True)


def preview(source,destination,pages='1-3'):
    from .smart_pdf import page_range, copy_navigation
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    sample=destination/'sample.pdf'
    if sample.exists():raise FileExistsError('Yeni önizleme klasörü seçin.')
    with fitz.open(source) as doc:
        indices=page_range(pages,len(doc),clip=True)
        with fitz.open() as out:
            for index in indices:out.insert_pdf(doc,from_page=index,to_page=index)
            copy_navigation(doc,out,indices);out.save(sample)
    reports=[]
    for profile in ('lossless','balanced','small'):
        target=destination/(profile+'.pdf');report=compress(sample,target,profile)
        with fitz.open(target) as doc:doc[0].get_pixmap(dpi=120,alpha=False).save(destination/(profile+'.png'))
        report.update(preview=str(destination/(profile+'.png')),profile=profile);reports.append(report)
    return {'sample_pages':[i+1 for i in indices],'profiles':reports,'sample':str(sample)}
