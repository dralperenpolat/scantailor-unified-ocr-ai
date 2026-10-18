"""Synthetic multilingual OCR/threshold comparison; no real-book accuracy claim.

Ground truth is the exact text drawn into each fixture. Model loading is timed
separately from the engine's own recognition metric. User documents are not read.
"""
from pathlib import Path
import argparse
import json
import os
import sys
import time
import unicodedata


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',required=True)
    parser.add_argument('--work',required=True)
    parser.add_argument('--paddle',action='store_true')
    args=parser.parse_args()
    root=Path(args.root).resolve();work=Path(args.work).resolve()
    work.mkdir(parents=True,exist_ok=False)
    os.environ.update(STOU_ROOT=str(root),STOU_DATA=str(work/'data'),STOU_PYTHON=sys.executable,
                      PYTHONPATH=str(root/'src'),PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1',
                      OMP_NUM_THREADS='2',OMP_THREAD_LIMIT='2',CUDA_VISIBLE_DEVICES='-1')
    sys.path.insert(0,str(root/'src'))
    import cv2
    import fitz
    import numpy as np
    from PIL import Image,ImageDraw,ImageFont,ImageFilter
    from scantailor_ocr_unified.benchmark import scores
    from scantailor_ocr_unified.pdf_integrity import file_digest
    from scantailor_ocr_unified.runtime_layout import native_model_directory
    cv2.setNumThreads(2)
    font_path=Path(os.environ['WINDIR'])/'Fonts/arial.ttf'
    font=ImageFont.truetype(str(font_path),36)
    note=ImageFont.truetype(str(font_path),25)
    texts={
        'tur':[
            'İstanbul, İzmir ve Şanlıurfa: ışık, ölçü, görüş.',
            'Küçük harfler: ç, ğ, ı, i, ö, ş, ü; büyük harfler: Ç, Ğ, İ, Ö, Ş, Ü.',
            'Sayfa numarası 128; örnek tutar 1.234,56; oran %12,5.',
            'Bir kitabın kenarındaki küçük notlar da okunabilmelidir.',
            'Bu satır yalnızca optik karakter tanıma denemesi içindir.',
            'Aynı görüntü, bütün motorlara aynı çözünürlükte verilir.',
            'Karşılaştırmada harfler, sayılar ve noktalama işaretleri korunur.',
            'Dipnot 6: 8, 0, O, l, 1 ve I birbirine karıştırılmamalıdır.'],
        'deu':[
            'Übergröße, Straße und Bücher: ä, ö, ü und ß.',
            'Diese Seite enthält nur einen künstlichen Beispieltext.',
            'Die Fußnote bleibt auch bei kleiner Schrift lesbar.',
            'Seite 128; Betrag 1.234,56; Anteil 12,5 Prozent.'],
        'eng':[
            'Printed text and small footnotes should remain readable.',
            'This is a synthetic example for character recognition.',
            'Page 128; amount 1,234.56; ratio 12.5 percent.',
            'Footnote 6: distinguish 8, 0, O, l, 1 and I.']}
    fixtures=[]
    for language,lines in texts.items():
        clean=Image.new('L',(1800,1050),255);draw=ImageDraw.Draw(clean)
        for index,line in enumerate(lines):
            draw.text((90,80+index*95),line,font=note if index==len(lines)-1 else font,fill=20)
        for variant in (('clean','shaded') if language=='tur' else ('clean',)):
            image=clean.copy()
            if variant=='shaded':
                pixels=np.asarray(image.filter(ImageFilter.GaussianBlur(.65)),dtype=np.float32)
                illumination=np.linspace(.46,1,pixels.shape[1],dtype=np.float32)[None,:]
                noise=np.random.default_rng(60).normal(0,2,pixels.shape)
                image=Image.fromarray(np.clip(pixels*illumination+noise,0,255).astype(np.uint8))
            variants={'gray':np.asarray(image)}
            if language=='tur':
                variants['otsu']=cv2.threshold(variants['gray'],0,255,cv2.THRESH_BINARY|cv2.THRESH_OTSU)[1]
                for name,method,k in [('sauvola',cv2.ximgproc.BINARIZATION_SAUVOLA,.34),('wolf',cv2.ximgproc.BINARIZATION_WOLF,.5)]:
                    variants[name]=cv2.ximgproc.niBlackThreshold(variants['gray'],255,cv2.THRESH_BINARY,51,k,binarizationMethod=method)
            for threshold,pixels in variants.items():
                path=work/f'{language}-{variant}-{threshold}.png'
                Image.fromarray(pixels).save(path,dpi=(300,300))
                fixtures.append({'language':language,'variant':variant,'threshold':threshold,
                                 'image':str(path),'reference':'\n'.join(lines)})
    def normalized(text):return ' '.join(unicodedata.normalize('NFC',text).split())
    records=[]
    def record(fixture,engine,hypothesis,seconds,details):
        row={k:v for k,v in fixture.items() if k!='reference'}
        row.update(engine=engine,seconds=round(seconds,3),reference=fixture['reference'],hypothesis=hypothesis,
                   **scores(normalized(fixture['reference']),normalized(hypothesis)),details=details)
        row['normalization']='NFC; whitespace collapsed; case and punctuation preserved'
        records.append(row)
        print(json.dumps({k:row[k] for k in ('language','variant','threshold','engine','CER','WER','seconds')},ensure_ascii=False),flush=True)
    for quality in ('fast','best'):
        for fixture in fixtures:
            # Match the application's RGB OCR adapter. MuPDF's gray-Pixmap OCR
            # path can return an empty layer without reporting an error.
            path=Path(fixture['image']);pix=fitz.Pixmap(fitz.csRGB,fitz.Pixmap(path));pix.set_dpi(300,300)
            directory=root/'resources'/('tessdata_'+quality)
            started=time.perf_counter();data=pix.pdfocr_tobytes(language=fixture['language'],tessdata=native_model_directory(directory))
            with fitz.open(stream=data,filetype='pdf') as doc:hypothesis=doc[0].get_text()
            record(fixture,'tesseract_'+quality,hypothesis,time.perf_counter()-started,
                   {'model_sha256':file_digest(directory/(fixture['language']+'.traineddata'))})
    if args.paddle:
        from scantailor_ocr_unified.ocr_backend import PaddleClient
        client=PaddleClient(root,'cpu')
        try:
            for version in ('PP-OCRv5','PP-OCRv6'):
                first=True
                for fixture in fixtures:
                    if fixture['threshold']!='gray':continue
                    started=time.perf_counter()
                    result=client.recognize(fixture['image'],{'tur':'tr','deu':'de','eng':'en'}[fixture['language']],'cpu',version)
                    details={key:result.get(key) for key in ('detector','recognizer','detector_limit','seconds')}
                    details['first_call_for_version']=first;first=False
                    record(fixture,version,'\n'.join(row['text'] for row in result['lines']),time.perf_counter()-started,details)
        finally:client.close()
    result={'reference_kind':'synthetic rendered ground truth','font':str(font_path),'font_sha256':file_digest(font_path),
            'real_book_accuracy_measured':False,'records':records}
    (work/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8')


if __name__=='__main__':main()
