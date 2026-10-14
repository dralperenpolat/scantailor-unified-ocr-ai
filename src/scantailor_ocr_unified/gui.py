"""Interactive local preview. Launch with python -m scantailor_ocr_unified.gui."""
from __future__ import annotations
import argparse
import copy
from datetime import datetime
from pathlib import Path
import json
import os
import queue
import threading
import traceback
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from PIL import Image, ImageTk
from .project import APP_ROOT, new_project, save_project, open_project, page_entry, read_rgb, collect_images, file_hash
from . import engine

METHODS = {'Otomatik (kontrollü eğrilik)': 'auto', 'Eğrilik / satır düzeltme': 'dewarp',
           'Perspektif + eğiklik': 'perspective', 'Yalnız eğiklik': 'deskew', 'Düzeltme kapalı': 'none'}
THRESHOLDS = {'Otsu': 'otsu', 'Sauvola': 'sauvola', 'Wolf': 'wolf'}

class ImageCanvas(tk.Canvas):
    def __init__(self, parent, callback=None):
        super().__init__(parent, background='#e9edf2', highlightthickness=0, cursor='crosshair')
        self.original=None;self.photo=None;self.points=[];self.zones=[]
        self.mode='view';self.callback=callback;self.zoom=1.;self.pan=[0,0]
        self.drag=None;self.box_start=None;self.scale=1.;self.offset=(0,0)
        self.bind('<Configure>',lambda e:self.draw())
        self.bind('<ButtonPress-1>',self.down);self.bind('<B1-Motion>',self.motion);self.bind('<ButtonRelease-1>',self.up)
        self.bind('<MouseWheel>',self.wheel)
        self.bind('<ButtonPress-2>',self.pan_down);self.bind('<B2-Motion>',self.pan_motion)
    def set_image(self,image,points=None,zones=None,mode='view',reset=False):
        self.original=image.copy();self.points=copy.deepcopy(points or []);self.zones=copy.deepcopy(zones or [])
        self.mode=mode
        if reset:self.zoom=1.;self.pan=[0,0]
        self.draw()
    def fit(self):self.zoom=1.;self.pan=[0,0];self.draw()
    def draw(self):
        self.delete('all')
        if self.original is None:return
        w,h=self.original.size;cw,ch=max(1,self.winfo_width()),max(1,self.winfo_height())
        self.scale=min(cw/w,ch/h)*.94*self.zoom
        dw,dh=max(1,round(w*self.scale)),max(1,round(h*self.scale))
        x=(cw-dw)/2+self.pan[0];y=(ch-dh)/2+self.pan[1];self.offset=(x,y)
        # Only rasterize the visible viewport when zoomed; full 600-DPI pages stay off the UI heap.
        sx0=max(0,int(-x/self.scale));sy0=max(0,int(-y/self.scale))
        sx1=min(w,int((cw-x)/self.scale)+1);sy1=min(h,int((ch-y)/self.scale)+1)
        if sx1>sx0 and sy1>sy0:
            region=self.original.crop((sx0,sy0,sx1,sy1)).convert('RGB')
            size=(max(1,round(region.width*self.scale)),max(1,round(region.height*self.scale)))
            self.photo=ImageTk.PhotoImage(region.resize(size,Image.Resampling.LANCZOS))
            self.create_image(x+sx0*self.scale,y+sy0*self.scale,anchor='nw',image=self.photo)
        if self.points:
            xy=[v for p in self.points for v in self.screen(p)]
            self.create_polygon(*xy,fill='',outline='#1269cf',width=2)
            for point in self.points:
                px,py=self.screen(point)
                self.create_rectangle(px-5,py-5,px+5,py+5,fill='white',outline='#1269cf',width=2)
        for zone in self.zones:
            p0=self.screen(zone[:2]);p1=self.screen(zone[2:])
            self.create_rectangle(*p0,*p1,outline='#ab3c88',width=2,dash=(5,3))
        if self.mode=='zone':self.create_text(12,12,anchor='nw',text='Beyazla temizlenecek alanı çiz',fill='#842468',font=('Segoe UI',11,'bold'))
    def screen(self,p):
        return (self.offset[0]+p[0]*self.original.width*self.scale,self.offset[1]+p[1]*self.original.height*self.scale)
    def normalized(self,x,y):
        return [min(1.,max(0.,(x-self.offset[0])/(self.original.width*self.scale))),
                min(1.,max(0.,(y-self.offset[1])/(self.original.height*self.scale)))]
    def down(self,e):
        if self.original is None:return
        if self.mode=='zone':self.box_start=self.normalized(e.x,e.y);return
        if self.mode not in ['quad','content']:return
        for i,p in enumerate(self.points):
            x,y=self.screen(p)
            if (x-e.x)**2+(y-e.y)**2<225:self.drag=i;break
    def motion(self,e):
        if self.original is None:return
        if self.box_start is not None:
            self.delete('newzone');p0=self.screen(self.box_start);p1=self.screen(self.normalized(e.x,e.y))
            self.create_rectangle(*p0,*p1,outline='#ab3c88',width=2,tags='newzone');return
        if self.drag is None:return
        p=self.normalized(e.x,e.y)
        if self.mode=='quad':self.points[self.drag]=p
        else:
            opposite=self.points[(self.drag+2)%4]
            x0,x1=sorted([p[0],opposite[0]]);y0,y1=sorted([p[1],opposite[1]])
            if x1-x0>.02 and y1-y0>.02:self.points=[[x0,y0],[x1,y0],[x1,y1],[x0,y1]]
        self.draw()
    def up(self,e):
        if self.box_start is not None:
            p=self.normalized(e.x,e.y);a=self.box_start;self.box_start=None
            box=[min(a[0],p[0]),min(a[1],p[1]),max(a[0],p[0]),max(a[1],p[1])]
            if box[2]-box[0]>.002 and box[3]-box[1]>.002 and self.callback:self.callback('zone',box)
            self.draw();return
        if self.drag is not None:
            self.drag=None
            if self.callback:self.callback(self.mode,self.points)
    def wheel(self,e):
        self.zoom=min(8.,max(.5,self.zoom*(1.2 if e.delta>0 else 1/1.2)));self.draw()
    def pan_down(self,e):self.drag_pan=(e.x,e.y,*self.pan)
    def pan_motion(self,e):
        if hasattr(self,'drag_pan'):
            x,y,px,py=self.drag_pan;self.pan=[px+e.x-x,py+e.y-y];self.draw()

