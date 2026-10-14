"""Searchable PDF export with separate display and OCR images.

Usage: python kitap_pdf_modulu_v1.py project.json output.pdf
The two images must describe the SAME page geometry, crop and orientation.
This module does not estimate geometry or infer page order. It verifies lossless
compression relative to the supplied display TIFF, not the original photo.
Dependencies: Pillow, PyMuPDF, pypdf, img2pdf; optional jbig2enc (generic mode).
"""
from __future__ import annotations
import argparse, gc, hashlib, io, json, subprocess, sys, time, uuid, re, os, shutil, tempfile
from pathlib import Path
from .runtime_layout import native_model_directory
from PIL import Image
from .runtime_layout import resource_path
import fitz
from pypdf import PdfReader, PdfWriter
from pypdf.generic import NameObject

import img2pdf

# Semantic identity of the verified image/OCR transform. Its initial value is
# the 0.3.5 file digest so existing recognition and page caches stay usable.
# Temporary-directory changes do not change pixels or OCR coordinates. Bump
# this identifier for any future change to those output semantics.
PIXEL_PIPELINE_ID = 'bbb713d9aec2a0c597bc5723d9c75e4fee8c13bc4a94efe48f0e9b135f0755c2'

def digest(data): return hashlib.sha256(data).hexdigest()

def recognition_signature(conf,engine,tessdata,language):
    """OCR models/settings, independent of PDF size and visible thresholding."""
    if engine=='tesseract':
        return [engine,fitz.VersionBind,language,[(code,digest((tessdata/(code+'.traineddata')).read_bytes())) for code in language.split('+')]]
    root=Path(conf['root']);device=conf.get('ocr_device','cpu');ready=resource_path('paddle-'+device+'-ready.json',root)
    version=json.loads(ready.read_text(encoding='utf8')) if ready.exists() else {}
    from .ocr_backend import OCR_PIPELINE_ID
    return [engine,conf.get('paddle_language','de'),device,'PP-OCRv5',version.get('paddle'),version.get('paddleocr'),
            digest(Path(__file__).with_name('paddle_runtime.py').read_bytes()),OCR_PIPELINE_ID]

def text_layer_cache(candidate,rotation,signature,directory,recognize,notify):
    if not directory:return recognize()
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    key=digest(json.dumps(['ocr-layer-v1',candidate.size,rotation,signature,digest(candidate.tobytes())],sort_keys=True).encode())
    # Retain older full-hash names; new entries use 128 bits plus a full-key
    # receipt so long book names do not push Windows paths beyond MAX_PATH.
    stem=key if (directory/(key+'.pdf')).exists() else key[:32]
    pdf=directory/(stem+'.pdf');receipt=directory/(stem+'.json')
    if pdf.exists() and receipt.exists():
        try:
            data=pdf.read_bytes();record=json.loads(receipt.read_text(encoding='utf8'))
            if digest(data)==record['sha256'] and record.get('cache_key',key)==key:
                notify('OCR metni önbellekten alındı; görünür sayfa ayrı oluşturuluyor.')
                return data
        except (OSError,ValueError,KeyError):pass
    data=recognize()
    # Store text only: no duplicate page bitmap in the recognition cache.
    writer=PdfWriter();writer.clone_document_from_reader(PdfReader(io.BytesIO(data)));writer.remove_images()
    stream=io.BytesIO();writer.write(stream);data=stream.getvalue()
    for destination,content in ((pdf,data),(receipt,json.dumps({'sha256':digest(data),'cache_key':key}).encode())):
        temp=directory/('.write-'+uuid.uuid4().hex[:12]+'.tmp')
        temp.write_bytes(content);os.replace(temp,destination)
    return data

