"""Experimental independent text baselines for geometric quality assessment."""
from pathlib import Path
import json
import time
import cv2
import numpy as np


def trace_words(image):
    gray=cv2.cvtColor(image,cv2.COLOR_BGR2GRAY) if image.ndim==3 else image
    binary=cv2.adaptiveThreshold(gray,255,cv2.ADAPTIVE_THRESH_GAUSSIAN_C,cv2.THRESH_BINARY_INV,31,12)
    count,labels,stats,centers=cv2.connectedComponentsWithStats(binary,8)
    height,width=gray.shape
    glyph=(stats[:,cv2.CC_STAT_HEIGHT]>=4)&(stats[:,cv2.CC_STAT_HEIGHT]<height*.035)&(stats[:,cv2.CC_STAT_WIDTH]>=2)&(stats[:,cv2.CC_STAT_WIDTH]<width*.045)&(stats[:,cv2.CC_STAT_AREA]>=5)
    glyph[0]=False
    if glyph.sum()<30:return [],{'reason':'insufficient glyphs'}
    char_height=float(np.median(stats[glyph,cv2.CC_STAT_HEIGHT]))
    # Remove photograph borders, fingers and long rules before forming words.
    keep=glyph.copy()
    keep|=(stats[:,cv2.CC_STAT_HEIGHT]<char_height*1.6)&(stats[:,cv2.CC_STAT_WIDTH]<char_height*18)&(stats[:,cv2.CC_STAT_AREA]>=5)
    keep[0]=False
    clean=(keep[labels]*255).astype('uint8')
    joined=cv2.morphologyEx(clean,cv2.MORPH_CLOSE,cv2.getStructuringElement(cv2.MORPH_RECT,(max(3,int(char_height*.75)),1)))
    count,labels,stats,centers=cv2.connectedComponentsWithStats(joined,8)
    words=[];word_points=[]
    for label in range(1,count):
        x,y,w,h,area=stats[label]
        if w<char_height*1.25 or h<char_height*.5 or h>char_height*2.2+w*.16 or w>width*.85 or area<12:continue
        mask=(labels[y:y+h,x:x+w]==label)&(clean[y:y+h,x:x+w]>0)
        ys=np.max(np.where(mask,np.arange(h)[:,None],-1000),axis=0)
        usable=ys>=0
        if usable.sum()<5:continue
        baseline=float(np.quantile(ys[usable],.55))+y
        points=[]
        for begin in range(0,w,max(5,int(char_height*2.8))):
            stop=min(w,begin+max(5,int(char_height*2.8)));valid=np.flatnonzero(usable[begin:stop])+begin
            if len(valid)>=4:points.append([x+float(np.mean(valid)),y+float(np.quantile(ys[valid],.55))])
        if len(points)<2:points=[[x+w*.5,baseline]]
        slope=float(np.polyfit(np.array(points)[:,0],np.array(points)[:,1],1)[0]) if len(points)>1 else 0.
        if abs(slope)>.3:continue
        words.append((x,y,w,h,baseline,slope,len(word_points)));word_points.append(points)
    if len(words)<10:return [],{'reason':'insufficient words'}
    words=np.array(words,float);words=words[np.argsort(words[:,0])]
    # A persistent empty vertical band separates columns, unlike interword gaps.
    x0=int(np.min(words[:,0]));x1=int(np.max(words[:,0]+words[:,2]));y0=int(np.min(words[:,1]));y1=int(np.max(words[:,1]+words[:,3]))
    density=np.mean(clean[y0:y1]>0,axis=0);density=cv2.GaussianBlur(density[None,:].astype('float32'),(0,0),max(1,char_height*.25))[0]
    valley=density<max(.003,float(np.max(density))*.08)
    separators=[]
    for run in np.split(np.arange(x0,x1),np.where(np.diff(valley[x0:x1].astype('int8'))!=0)[0]+1):
        if len(run)>char_height*1.1 and valley[run[0]] and run[0]>x0+(x1-x0)*.2 and run[-1]<x1-(x1-x0)*.2:separators.append((float(run[0])+run[-1])/2)
    edges=[]
    for i,word in enumerate(words):
        right=word[0]+word[2]
        gap=words[:,0]-right
        dy=(words[:,4]-words[:,2]*.5*words[:,5])-(word[4]+word[2]*.5*word[5])
        distance=(words[:,0]+words[:,2]*.5)-(word[0]+word[2]*.5)
        valid=(gap>=-char_height*.3)&(gap<char_height*4)&(distance>0)&(np.abs(dy)<char_height*.6+np.maximum(gap,0)*.14)
        for separator in separators:
            valid&=~((right<separator)&(words[:,0]>separator))
        ids=np.flatnonzero(valid)
        if len(ids):
            cost=np.maximum(gap[ids],0)+np.abs(dy[ids])*3
            j=int(ids[np.argmin(cost)]);edges.append((float(cost.min()),i,j))
    parent=list(range(len(words)))
    def root(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    outgoing=set();incoming=set()
    for _,i,j in sorted(edges):
        if i in outgoing or j in incoming:continue
        a,b=root(i),root(j)
        if a==b:continue
        parent[b]=a;outgoing.add(i);incoming.add(j)
    groups={}
    for i in range(len(words)):groups.setdefault(root(i),[]).append(i)
    lines=[]
    for group in groups.values():
        rows=words[group];rows=rows[np.argsort(rows[:,0])]
        if rows[-1,0]+rows[-1,2]-rows[0,0]<width*.18:continue
        points=[p for row in rows for p in word_points[int(row[6])]]
        if len(points)>=4:lines.append(points)
    return lines,{'glyph_height':char_height,'words':len(words),'lines':len(lines),'separators':separators}


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path,help='Folder containing *-ai-input.png previews')
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    cv2.setNumThreads(2)
    source=args.source;out=args.output;out.mkdir(parents=True,exist_ok=True)
    for path in sorted(source.glob('*-ai-input.png')):
        index=path.name.removesuffix('-ai-input.png')
        image=cv2.imread(str(path));start=time.perf_counter();lines,report=trace_words(image)
        report['seconds']=time.perf_counter()-start;print(index,report)
        (out/f'{index}-words.json').write_text(json.dumps({'lines':lines,'report':report,'size':list(image.shape[:2][::-1])}),encoding='utf8')
        overlay=image.copy()
        for line in lines:cv2.polylines(overlay,[np.array(line).astype('int32')],False,(0,0,255),1)
        cv2.imwrite(str(out/f'{index}-lines.jpg'),overlay)
