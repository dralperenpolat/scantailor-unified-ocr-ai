"""Optional processing of an already geometrically corrected page.

No recognition, text replacement or generated pixels. Line fitting is deliberately
limited to small, coherent residual bends; uncertain/large distortions are skipped.
Both visible and recognition images use the same displacement field.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
from PIL import Image

VERSION='output-finish-2'


def analysis_image(gray):
    gray=np.asarray(gray,dtype=np.uint8)
    scale=min(1.,1800/max(gray.shape))
    return (cv2.resize(gray,None,fx=scale,fy=scale,interpolation=cv2.INTER_AREA) if scale<1 else gray),scale


def glyphs(gray):
    ink=cv2.adaptiveThreshold(gray,255,cv2.ADAPTIVE_THRESH_GAUSSIAN_C,cv2.THRESH_BINARY_INV,41,12)
    _,labels,stats,_=cv2.connectedComponentsWithStats(ink,8)
    s=stats[1:];h,w=gray.shape
    keep=(s[:,3]>=4)&(s[:,3]<h*.025)&(s[:,2]>=2)&(s[:,2]<s[:,3]*2.5)&(s[:,4]>=6)&(s[:,4]<s[:,2]*s[:,3]*.96)
    return s[keep],labels,stats,keep


def residual_field(gray):
    """Fit robust line bottoms and return a bounded inverse vertical map."""
    g,scale=analysis_image(gray);h,w=g.shape;s,_,_,_=glyphs(g)
    report={'applied':False,'reason':'Yeterli güvenilir satır bulunamadı.','lines':0}
    if len(s)<120:return None,report
    xh=float(np.quantile(s[:,3],.55));centres=s[:,1]+s[:,3]*.5
    hist=np.bincount(np.clip(centres.astype(int),0,h-1),minlength=h).astype(np.float32)
    smooth=cv2.GaussianBlur(hist.reshape(-1,1),(1,0),sigmaX=0,sigmaY=max(1,xh*.15)).ravel()
    peaks=[i for i in range(1,h-1) if smooth[i]>smooth[i-1] and smooth[i]>=smooth[i+1] and smooth[i]>1.]
    chosen=[]
    for p in sorted(peaks,key=lambda i:smooth[i],reverse=True):
        if all(abs(p-q)>xh*.9 for q in chosen):chosen.append(p)
    curves=[];errors=[];validation=[]
    for p in sorted(chosen):
        row=s[np.abs(centres-p)<xh*.58]
        if len(row)<24:continue
        x=row[:,0]+row[:,2]*.5;y=row[:,1]+row[:,3]
        order=np.argsort(x);x=x[order];y=y[order].astype(float)
        if np.ptp(x)<w*.28:continue
        xn=(x-w/2)/w;mask=np.ones(len(x),bool)
        for _ in range(3):
            if mask.sum()<18:break
            coef=np.polyfit(xn[mask],y[mask],2);err=y-np.polyval(coef,xn)
            mask=np.abs(err-np.median(err[mask]))<max(1.2,xh*.15)
        if mask.sum()<18 or np.ptp(x[mask])<w*.28:continue
        # Held-out glyphs must agree with a fit made on other glyphs.
        indices=np.flatnonzero(mask);train=indices[::2];test=indices[1::2]
        if len(test)<8:continue
        check=np.polyfit(xn[train],y[train],2);pred=np.polyval(check,xn[test]);target=float(np.median(y[mask]))
        before=float(np.mean(np.abs(y[test]-target)));after=float(np.mean(np.abs(y[test]-pred)))
        full=np.polyval(coef,(np.arange(w)-w/2)/w);delta=full-target
        left,right=int(x[mask].min()),int(x[mask].max())
        amplitude=float(np.ptp(delta[left:right+1]))
        if amplitude<1.2 or amplitude>xh*.65 or after>max(.6,before*.75):continue
        if np.max(np.abs(delta[left:right+1]))>xh*.65:continue
        # Do not extend a text-line estimate into another column or a picture.
        weight=np.zeros(w,np.float32);weight[left:right+1]=1
        weight=cv2.GaussianBlur(weight.reshape(1,-1),(0,1),max(1,xh)).ravel()
        curves.append((target,(delta*weight).astype(np.float32)));errors.append((before,after))
        validation.append((x[test].astype(np.float32),y[test].astype(np.float32),target))
    report['lines']=len(curves)
    if len(curves)<5:return None,report
    field=np.zeros((h,w),np.float32);nodes=[(0,np.zeros(w,np.float32))]
    for i,(y,d) in enumerate(curves):
        if i==0 or y-curves[i-1][0]>xh*5:nodes.append((max(0,y-xh*1.5),np.zeros(w,np.float32)))
        nodes.append((y,d))
        if i+1==len(curves) or curves[i+1][0]-y>xh*5:nodes.append((min(h-1,y+xh*1.5),np.zeros(w,np.float32)))
    nodes.append((h-1,np.zeros(w,np.float32)));nodes.sort(key=lambda p:p[0])
    for (y0,d0),(y1,d1) in zip(nodes,nodes[1:]):
        a,b=int(np.ceil(y0)),int(np.ceil(y1))
        if b<=a:continue
        t=(np.arange(a,b,dtype=np.float32)-y0)/max(.01,y1-y0)
        field[a:b]=d0[None,:]*(1-t[:,None])+d1[None,:]*t[:,None]
    # Gentle regularization prevents independent glyph fits from squeezing
    # the space between neighbouring baselines. It does not relax the guard.
    field=cv2.GaussianBlur(field,(0,0),sigmaX=max(2,xh),sigmaY=max(3,xh*1.5))
    stretch=1+np.diff(field,axis=0)
    if stretch.min()<.88 or stretch.max()>1.12:
        report['reason']='Satır aralığını fazla değiştirecek öneri uygulanmadı.';return None,report
    before_values=[];after_values=[]
    for x,y,target in validation:
        displacement=cv2.remap(field,x[None,:],y[None,:],cv2.INTER_LINEAR).ravel()
        before_values.extend(np.abs(y-target));after_values.extend(np.abs(y-displacement-target))
    before_error=float(np.mean(before_values));after_error=float(np.mean(after_values))
    if before_error<.6 or after_error>before_error*.85 or before_error-after_error<.12:
        report['reason']='Son düzeltmede yeterli iyileşme doğrulanmadı; görüntü korundu.';return None,report
    report.update(applied=True,reason='Kalan küçük satır eğrilikleri düzeltildi.',max_shift_px=round(float(abs(field).max()/scale),2),
                  before_error=round(before_error,3),after_error=round(after_error,3))
    return field,report


def remap_vertical(image,field):
    src=np.asarray(image);h,w=src.shape[:2];fh,fw=field.shape;out=np.empty_like(src)
    x=np.arange(w,dtype=np.float32)[None,:]
    # Bounded strips, including interpolation halo, avoid full-page float maps.
    for top in range(0,h,128):
        end=min(h,top+128);y=np.arange(top,end,dtype=np.float32)[:,None]
        small_x=np.broadcast_to(x*(fw-1)/max(1,w-1),(end-top,w)).copy()
        small_y=np.broadcast_to(y*(fh-1)/max(1,h-1),(end-top,w)).copy()
        delta=cv2.remap(field,small_x,small_y,cv2.INTER_LINEAR)*h/fh
        out[top:end]=cv2.remap(src,np.broadcast_to(x,(end-top,w)).copy(),np.broadcast_to(y,(end-top,w))+delta,cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT,borderValue=255 if src.ndim==2 else (255,255,255))
    return out


def tonal_finish(image,white=False,fine=False):
    src=np.asarray(image,dtype=np.uint8);gray=cv2.cvtColor(src,cv2.COLOR_RGB2GRAY) if src.ndim==3 else src
    small,scale=analysis_image(gray);s,labels,stats,keep=glyphs(small)
    if len(s)<80:return src.copy(),{'applied':False,'reason':'Seyrek / resimli sayfa korundu.'}
    xh=float(np.quantile(s[:,3],.65));kernel=max(15,int(xh*4)|1)
    background=cv2.morphologyEx(small,cv2.MORPH_CLOSE,cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(kernel,kernel)))
    background=cv2.GaussianBlur(background,(0,0),max(2,xh))
    # Preserve large connected graphics; this is a conservative heuristic,
    # not a semantic picture detector or a replacement for native Picture Zones.
    protect=np.zeros_like(small)
    for x,y,w,h,area in stats[1:]:
        if w>xh*9 and h>xh*9:protect[y:y+h,x:x+w]=255
    paper_balance=np.ones(3,np.float32)
    if src.ndim==3:
        rgb_small=cv2.resize(src,(small.shape[1],small.shape[0]),interpolation=cv2.INTER_AREA)
        paper_pixels=rgb_small[(protect==0)&(small>=max(120,float(np.quantile(small,.7))))]
        if len(paper_pixels)>=100:
            paper=np.median(paper_pixels,axis=0).astype(np.float32)
            # A moderate paper/illumination cast is not colored ink. Judge
            # color relative to this cast; preserve saturated backgrounds.
            if paper.min()>=120 and np.ptp(paper)<=85:
                paper_balance=float(np.dot(paper,[.299,.587,.114]))/paper
    mask=np.isin(labels,np.flatnonzero(keep)+1).astype(np.uint8)*255
    small_glyphs=(stats[1:,3]>=3)&(stats[1:,3]<xh*.85)&(stats[1:,2]>=1)&(stats[1:,2]<xh*2)&(stats[1:,4]>=3)
    fine_mask=np.isin(labels,np.flatnonzero(small_glyphs)+1).astype(np.uint8)*255
    fine_mask=cv2.dilate(fine_mask,np.ones((3,3),np.uint8));fine_mask[protect>0]=0
    height,width=gray.shape;result=np.empty_like(src);protected_count=0
    x=(np.arange(width,dtype=np.float32)+.5)*small.shape[1]/width-.5
    for start in range(0,height,128):
        end=min(height,start+128);top=max(0,start-4);bottom=min(height,end+4)
        y=(np.arange(top,bottom,dtype=np.float32)+.5)*small.shape[0]/height-.5
        mx=np.broadcast_to(x,(bottom-top,width)).copy();my=np.broadcast_to(y[:,None],mx.shape).copy()
        def lift(a,nearest=False):return cv2.remap(a,mx,my,cv2.INTER_NEAREST if nearest else cv2.INTER_LINEAR,borderMode=cv2.BORDER_REPLICATE)
        bg=lift(background).astype(np.float32);protected=lift(protect,True)>0
        original=src[top:bottom];g=gray[top:bottom]
        if src.ndim==3:protected|=np.ptp(original.astype(np.float32)*paper_balance,axis=2)>24
        out=original.astype(np.float32)
        if white:
            normalized=np.minimum(255,g.astype(np.float32)*255/np.maximum(bg,32))
            normalized=np.minimum(255,normalized+12/(1+np.exp(-(normalized-220)/8)))
            if src.ndim==3:out*=(normalized/np.maximum(g,1))[...,None]*paper_balance
            else:out=normalized
        if fine:
            weight=lift(fine_mask).astype(np.float32)/255;blurred=cv2.GaussianBlur(out,(0,0),.65)
            out+=.22*(out-blurred)*(weight[...,None] if src.ndim==3 else weight)
        out=np.clip(out,0,255).astype(np.uint8);out[protected]=original[protected]
        result[start:end]=out[start-top:end-top];protected_count+=int(protected[start-top:end-top].sum())
    return result,{'applied':bool(white or fine),'glyphs':len(s),'white_background':white,'small_print':fine,'protected_fraction':round(protected_count/gray.size,4)}


def geometry_finish(gray,display,options):
    g=np.asarray(gray,dtype=np.uint8);visible=np.asarray(display,dtype=np.uint8);report={'version':VERSION}
    if options.get('lines'):
        field,report['lines']=residual_field(g)
        if field is not None:g=remap_vertical(g,field);visible=remap_vertical(visible,field)
    return Image.fromarray(g),Image.fromarray(visible),report


def main():
    p=argparse.ArgumentParser();p.add_argument('request');a=p.parse_args();req=json.loads(Path(a.request).read_text('utf8'))
    with Image.open(req['gray']) as im:gray=im.convert('L')
    with Image.open(req['source']) as im:visible=im.convert('RGB') if im.mode=='RGB' else im.convert('L')
    mode=req.get('appearance','native')
    def appearance(gray,visible):
        if mode=='native':return visible
        visible=gray.copy();visible.thumbnail((4200,4200),Image.Resampling.LANCZOS)
        if mode=='soft':
            from .engine import soft_monochrome
            visible=Image.fromarray(soft_monochrome(np.asarray(visible)))
        elif mode=='soft_local':
            from .output_appearance import local_soft_monochrome
            visible=Image.fromarray(local_soft_monochrome(np.asarray(visible)))
        return visible
    if req.get('before'):appearance(gray,visible).save(req['before'])
    opts=req.get('options',{});gray,visible,report=geometry_finish(gray,visible,opts)
    visible=appearance(gray,visible)
    pixels,report['tone']=tonal_finish(visible,opts.get('white',False),opts.get('fine',False))
    Image.fromarray(pixels).save(req['output'])
    print('STOUFINISH '+json.dumps(report,ensure_ascii=False),flush=True)


if __name__=='__main__':main()