def verify_page(pdf_path, source):
    with fitz.open(pdf_path) as doc:
        if len(doc)!=1: raise ValueError('Expected one PDF page')
        images=doc[0].get_images(full=True)
        if len(images)!=1: raise ValueError('Expected exactly one display image')
        pix=fitz.Pixmap(doc,images[0][0])
        with Image.open(source) as im:
            mode='RGB' if im.mode=='RGB' else 'L'
            if im.mode not in ('1','L','RGB') or pix.n!=(3 if mode=='RGB' else 1) or pix.alpha or im.size!=(pix.width,pix.height):
                raise ValueError('Display image format or dimensions changed')
            expected=digest(im.convert(mode).tobytes())
        if digest(pix.samples)!=expected: raise ValueError('Pixel integrity check failed')
        return expected

def ocr_protection(gray,conf,notify=lambda message:None):
    """Read OCR on the uncleaned gray page before irreversible-looking edits.

    The very same text-only cache is reused by final PDF export. Coordinates
    are normalized to the original page, independent of A4 sizing/thresholds.
    """
    candidate=gray.copy();candidate.thumbnail((4200,4200),Image.Resampling.LANCZOS)
    engine=conf.get('ocr_engine','tesseract');tessdata=Path(conf['tessdata']);language=conf.get('language','deu')
    signature=recognition_signature(conf,engine,tessdata,language)
    def recognize():
        if engine=='paddle':
            from .ocr_backend import paddle_pdf
            with tempfile.TemporaryDirectory(prefix='stou_protect_') as work:
                return paddle_pdf(candidate,conf,Path(work))
        stream=io.BytesIO();candidate.convert('RGB').save(stream,format='PNG')
        pix=fitz.Pixmap(stream.getvalue());pix.set_dpi(300,300)
        return pix.pdfocr_tobytes(language=language,tessdata=native_model_directory(tessdata))
    data=text_layer_cache(candidate,0,signature,conf.get('ocr_cache'),recognize,notify)
    boxes=[]
    with fitz.open(stream=data,filetype='pdf') as doc:
        page=doc[0];w,h=page.rect.width,page.rect.height
        for word in page.get_text('words'):
            txt=word[4].strip()
            if len(txt)>=2 and any(c.isalnum() for c in txt) or txt.isdigit():
                boxes.append([word[0]/w,word[1]/h,word[2]/w,word[3]/h])
    return boxes

