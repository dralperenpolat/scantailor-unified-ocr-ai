"""Conservative removal of detached peripheral scan debris.

No text synthesis, cropping or change of coordinate system. Ambiguous figures,
rules and objects touching glyphs are retained. Masks are saved for inspection.
"""
from __future__ import annotations
import cv2
import numpy as np
from PIL import Image

VERSION='peripheral-v1'

def analyze(gray,allow_closed_frame=False,protected_boxes=(),reviewed_components=()):
    original=np.asarray(gray,dtype=np.uint8)
    scale=min(1.,1800/max(original.shape))
    g=cv2.resize(original,None,fx=scale,fy=scale,interpolation=cv2.INTER_AREA) if scale<1 else original
    h,w=g.shape
    empty=np.zeros_like(g)
    if min(g.shape)<64:return empty,{'version':VERSION,'removed':[],'uncertain':[]}
    cut,ink=cv2.threshold(g,0,255,cv2.THRESH_BINARY_INV|cv2.THRESH_OTSU)
    _,labels,stats,_=cv2.connectedComponentsWithStats(ink,8)
    s=stats[1:]
    # Estimate glyph height without allowing a large finger to set the scale.
    candidates=(s[:,3]>=max(5,h*.004))&(s[:,3]<h*.025)&(s[:,2]>=2)&(s[:,2]<s[:,3]*2)&(s[:,4]>8)
    heights=s[candidates,3]
    if len(heights)<80:
        return empty,{'version':VERSION,'removed':[],'uncertain':['Seyrek veya resimli sayfa: otomatik silme uygulanmadı.']}
    xh=float(np.quantile(heights,.35))
    glyph=(s[:,3]>=xh*.55)&(s[:,3]<=xh*2)&(s[:,2]>=2)&(s[:,2]<=xh*3)&(s[:,4]>=7)&(s[:,4]<s[:,2]*s[:,3]*.94)
    glyph&=(s[:,0]>w*.05)&(s[:,0]+s[:,2]<w*.95)&(s[:,1]>h*.04)&(s[:,1]+s[:,3]<h*.96)
    # Edge fragments can resemble individual letters. Require a neighbouring
    # run for estimating the text extent, but protect isolated numeral-sized
    # components separately below.
    glyph_ink=np.isin(labels,np.flatnonzero(glyph)+1).astype(np.uint8)*255
    groups=cv2.dilate(glyph_ink,cv2.getStructuringElement(cv2.MORPH_RECT,(round(xh*5)|1,round(xh*.8)|1)))
    _,group_ids,_,_=cv2.connectedComponentsWithStats(groups,8)
    indices=np.flatnonzero(glyph)
    groups_at=[int(group_ids[int(s[i,1]+s[i,3]//2),int(s[i,0]+s[i,2]//2)]) for i in indices]
    counts=np.bincount(groups_at)
    supported=np.zeros(len(s),bool)
    for i,group in zip(indices,groups_at):supported[i]=group>0 and counts[group]>=4
    gs=s[supported]
    if len(gs)<80:return empty,{'version':VERSION,'removed':[],'uncertain':['Metin alanı güvenilir bulunamadı.']}
    # All detected glyphs count, including isolated page and marginal numbers.
    x0,y0=gs[:,:2].min(axis=0);x1,y1=(gs[:,:2]+gs[:,2:4]).max(axis=0)
    protected=np.isin(labels,np.flatnonzero(supported)+1).astype(np.uint8)*255
    isolated=np.isin(labels,np.flatnonzero(glyph & ~supported)+1).astype(np.uint8)*255
    isolated=cv2.dilate(isolated,np.ones((3,3),np.uint8))
    radius=max(2,round(xh*.4));protected=cv2.dilate(protected,np.ones((2*radius+1,2*radius+1),np.uint8))
    for box in protected_boxes:
        bx0,by0,bx1,by1=[int(round(v*d)) for v,d in zip(box,(w,h,w,h))]
        bx0,by0=max(0,bx0-radius),max(0,by0-radius);bx1,by1=min(w,bx1+radius),min(h,by1+radius)
        if bx0<bx1 and by0<by1:protected[by0:by1,bx0:bx1]=255
    mask=np.zeros_like(g);removed=[];uncertain=[]
    # Adaptive edges connect the pale portions of a book rim; global ink keeps
    # the shaded interior of fingers. Neither threshold changes the output.
    edge=cv2.adaptiveThreshold(g,255,cv2.ADAPTIVE_THRESH_GAUSSIAN_C,cv2.THRESH_BINARY_INV,31,12)
    debris=cv2.morphologyEx(ink|edge,cv2.MORPH_CLOSE,np.ones((3,3),np.uint8))
    _,debris_labels,debris_stats,_=cv2.connectedComponentsWithStats(debris,8)
    for label,(x,y,cw,ch,area) in enumerate(debris_stats[1:],1):
        heading_shape=cw>ch*1.2 and ch<xh*6
        large=ch>xh*2.25 and area>xh*xh*2 and not heading_shape
        rule=(cw>xh*15 and ch<xh*.55) or (ch>xh*15 and cw<xh*.55)
        if not (large or rule):continue
        component=(debris_labels[y:y+ch,x:x+cw]==label).astype(np.uint8)
        yy,xx=np.nonzero(component);xx=xx+x;yy=yy+y
        # The interior rectangle is conservative: a real table has substantial
        # ink inside it, whereas the book's outer edge surrounds all text.
        outside=(xx<x0-xh*.7)|(xx>x1+xh*.7)|(yy<y0-xh*.7)|(yy>y1+xh*.7)
        fraction=float(outside.mean())
        sides=sum((x<x0-xh*.7,x+cw>x1+xh*.7,y<y0-xh*.7,y+ch>y1+xh*.7))
        # Do not erase a closed rectangle: it may be an intentional figure box.
        closed=False
        if cw>xh*10 and ch>xh*10:
            band=max(2,round(xh))
            top=np.any(component[:band],axis=0).mean();bottom=np.any(component[-band:],axis=0).mean()
            left=np.any(component[:,:band],axis=1).mean();right=np.any(component[:,-band:],axis=1).mean()
            closed=min(top,bottom,left,right)>.72
        core_hits=np.count_nonzero(component & (protected[y:y+ch,x:x+cw]>0))
        reviewed=False
        for bx,by,bw,bh in reviewed_components:
            bx,by,bw,bh=bx*w,by*h,bw*w,bh*h
            intersection=max(0,min(x+cw,bx+bw)-max(x,bx))*max(0,min(y+ch,by+bh)-max(y,by))
            if intersection/max(1,cw*ch+bw*bh-intersection)>.8:reviewed=True;break
        frame=(cw>w*.5 or ch>h*.5) and area/(cw*ch)<.09 and fraction>.85
        # A clipped, broad shaded object at the outer corner of a dense text
        # page is typical of a holding finger. Sparse covers / figures remain.
        flat=max(np.mean(component[0]),np.mean(component[-1]),np.mean(component[:,0]),np.mean(component[:,-1]))
        radius_inside=float(cv2.distanceTransform(component,cv2.DIST_L2,3).max()) if cw<w*.22 and ch<h*.22 else 0
        bulb=len(gs)>500 and cw<w*.22 and ch<h*.22 and flat>.5 and radius_inside>xh*.65 and sides>0
        eligible=fraction>=.985 or frame or bulb
        if not reviewed and (not eligible or sides==0 or (closed and not allow_closed_frame) or core_hits>max(5,area*.003)):
            if (area>xh*xh*10 and sides>0):uncertain.append({'box':[int(v) for v in (x,y,cw,ch)],'outside_text':round(fraction,3),'core_hits':int(core_hits),'reason':'Metin / tabloyla çakışabilecek bölge korundu.'})
            continue
        candidate=np.zeros_like(g)
        # Thick objects get their full contour interior (e.g. a shaded finger).
        candidate[y:y+ch,x:x+cw]=component*255
        contours,_=cv2.findContours(candidate,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_SIMPLE)
        if cw<xh*15 and ch<xh*35:
            cv2.drawContours(candidate,contours,-1,255,cv2.FILLED)
        padding=max(3,round(xh));candidate=cv2.dilate(candidate,np.ones((2*padding+1,2*padding+1),np.uint8))
        # Trim padding at text. A one-pixel contact must not force a whole
        # detached finger to remain (e.g. an index number close to its edge).
        candidate[protected>0]=0
        # An isolated page number may not belong to a text run. Keep it even
        # if a nearby finger or rim grows into its neighbourhood.
        candidate[isolated>0]=0
        mask|=candidate
        removed.append({'box':[int(v) for v in (x,y,cw,ch)],'area':int(area),'outside_text':round(fraction,4),'visually_reviewed':reviewed})
    if np.count_nonzero(mask)>g.size*.07:
        return empty,{'version':VERSION,'removed':[],'uncertain':['Önerilen silme alanı büyük; görüntü korundu.']}
    return mask,{'version':VERSION,'analysis_size':[w,h],'glyph_height':xh,'text_box':[int(v) for v in (x0,y0,x1,y1)],
                 'removed':removed,'uncertain':uncertain,'mask_fraction':float(np.count_nonzero(mask)/g.size)}

def apply(gray,mask):
    g=np.asarray(gray).copy()
    if mask.shape!=g.shape:mask=cv2.resize(mask,(g.shape[1],g.shape[0]),interpolation=cv2.INTER_NEAREST)
    g[mask>0]=255
    return g