class App(tk.Tk):
    def __init__(self, project=None):
        super().__init__()
        self.title('scantailor-ocr-unified · Deneme 0.2')
        self.geometry('1500x930');self.minsize(1100,720)
        try:self.state('zoomed')
        except tk.TclError:pass
        self.project=None;self.selected=None;self.result=None;self.rendered=None
        self.messages=queue.Queue();self.cancel=threading.Event();self.busy=False;self.loading=False
        self.job_done=None;self.log=[];self.controls=[];self.previews={}
        style=ttk.Style(self);style.theme_use('clam')
        style.configure('.',font=('Segoe UI',10));style.configure('TButton',padding=(9,5))
        style.configure('Treeview',rowheight=27);style.configure('Accent.TButton',background='#1766ad',foreground='white')
        self.build_ui();self.after(100,self.poll)
        self.protocol('WM_DELETE_WINDOW',self.close)
        if project:self.load(project)
        else:self.load_demo()
    def build_ui(self):
        self.columnconfigure(0,weight=1);self.rowconfigure(1,weight=1)
        bar=ttk.Frame(self,padding=8);bar.grid(row=0,column=0,sticky='ew')
        actions=[('Yeni kitap',self.new),('Proje aç',self.open),('Dosya ekle',self.add_files),('Klasör ekle',self.add_folder),('Örnek kitap',self.load_demo)]
        for text,command in actions:
            b=ttk.Button(bar,text=text,command=command);b.pack(side='left',padx=2);self.controls.append(b)
        ttk.Button(bar,text='Projeyi kaydet',command=self.save).pack(side='left',padx=2)
        self.project_label=ttk.Label(bar,text='',font=('Segoe UI',11,'bold'));self.project_label.pack(side='right',padx=10)
        panel=ttk.Panedwindow(self,orient='horizontal');panel.grid(row=1,column=0,sticky='nsew',padx=8)
        sidebar=ttk.Frame(panel,width=290,padding=(0,0,8,0));panel.add(sidebar,weight=0)
        ttk.Label(sidebar,text='Kitap sayfaları',font=('Segoe UI',11,'bold')).pack(anchor='w',pady=(0,5))
        self.sort=tk.StringVar(value='Kitap sırası')
        sortbox=ttk.Combobox(sidebar,textvariable=self.sort,state='readonly',values=['Kitap sırası','Önce kontrol gerekenler','Genişlik: küçük → büyük','Genişlik: büyük → küçük','Yükseklik: küçük → büyük','Yükseklik: büyük → küçük'])
        sortbox.pack(fill='x',pady=(0,5));sortbox.bind('<<ComboboxSelected>>',lambda e:self.sort_tree())
        list_frame=ttk.Frame(sidebar);list_frame.pack(fill='both',expand=True)
        self.tree=ttk.Treeview(list_frame,columns=('status',),show='tree headings',selectmode='browse',height=5)
        self.tree.heading('#0',text='Sayfa');self.tree.heading('status',text='Durum')
        self.tree.column('#0',width=166,stretch=True);self.tree.column('status',width=80,stretch=False)
        self.tree.pack(side='left',fill='both',expand=True)
        scroll=ttk.Scrollbar(list_frame,orient='vertical',command=self.tree.yview);scroll.pack(side='right',fill='y');self.tree.configure(yscrollcommand=scroll.set)
        self.tree.bind('<<TreeviewSelect>>',self.select_page)
        row=ttk.Frame(sidebar);row.pack(fill='x',pady=5)
        for label,cmd in [('↑',lambda:self.move(-1)),('↓',lambda:self.move(1)),('Listeden çıkar',self.remove)]:
            b=ttk.Button(row,text=label,command=cmd);b.pack(side='left',padx=1);self.controls.append(b)
        settings=ttk.Notebook(sidebar);settings.pack(fill='x',pady=5)
        opts=ttk.Frame(settings,padding=8);settings.add(opts,text='Seçili sayfa')
        self.method=tk.StringVar(value=list(METHODS)[0]);self.threshold=tk.StringVar(value='Otsu')
        self.adjust=tk.IntVar(value=0);self.light=tk.BooleanVar(value=True);self.body=tk.StringVar(value='Otomatik')
        ttk.Label(opts,text='Geometrik düzeltme').pack(anchor='w')
        cb=ttk.Combobox(opts,textvariable=self.method,values=list(METHODS),state='readonly');cb.pack(fill='x',pady=(2,8));cb.bind('<<ComboboxSelected>>',lambda e:self.changed());self.controls.append(cb)
        rot=ttk.Frame(opts);rot.pack(fill='x')
        for label,delta in [('↶ 90°',-90),('↷ 90°',90)]:
            b=ttk.Button(rot,text=label,command=lambda d=delta:self.rotate(d));b.pack(side='left',expand=True,fill='x');self.controls.append(b)
        ttk.Label(opts,text='Siyah-beyaz yöntemi').pack(anchor='w',pady=(8,1))
        cb=ttk.Combobox(opts,textvariable=self.threshold,values=list(THRESHOLDS),state='readonly');cb.pack(fill='x');cb.bind('<<ComboboxSelected>>',lambda e:self.changed());self.controls.append(cb)
        adjust=ttk.Frame(opts);adjust.pack(fill='x',pady=5)
        ttk.Label(adjust,text='Kalınlık (−30 / +30)').pack(side='left')
        spin=ttk.Spinbox(adjust,from_=-30,to=30,textvariable=self.adjust,width=5,command=self.changed);spin.pack(side='right');spin.bind('<FocusOut>',lambda e:self.changed());self.controls.append(spin)
        cb=ttk.Checkbutton(opts,text='Gölge / aydınlatma düzelt',variable=self.light,command=self.changed);cb.pack(anchor='w');self.controls.append(cb)
        br=ttk.Frame(opts);br.pack(fill='x',pady=5)
        ttk.Label(br,text='Kaynak harf boyu (px)').pack(side='left')
        ent=ttk.Entry(br,textvariable=self.body,width=8);ent.pack(side='right');ent.bind('<Return>',lambda e:self.changed());ent.bind('<FocusOut>',lambda e:self.changed());self.controls.append(ent)
        ttk.Label(opts,text='Boş / Otomatik: program ölçer',foreground='#586879',font=('Segoe UI',9)).pack(anchor='w')
        for label,cmd in [('İçerik kutusunu otomatik seç',self.reset_content),('Beyaz temizleme alanı çiz',self.zone_mode),('Temizleme alanlarını kaldır',self.clear_zones)]:
            b=ttk.Button(opts,text=label,command=cmd);b.pack(fill='x',pady=2);self.controls.append(b)
        self.allow_empty=tk.BooleanVar(value=False)
        cb=ttk.Checkbutton(opts,text='Boş / resimli sayfa: metin şart değil',variable=self.allow_empty,command=self.changed);cb.pack(anchor='w',pady=(5,0));self.controls.append(cb)
        book=ttk.Frame(settings,padding=9);settings.add(book,text='Kitap çıktısı')
        self.language=tk.StringVar(value='deu');self.dpi=tk.StringVar(value='600');self.equalize=tk.BooleanVar(value=True)
        row=ttk.Frame(book);row.pack(fill='x')
        ttk.Label(row,text='OCR').pack(side='left');self.langbox=ttk.Combobox(row,textvariable=self.language,values=['deu','tur','eng','deu+eng','tur+eng'],width=10,state='readonly');self.langbox.pack(side='right');self.langbox.bind('<<ComboboxSelected>>',lambda e:self.book_changed());self.controls.append(self.langbox)
        row=ttk.Frame(book);row.pack(fill='x',pady=4)
        ttk.Label(row,text='Çıktı DPI').pack(side='left');cb=ttk.Combobox(row,textvariable=self.dpi,values=['300','600'],width=10,state='readonly');cb.pack(side='right');cb.bind('<<ComboboxSelected>>',lambda e:self.book_changed());self.controls.append(cb)
        cb=ttk.Checkbutton(book,text='Gövde yazı boyutunu eşitle',variable=self.equalize,command=self.book_changed);cb.pack(anchor='w');self.controls.append(cb)
        ttk.Label(book,text='Tüm sayfalar A4. Ortak yazı ölçeği kitap\nönizlemesinde ve PDF oluştururken hesaplanır.\n\nOCR, düzeltilmiş gri kopyadan alınır.\nSiyah-beyaz görünüm ayrı üretilir.\n\nGenişlik / yükseklik sıralaması yalnız\nlisteyi değiştirir; PDF sırası korunur.',justify='left',foreground='#586879').pack(anchor='w',pady=15)
        main=ttk.Frame(panel);panel.add(main,weight=1)
        self.hint=ttk.Label(main,text='Köşeleri sürükle. Tekerlek: yakınlaştır • Orta tuş: kaydır',padding=7);self.hint.pack(fill='x')
        views=ttk.Panedwindow(main,orient='horizontal');views.pack(fill='both',expand=True)
        left=ttk.Frame(views);right=ttk.Frame(views);views.add(left,weight=1);views.add(right,weight=1)
        ttk.Label(left,text='Kaynak fotoğraf · mavi köşeler sayfa alanı',padding=5).pack(fill='x')
        self.source_canvas=ImageCanvas(left,self.canvas_changed);self.source_canvas.pack(fill='both',expand=True)
        tabs=ttk.Frame(right);tabs.pack(fill='x')
        self.view=tk.StringVar(value='geometry')
        for label,value in [('Düzeltilmiş','geometry'),('Siyah-beyaz','display'),('OCR gri kopya','ocr')]:
            ttk.Radiobutton(tabs,text=label,variable=self.view,value=value,command=self.show_view).pack(side='left',padx=3,pady=5)
        self.output_canvas=ImageCanvas(right,self.canvas_changed);self.output_canvas.pack(fill='both',expand=True)
        self.info=ttk.Label(main,text='',padding=7,wraplength=1000);self.info.pack(fill='x')
        bottom=ttk.Frame(self,padding=8);bottom.grid(row=2,column=0,sticky='ew')
        for text,cmd,style in [('Önizle',self.preview,'Accent.TButton'),('Kitabın tamamını önizle',self.process_all,'TButton'),('OCR’lı PDF oluştur',self.export,'Accent.TButton')]:
            b=ttk.Button(bottom,text=text,command=cmd,style=style);b.pack(side='left',padx=3);self.controls.append(b)
        ttk.Button(bottom,text='Durdur',command=self.cancel.set).pack(side='left',padx=3)
        ttk.Button(bottom,text='Ekrana sığdır (PDF değişmez)',command=lambda:(self.source_canvas.fit(),self.output_canvas.fit())).pack(side='left',padx=3)
        self.status=tk.StringVar(value='Hazır');ttk.Label(bottom,textvariable=self.status).pack(side='right',padx=8)
        self.progress=ttk.Progressbar(self,mode='indeterminate');self.progress.grid(row=3,column=0,sticky='ew')
    def current(self):
        return next((p for p in (self.project or {}).get('pages',[]) if p['id']==self.selected),None)
    def set_busy(self,value):
        self.busy=value
        for widget in self.controls:
            if isinstance(widget,ttk.Combobox):widget.configure(state='disabled' if value else 'readonly')
            else:widget.configure(state='disabled' if value else 'normal')
        self.source_canvas.mode='view' if value else 'quad'
        if value:self.progress.start(12)
        else:self.progress.stop();self.show_view()
    def run(self,fn,done):
        if self.busy:return
        self.cancel.clear();self.job_done=done;self.set_busy(True)
        def worker():
            try:self.messages.put(('done',fn()))
            except engine.Cancelled as e:self.messages.put(('cancelled',str(e)))
            except Exception as e:self.messages.put(('error',(str(e),traceback.format_exc())))
        threading.Thread(target=worker,daemon=True).start()
    def poll(self):
        try:
            while True:
                kind,value=self.messages.get_nowait()
                if kind=='progress':self.status.set(value)
                elif kind=='done':
                    self.set_busy(False)
                    if self.job_done:self.job_done(value)
                elif kind=='cancelled':self.set_busy(False);self.status.set(value)
                elif kind=='error':
                    self.set_busy(False);self.status.set('İşlem tamamlanamadı')
                    logdir=APP_ROOT/'output';logdir.mkdir(exist_ok=True)
                    (logdir/('error_'+datetime.now().strftime('%Y%m%d_%H%M%S')+'.txt')).write_text(value[1],encoding='utf8')
                    messagebox.showerror('İşlem hatası',value[0],parent=self)
        except queue.Empty:pass
        self.after(100,self.poll)
    def save(self):
        if self.project:
            save_project(self.project);self.status.set('Proje kaydedildi')
    def new(self):
        if self.busy:return
        path=filedialog.asksaveasfilename(title='Yeni kitap projesi',defaultextension='.json',filetypes=[('Kitap projesi','*.json')],initialfile='kitap.json',parent=self)
        if path:
            self.project=new_project(path,Path(path).stem);self.selected=None;self.previews={};self.result=None;self.rendered=None
            self.source_canvas.original=None;self.output_canvas.original=None;self.source_canvas.draw();self.output_canvas.draw();self.info.configure(text='Dosya veya klasör ekleyerek başla.');self.refresh_tree();self.save()
    def open(self):
        if self.busy:return
        path=filedialog.askopenfilename(title='Kitap projesi aç',filetypes=[('Kitap projesi','*.json')],parent=self)
        if path:self.load(path)
    def load(self,path):
        if self.busy:return
        try:
            self.project=open_project(path);self.selected=None;self.previews={}
            self.language.set(self.project.get('language','deu'));self.dpi.set(str(self.project.get('output_dpi',600)));self.equalize.set(self.project.get('normalize_text',True))
            self.refresh_tree()
            if self.project['pages']:
                page=self.project['pages'][0];self.tree.selection_set(page['id']);self.after(150,self.preview)
        except Exception as e:messagebox.showerror('Proje açılamadı',str(e),parent=self)
    def load_demo(self):
        if self.busy:return
        path=APP_ROOT/'projects'/'Aden_Menno_Deneme'/'kitap.json'
        if path.exists():self.load(path)
        else:self.status.set('Dosya veya klasör ekleyerek başla')
    def refresh_tree(self):
        for item in self.tree.get_children():self.tree.delete(item)
        for page in (self.project or {}).get('pages',[]):self.tree.insert('', 'end',iid=page['id'],text=page['label'],values=(page.get('status','Bekliyor'),))
        self.project_label.configure(text=(self.project or {}).get('title','Yeni kitap'))
        self.sort_tree()
    def sort_tree(self):
        pages=list((self.project or {}).get('pages',[]));mode=self.sort.get()
        if mode=='Önce kontrol gerekenler':pages.sort(key=lambda p:(p.get('status')!='Kontrol',p.get('status')=='Hazır'))
        elif mode.startswith(('Genişlik','Yükseklik')):
            axis=0 if mode.startswith('Genişlik') else 1
            def metric(page):
                g=page.get('analysis')
                if not g:return -1
                rect=page.get('content') or g['content']
                return (rect[axis+2]-rect[axis])*g['size'][axis]/max(g['body_px'],3)
            pages.sort(key=metric,reverse='büyük →' in mode)
        for i,page in enumerate(pages):self.tree.move(page['id'],'',i)
    def remember_analysis(self,page,result):
        page['analysis']={k:result[k] for k in ('size','content','body_px')}
        page['status']='Kontrol' if result['warnings'] else 'Hazır';page['warnings']=result['warnings']
        self.tree.item(page['id'],values=(page['status'],))
    def select_page(self,e=None):
        if not self.tree.selection():return
        self.selected=self.tree.selection()[0];page=self.current();self.result=None;self.rendered=None
        if not page:return
        self.loading=True
        self.method.set(next(k for k,v in METHODS.items() if v==page.get('method','auto')))
        self.threshold.set(next(k for k,v in THRESHOLDS.items() if v==page.get('threshold','otsu')))
        self.adjust.set(page.get('threshold_adjust',0));self.light.set(page.get('normalize_light',True));self.body.set(str(page['body_override']) if page.get('body_override') else 'Otomatik')
        self.allow_empty.set(page.get('allow_empty_ocr',False))
        try:self.source_canvas.set_image(read_rgb(page['source']),page['quad'],mode='view' if self.busy else 'quad',reset=True)
        except Exception as ex:self.status.set(str(ex))
        self.output_canvas.original=None;self.output_canvas.draw();self.loading=False
        self.info.configure(text='Sayfa seçildi. Önizle ile düzeltmeyi gör; köşeleri kaynakta, içerik kutusunu düzeltilmiş görünümde ayarla.')
        if page['id'] in self.previews:
            self.result,self.rendered=self.previews[page['id']];self.show_view()
    def changed(self):
        if self.loading or self.busy:return
        page=self.current()
        if not page:return
        try:
            adjust=int(self.adjust.get())
            if not -30<=adjust<=30:raise ValueError('Kalınlık −30 ile +30 arasında olmalı.')
            body=self.body.get().strip();body=None if body in ('','Otomatik') else float(body.replace(',','.'))
            if body is not None and not 3<=body<=150:raise ValueError('Harf boyu 3–150 piksel aralığında olmalı.')
        except (ValueError,tk.TclError) as ex:self.status.set(str(ex));return
        method=METHODS[self.method.get()]
        if method!=page.get('method'):page['content']=None;page['white_zones']=[]
        page.update(method=method,threshold=THRESHOLDS[self.threshold.get()],threshold_adjust=adjust,normalize_light=self.light.get(),body_override=body,allow_empty_ocr=self.allow_empty.get())
        self.mark_changed()
    def mark_changed(self):
        page=self.current()
        if not page:return
        self.previews.pop(page['id'],None)
        page['status']='Değişti';save_project(self.project)
        self.tree.item(page['id'],values=('Değişti',));self.status.set('Ayar değişti — Önizle')
    def book_changed(self):
        if self.loading or not self.project:return
        self.project.update(language=self.language.get(),output_dpi=int(self.dpi.get()),normalize_text=self.equalize.get());save_project(self.project)
        self.status.set('Kitap ayarı kaydedildi — Önizle')
    def rotate(self,delta):
        page=self.current()
        if page:page['rotation']=(page.get('rotation',0)+delta)%360;page['content']=None;page['white_zones']=[];self.mark_changed()
    def canvas_changed(self,kind,points):
        if self.busy:return
        page=self.current()
        if not page:return
        if kind=='quad':page['quad']=copy.deepcopy(points);page['content']=None;page['white_zones']=[]
        elif kind=='content':page['content']=[points[0][0],points[0][1],points[2][0],points[2][1]]
        elif kind=='zone':page['white_zones'].append(points);self.output_canvas.mode='content';self.show_view()
        self.mark_changed()
    def reset_content(self):
        page=self.current()
        if page:page['content']=None;self.mark_changed();self.show_view()
    def zone_mode(self):
        if self.result and not self.busy:self.view.set('geometry');self.show_view();self.output_canvas.mode='zone';self.output_canvas.draw()
    def clear_zones(self):
        page=self.current()
        if page:page['white_zones']=[];self.mark_changed();self.show_view()
    def show_view(self):
        if not self.result or not self.current():return
        page=self.current();mode=self.view.get()
        method=next((k for k,v in METHODS.items() if v==self.result['selected_method']),self.result['selected_method'])
        info=f"{method} • Kaynak harf boyu: {self.result['body_px']:.1f} px"
        if self.result['warnings']:info+='\n'+' '.join(self.result['warnings'])
        self.info.configure(text=info)
        if mode=='geometry':
            r=page.get('content') or self.result['content'];points=[[r[0],r[1]],[r[2],r[1]],[r[2],r[3]],[r[0],r[3]]]
            self.output_canvas.set_image(Image.open(self.result['image']),points,page.get('white_zones',[]),mode='view' if self.busy else 'content')
        elif self.rendered:
            path=self.rendered['display_image' if mode=='display' else 'ocr_image']
            self.output_canvas.set_image(Image.open(path),mode='view')
    def preview(self):
        if self.busy or not self.current():return
        self.changed();project=copy.deepcopy(self.project);page=copy.deepcopy(self.current());identity=page['id']
        self.status.set('Geometri ve çıktı hazırlanıyor…')
        def work():
            result=engine.geometry(project,page,self.cancel)
            rendered=engine.render_page(project,page,result,cancel=self.cancel)
            return identity,result,rendered
        def done(value):
            pid,result,rendered=value
            dest=next(p for p in self.project['pages'] if p['id']==pid)
            self.previews[pid]=(result,rendered)
            self.remember_analysis(dest,result);save_project(self.project);self.sort_tree()
            if pid==self.selected:
                self.result=result;self.rendered=rendered;self.show_view()
                method=next((k for k,v in METHODS.items() if v==result['selected_method']),result['selected_method'])
                info=f"{method} • Kaynak harf boyu: {result['body_px']:.1f} px • Tek sayfa önizlemesi"
                if result['warnings']:info+='\n'+' '.join(result['warnings'])
                self.info.configure(text=info)
            self.status.set('Önizleme hazır')
        self.run(work,done)
    def process_all(self):
        if self.busy or not self.project or not self.project['pages']:return
        project=copy.deepcopy(self.project)
        def work():
            pairs=[]
            for i,p in enumerate(project['pages']):
                self.messages.put(('progress',f"Geometri {i+1}/{len(project['pages'])}"));pairs.append((p,engine.geometry(project,p,self.cancel)))
            target=engine.fit_target(project,pairs);rows=[]
            for i,(p,g) in enumerate(pairs):
                self.messages.put(('progress',f'Önizleme {i+1}/{len(pairs)}'));r=engine.render_page(project,p,g,target,self.cancel);rows.append((p['id'],g,r))
            return rows
        def done(rows):
            for pid,g,r in rows:
                self.previews[pid]=(g,r)
                p=next(p for p in self.project['pages'] if p['id']==pid);self.remember_analysis(p,g)
                if pid==self.selected:self.result=g;self.rendered=r;self.show_view()
            save_project(self.project);self.sort_tree();self.status.set(f'{len(rows)} sayfa hazır; kontrol işaretlerini gözden geçir')
        self.run(work,done)
    def export(self):
        if self.busy or not self.project or not self.project['pages']:return
        self.book_changed()
        initial=self.project['title']+'_'+datetime.now().strftime('%Y%m%d_%H%M%S')+'.pdf'
        output=filedialog.asksaveasfilename(title='Aranabilir PDF oluştur',initialdir=str(Path(self.project['project_file']).parent),initialfile=initial,defaultextension='.pdf',filetypes=[('PDF','*.pdf')],parent=self)
        if not output:return
        if Path(output).exists():messagebox.showinfo('Yeni dosya adı gerekli','Mevcut PDF korunuyor. Yeni bir ad seç.',parent=self);return
        project=copy.deepcopy(self.project)
        def notify(stage,i,total,label):self.messages.put(('progress',f'{stage} {i+1}/{total} — {label}'))
        def done(result):
            self.status.set(f"PDF hazır · {result['bytes']/1024:.0f} KB · {result['total_seconds']:.1f} sn")
            self.info.configure(text='Kaydedildi: '+result['output'])
            os.startfile(result['output'])
        self.run(lambda:engine.export_book(project,output,self.cancel,notify),done)
    def add_files(self):
        paths=filedialog.askopenfilenames(title='Fotoğraf veya PDF ekle',filetypes=[('Görüntü / PDF','*.jpg *.jpeg *.png *.tif *.tiff *.bmp *.webp *.pdf'),('Tüm dosyalar','*.*')],parent=self)
        if paths:self.import_paths(list(map(Path,paths)))
    def add_folder(self):
        folder=filedialog.askdirectory(title='Kitabın fotoğraf klasörünü seç (out klasörünü değil)',parent=self)
        if folder:self.import_paths(collect_images(folder))
    def import_paths(self,paths):
        if self.busy or not paths:return
        if not self.project:
            directory=APP_ROOT/'projects'/('Kitap_'+datetime.now().strftime('%Y%m%d_%H%M%S'))
            self.project=new_project(directory/'kitap.json',paths[0].parent.name)
        split=messagebox.askyesno('Fotoğraflardaki sayfalar','Her fotoğrafı sol ve sağ olmak üzere iki kitap sayfasına ayıralım mı?\n\nPDF dosyalarındaki sayfalar tek tek alınır.',parent=self)
        project=copy.deepcopy(self.project)
        def work():
            added=[]
            for i,path in enumerate(paths):
                engine.check_cancel(self.cancel);self.messages.put(('progress',f'İçe alınıyor {i+1}/{len(paths)}'))
                if path.suffix.lower()=='.pdf':
                    import fitz
                    folder=Path(project['project_file']).parent/'imported_pdf'/file_hash(path)[:16];folder.mkdir(parents=True,exist_ok=True)
                    with fitz.open(path) as pdf:
                        for n,p in enumerate(pdf):
                            engine.check_cancel(self.cancel)
                            imagepath=folder/f'{n+1:05d}.png'
                            if not imagepath.exists():
                                zoom=min(200/72,6000/max(p.rect.width,p.rect.height))
                                p.get_pixmap(matrix=fitz.Matrix(zoom,zoom),alpha=False).save(imagepath)
                            entry=page_entry(imagepath,f'{path.stem} · {n+1}',engine.auto_quad(read_rgb(imagepath)))
                            entry['pdf_source']=str(path);entry['pdf_page']=n;added.append(entry)
                else:
                    im=read_rgb(path);sha=file_hash(path)
                    for side in (['left','right'] if split else ['whole']):
                        suffix={'left':' · sol','right':' · sağ','whole':''}[side]
                        entry=page_entry(path,path.stem+suffix,engine.auto_quad(im,side),sha)
                        entry['warnings']=['Sayfa köşeleri tahmin edildi; önizlemede kontrol et.'];entry['status']='Kontrol';added.append(entry)
            return added
        def done(added):
            self.project['pages'].extend(added);save_project(self.project);self.refresh_tree()
            if added:self.tree.selection_set(added[0]['id'])
            self.status.set(f'{len(added)} sayfa eklendi — köşeleri kontrol et')
        self.run(work,done)
    def move(self,delta):
        if self.busy or not self.current():return
        pages=self.project['pages'];i=next(i for i,p in enumerate(pages) if p['id']==self.selected);j=i+delta
        if 0<=j<len(pages):pages[i],pages[j]=pages[j],pages[i];pid=self.selected;self.sort.set('Kitap sırası');save_project(self.project);self.refresh_tree();self.tree.selection_set(pid)
    def remove(self):
        if self.busy or not self.current():return
        self.project['pages']=[p for p in self.project['pages'] if p['id']!=self.selected];self.selected=None;save_project(self.project);self.refresh_tree()
    def close(self):
        if self.busy:
            self.cancel.set();self.status.set('İş güvenli noktada duruyor…');self.after(400,self.close);return
        if self.project:save_project(self.project)
        self.destroy()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--project');parser.add_argument('--smoke-test',action='store_true');args=parser.parse_args()
    app=App(args.project)
    if args.smoke_test:
        def finish():
            if app.busy:app.after(500,finish)
            else:print(json.dumps({'title':app.title(),'pages':len((app.project or {}).get('pages',[])),'preview':bool(app.rendered)}));app.destroy()
        app.after(500,finish)
    app.mainloop()

if __name__=='__main__':main()