def export(project_path, destination, notify=print):
    project_path=Path(project_path).resolve();destination=Path(destination).resolve()
    if destination.exists(): raise FileExistsError(destination)
    conf=json.loads(project_path.read_text(encoding='utf8'))
    if not conf.get('pages'): raise ValueError('No pages in project')
    resolve=lambda s: (project_path.parent/s).resolve()
    tessdata=resolve(conf.get('tessdata','.'));language=conf.get('language','deu')
    engine=conf.get('ocr_engine','tesseract')
    if engine not in ('tesseract','paddle','none'):raise ValueError('Unknown OCR engine: '+engine)
    if engine=='tesseract':
        for lang in language.split('+'):
            if not (tessdata/(lang+'.traineddata')).is_file(): raise FileNotFoundError(f'OCR language missing: {lang}')
    ocr_signature=recognition_signature(conf,engine,tessdata,language) if engine!='none' and conf.get('ocr_cache') else None
    jbig=resolve(conf['jbig2']) if conf.get('jbig2') else None
    if jbig and not jbig.is_file(): raise FileNotFoundError(jbig)
    destination.parent.mkdir(parents=True,exist_ok=True)
    # Nested book/cache paths exceed MAX_PATH in Paddle's image/JSON I/O on
    # Windows. Keep only transient files in a short, private system temp dir.
    work_root=Path(tempfile.gettempdir()).resolve()
    work=Path(tempfile.mkdtemp(prefix='stou_pdf_',dir=work_root)).resolve()
    total=PdfWriter();checks=[];start=time.monotonic()
    for n,entry in enumerate(conf['pages'],1):
        visible=resolve(entry['display_image']);recognition=resolve(entry.get('ocr_image',entry['display_image']))
        if visible==destination or recognition==destination: raise ValueError('Output overlaps an input')
        hashes={str(p):digest(p.read_bytes()) for p in {visible,recognition}}
        with Image.open(visible) as im:
            if im.mode not in ('1','L','RGB') or im.format!='TIFF' or getattr(im,'n_frames',1)!=1:
                raise ValueError(f'Expected a single-frame binary or grayscale TIFF: {visible}')
            binary=im.mode=='1'
            vw,vh=im.size
        with Image.open(recognition) as im:
            if abs(im.width/im.height-vw/vh)>0.0008:
                raise ValueError('Display and OCR copies have different aspect ratios; coordinate mapping is required')
            # Large intermediates need not be recognized at the display resolution.
            # The immutable display image is never downsampled.
            ocr=im.convert('L')
            max_edge=int(conf.get('ocr_max_edge',4200))
            if max(ocr.size)>max_edge: ocr.thumbnail((max_edge,max_edge),Image.Resampling.LANCZOS)
        page_mm=entry.get('page_mm',conf.get('page_mm',[210,297]))
        pw,ph=[float(v)*72/25.4 for v in page_mm]
        margin=float(conf.get('margin_mm',0))*72/25.4
        scale=min((pw-2*margin)/vw,(ph-2*margin)/vh)
        if scale<=0: raise ValueError('Margins exceed page')
        iw,ih=vw*scale,vh*scale
        if entry.get('image_mm'):
            iw,ih=[float(v)*72/25.4 for v in entry['image_mm']]
            if iw<=0 or ih<=0 or iw>pw+.01 or ih>ph+.01:raise ValueError('Physical image size exceeds PDF page')
            if abs(iw/ih-vw/vh)>.001:raise ValueError('Physical image aspect ratio mismatch')
        layout=lambda w,h,dpi:(pw,ph,iw,ih)
        ccitt=work/(f'{n:05d}_ccitt.pdf' if binary else f'{n:05d}_flate.pdf')
        ccitt.write_bytes(img2pdf.convert(str(visible),engine=img2pdf.Engine.internal,layout_fun=layout))
        expected=verify_page(ccitt,visible)
        best=ccitt
        if jbig and binary:
            raw=work/f'{n:05d}.jb2'
            with raw.open('xb') as stream:
                run=subprocess.run([str(jbig),'-d','-p',str(visible)],stdout=stream,stderr=subprocess.PIPE,
                    timeout=180,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            if run.returncode: raise RuntimeError(run.stderr.decode(errors='replace'))
            w=PdfWriter();w.clone_document_from_reader(PdfReader(ccitt))
            image=next(obj.get_object() for obj in w.pages[0]['/Resources']['/XObject'].values()
                       if obj.get_object().get('/Subtype')=='/Image')
            image._data=raw.read_bytes();image[NameObject('/Filter')]=NameObject('/JBIG2Decode')
            image.pop('/DecodeParms',None);image.pop('/Decode',None)
            jp=work/f'{n:05d}_jbig2.pdf'
            with jp.open('xb') as f:w.write(f)
            verify_page(jp,visible)
            best=min([ccitt,jp],key=lambda p:p.stat().st_size)
        if engine=='none':
            out=best;chars=0;rotation=0
        else:
            candidates=[]
            for rotation in entry.get('ocr_rotations',[0]):
                if rotation not in [0,90,180,270]:raise ValueError('Invalid OCR rotation')
                candidate=ocr.rotate(-rotation,expand=True) if rotation else ocr
                def recognize():
                    if engine=='paddle':
                        from .ocr_backend import paddle_pdf
                        return paddle_pdf(candidate,conf,work)
                    stream=io.BytesIO();candidate.convert('RGB').save(stream,format='PNG',compress_level=1)
                    pix=fitz.Pixmap(stream.getvalue());pix.set_dpi(300,300)
                    return pix.pdfocr_tobytes(language=language,tessdata=native_model_directory(tessdata))
                candidate_pdf=text_layer_cache(candidate,rotation,ocr_signature,conf.get('ocr_cache'),recognize,notify)
                with fitz.open(stream=candidate_pdf,filetype='pdf') as recognized:
                    txt=recognized[0].get_text()
                    words=re.findall(r'[^\W\d_]{3,}',txt,flags=re.UNICODE)
                    score=sum(min(len(word),16) for word in words)
                candidates.append((score,rotation,candidate_pdf))
            _,rotation,text_pdf=max(candidates,key=lambda row:row[0])
            tw=PdfWriter();tw.clone_document_from_reader(PdfReader(io.BytesIO(text_pdf)));tw.remove_images()
            text_page=tw.pages[0]
            one=PdfWriter();one.clone_document_from_reader(PdfReader(best))
            image_page=one.pages[0]
            rw,rh=float(text_page.mediabox.width),float(text_page.mediabox.height)
            ox,oy=(pw-iw)/2,(ph-ih)/2
            # Undo recognition-only rotation in PDF bottom-left coordinates.
            if rotation==90:matrix=[0,ih/rw,-iw/rh,0,ox+iw,oy]
            elif rotation==180:matrix=[-iw/rw,0,0,-ih/rh,ox+iw,oy+ih]
            elif rotation==270:matrix=[0,-ih/rw,iw/rh,0,ox,oy+ih]
            else:matrix=[iw/rw,0,0,ih/rh,ox,oy]
            image_page.merge_transformed_page(text_page,matrix)
            out=work/f'{n:05d}_ocr.pdf'
            with out.open('xb') as stream:one.write(stream)
            verify_page(out,visible)
            with fitz.open(best) as before,fitz.open(out) as after:
                pa=before[0].get_pixmap(dpi=120,alpha=False);pb=after[0].get_pixmap(dpi=120,alpha=False)
                if pa.samples!=pb.samples:raise ValueError('OCR layer changed the visible page')
                chars=len(after[0].get_text().strip())
                if chars<15 and not entry.get('allow_empty_ocr',False):
                    raise ValueError(f'OCR produced too little text on page {n}')
        if not all(digest(Path(p).read_bytes())==h for p,h in hashes.items()):raise ValueError('Input changed during export')
        total.add_page(PdfReader(out).pages[0])
        checks.append({'page':n,'id':entry.get('id',str(n)),'pixels_sha256':expected,'pixel_identical':True,
                       'ocr_visible_pixels_unchanged':True,'ocr_characters':chars,'compression':best.stem.split('_',1)[1],
                       'display_bytes':visible.stat().st_size,'single_page_pdf_bytes':out.stat().st_size,
                       'geometry_id':entry.get('geometry_id'),'source_hashes':hashes})
        checks[-1]['ocr_rotation']=rotation
        checks[-1]['ocr_engine']=engine
        notify(f'{n}/{len(conf["pages"])} — {entry.get("id",visible.name)} — {chars} OCR characters')
        gc.collect()
    # Avoid overwriting an existing destination even if another process created it.
    with destination.open('xb') as stream:total.write(stream)
    with fitz.open(destination) as pdf:
        if len(pdf)!=len(checks):raise ValueError('Page count mismatch')
        for page,check in zip(pdf,checks):
            pix=fitz.Pixmap(pdf,page.get_images(full=True)[0][0])
            if digest(pix.samples)!=check['pixels_sha256']:raise ValueError('Final assembly changed pixels')
    result={'output':str(destination),'pages':len(checks),'bytes':destination.stat().st_size,
            'seconds':round(time.monotonic()-start,2),'scope':'lossless relative to processed display TIFF (binary or grayscale), not raw photograph',
            'checks':checks}
    (work/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    destination.with_suffix('.verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    if not conf.get('keep_export_work',False):
        # Only this call's fresh, private UUID directory can be reclaimed.
        if work.resolve().parent!=work_root or not work.name.startswith('stou_pdf_'):
            raise ValueError('Geçici çıktı yolu doğrulanamadı.')
        shutil.rmtree(work)
    return result

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('project');ap.add_argument('output')
    a=ap.parse_args()
    print(json.dumps(export(a.project,a.output),ensure_ascii=False,indent=2))
