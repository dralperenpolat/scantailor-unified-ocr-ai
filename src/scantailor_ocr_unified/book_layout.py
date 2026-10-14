"""Explicit PDF placement, independent of thresholding and OCR engine."""
from __future__ import annotations
from pathlib import Path
import numpy as np
import cv2
from PIL import Image
from concurrent.futures import ThreadPoolExecutor

# Parallel scheduling does not change measurements or physical placement.
# Preserve the prior semantic digest so verified OCR/PDF caches remain valid.
LAYOUT_PIPELINE_ID = '8345f2576cf1b16dcc358b79ed163282f27a8396c14fdd04de0e6b12564b9ba7'

def body_height(path):
    with Image.open(path) as im:
        gray=np.asarray(im.convert('L'))
    scale=min(1.,1800/max(gray.shape))
    if scale<1:gray=cv2.resize(gray,None,fx=scale,fy=scale,interpolation=cv2.INTER_AREA)
    h,w=gray.shape
    if min(h,w)<40:return None
    binary=cv2.adaptiveThreshold(gray,255,cv2.ADAPTIVE_THRESH_GAUSSIAN_C,cv2.THRESH_BINARY_INV,31,12)
    _,_,stats,_=cv2.connectedComponentsWithStats(binary,8)
    s=stats[1:];min_h=max(3,h*.0025);max_h=max(10,h*.035)
    components=(s[:,2]>=3)&(s[:,3]>=3)&(s[:,2]<=max_h)&(s[:,3]<=max_h)&(s[:,4]>=7)
    components&=(s[:,0]>w*.08)&(s[:,0]+s[:,2]<w*.92)&(s[:,1]>h*.12)&(s[:,1]+s[:,3]<h*.85)
    glyphs=s[components];scores=[]
    # Form borders can connect otherwise separate vertical text lines into a
    # single wide component. Remove long rules only from orientation analysis;
    # the image, glyph measurement and PDF appearance remain untouched.
    rules=cv2.morphologyEx(binary,cv2.MORPH_OPEN,np.ones((1,75),np.uint8))
    rules|=cv2.morphologyEx(binary,cv2.MORPH_OPEN,np.ones((75,1),np.uint8))
    orientation_ink=binary & ~cv2.dilate(rules,np.ones((3,3),np.uint8))
    for vertical in (False,True):
        kernel=(3,17) if vertical else (17,3)
        joined=cv2.morphologyEx(orientation_ink,cv2.MORPH_CLOSE,cv2.getStructuringElement(cv2.MORPH_RECT,kernel))
        _,_,segments,_=cv2.connectedComponentsWithStats(joined,8);segments=segments[1:]
        lengths=segments[:,3] if vertical else segments[:,2];thickness=segments[:,2] if vertical else segments[:,3]
        use=(lengths>60)&(thickness>2)&(thickness<30)&(lengths>thickness*4)
        scores.append(int(segments[use,4].sum()))
    sideways=scores[1]>1500 and scores[1]>scores[0]*3
    if sideways:return {'pixels':None,'support':int(len(glyphs)),'confidence':0.,'sideways':True,'components':int(len(glyphs))}
    keep=(s[:,3]>=min_h)&(s[:,3]<=max_h)&(s[:,2]>=2)&(s[:,2]<s[:,3]*2.3)&(s[:,4]>=5)&(s[:,4]<s[:,2]*s[:,3]*.93)
    keep&=(s[:,0]>w*.08)&(s[:,0]+s[:,2]<w*.92)&(s[:,1]>h*.12)&(s[:,1]+s[:,3]<h*.85)
    heights=s[keep,3]
    if len(heights)<80:return None
    # Uppercase letters and index numbers can outvote lowercase letters. A
    # mode weighted towards tall components used to shrink index pages by 30%.
    # First reject punctuation relative to the upper letter-height quantile,
    # then estimate the lower (x-height) population, independent of that vote.
    cap=float(np.quantile(heights,.90))
    letters=heights[(heights>=cap*.52)&(heights<=cap*1.25)]
    if len(letters)<80:return None
    xheight=float(np.quantile(letters,.30))
    population=letters[(letters>=xheight*.80)&(letters<=xheight*1.20)]
    if len(population)<max(50,len(heights)*.12):return None
    return {'pixels':float(xheight/scale),'support':int(len(population)),
            'confidence':round(float(len(population)/len(letters)),3),'sideways':False,'components':int(len(heights))}

def plan(outputs,paper='a4',margin_mm=10.,target_x_mm=0.,notify=None,workers=1):
    if paper not in ('native','a4','a4_fill','a4_text'):raise ValueError('Bilinmeyen sayfa yerleşimi: '+paper)
    if not 0<=margin_mm<80:raise ValueError('Kenar payı 0–80 mm aralığında olmalı.')
    sizes=[]
    for source in outputs:
        with Image.open(source) as im:sizes.append(im.size)
    if not sizes:raise ValueError('Sayfa bulunamadı')
    capacities=[min((210-2*margin_mm)/w,(297-2*margin_mm)/h) for w,h in sizes]
    common=min(capacities);measurements=[None]*len(sizes);target=None
    # map preserves page order; only independent image measurements run in
    # parallel. Pool size bounds the number of decoded TIFFs held in memory.
    paths=[str(source)+'.ocr.tif' for source in outputs]
    workers=max(1,min(8,int(workers)))
    if workers==1:
        for n,path in enumerate(paths,1):
            measurements[n-1]=body_height(path)
            if notify:notify(n,len(outputs))
    else:
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for n,value in enumerate(pool.map(body_height,paths),1):
                measurements[n-1]=value
                if notify:notify(n,len(outputs))
    if paper=='a4_text':
        fits=[cap*m['pixels'] for cap,m in zip(capacities,measurements) if m and m.get('pixels') and m['confidence']>=.3]
        if fits:target=target_x_mm if target_x_mm>0 else float(np.quantile(fits,.25))*.98
    rows=[]
    reliable=[m['pixels'] for m in measurements if m and m.get('pixels') and m['confidence']>=.3]
    fallback=float(np.median(reliable)) if reliable else 0
    for n,((w,h),capacity,measurement) in enumerate(zip(sizes,capacities,measurements),1):
        warning='';pxmm=25.4/300 if paper=='native' else common
        if paper=='a4_fill':pxmm=capacity
        elif paper=='a4_text':
            height=measurement['pixels'] if measurement and measurement.get('pixels') and measurement['confidence']>=.3 else fallback
            if target and height:
                desired=target/height;pxmm=min(capacity,desired)
                if desired>capacity*1.015:warning=f'Ortak yazı ölçeği A4’e sığmadı; bu sayfa %{(1-capacity/desired)*100:.1f} küçültüldü. İçerik alanını kontrol edin.'
                if not measurement or measurement['confidence']<.3:warning+=' Yazı yüksekliği güvenilir ölçülemedi; kitap ortancası kullanıldı.'
            else:warning='Yazı ölçeği ölçülemedi; ortak piksel ölçeği korundu.'
        image_mm=[w*pxmm,h*pxmm];page_mm=image_mm if paper=='native' else [210,297]
        rows.append({'page':n,'source':str(outputs[n-1]),'paper':paper,'pxmm':pxmm,'image_mm':image_mm,'page_mm':page_mm,
                     'body_measurement':measurement,'body_mm':measurement['pixels']*pxmm if measurement and measurement.get('pixels') else None,'target_body_mm':target,'warning':warning})
    return {'schema':1,'paper':paper,'margin_mm':margin_mm,'pages':rows}
