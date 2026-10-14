"""Virtualized page previews, kept on the right as in ScanTailor."""
from collections import OrderedDict
from pathlib import Path
import tkinter as tk
from tkinter import ttk
from PIL import Image,ImageTk,ImageOps


class ThumbnailList(ttk.Frame):
    def __init__(self,parent):
        super().__init__(parent)
        self.items={};self.order=[];self.chosen=None;self.photos=OrderedDict();self.pending=None
        self.canvas=tk.Canvas(self,background='#edf0f4',highlightthickness=0,width=186)
        self.canvas.pack(side='left',fill='both',expand=True)
        self.scroll=ttk.Scrollbar(self,orient='vertical',command=self.yview);self.scroll.pack(side='right',fill='y')
        self.canvas.configure(yscrollcommand=self.scroll.set)
        self.canvas.bind('<Configure>',lambda e:self.schedule())
        self.canvas.bind('<MouseWheel>',self.wheel);self.canvas.bind('<Button-1>',self.clicked)
        self.canvas.bind('<Key-Up>',lambda e:self.advance(-1));self.canvas.bind('<Key-Down>',lambda e:self.advance(1))
        self.canvas.bind('<Home>',lambda e:self.selection_set(self.order[0]) if self.order else None)
        self.canvas.bind('<End>',lambda e:self.selection_set(self.order[-1]) if self.order else None)
        self.tile_height=205
    def schedule(self):
        if self.pending is None:self.pending=self.after_idle(self.draw)
    def get_children(self):return tuple(self.order)
    def insert(self,parent,index,iid,text='',values=()):
        self.items[iid]={'text':text,'values':values,'path':None,'quad':None}
        self.order.append(iid);self.schedule()
    def delete(self,iid):
        self.items.pop(iid,None);self.photos.pop(iid,None)
        if iid in self.order:self.order.remove(iid)
        if self.chosen==iid:self.chosen=None
        self.schedule()
    def item(self,iid,**kwargs):
        self.items[iid].update(kwargs);self.schedule();return self.items[iid]
    def move(self,iid,parent,index):
        self.order.remove(iid);self.order.insert(index,iid);self.schedule()
    def selection(self):return (self.chosen,) if self.chosen in self.items else ()
    def selection_set(self,iid):
        if iid not in self.items:return
        self.chosen=iid;self.see(iid);self.schedule();self.event_generate('<<TreeviewSelect>>',when='tail')
    def see(self,iid):
        top=self.order.index(iid)*self.tile_height;view=self.canvas.canvasy(0);height=self.canvas.winfo_height()
        total=max(1,len(self.order)*self.tile_height)
        if top<view:self.canvas.yview_moveto(top/total)
        elif top+self.tile_height>view+height:self.canvas.yview_moveto(max(0,top+self.tile_height-height)/total)
    def set_source(self,iid,path,quad=None):
        item=self.items[iid]
        if item['path']!=str(path) or item['quad']!=quad:
            item.update(path=str(path),quad=quad);self.photos.pop(iid,None);self.schedule()
    def yview(self,*args):self.canvas.yview(*args);self.schedule()
    def wheel(self,event):self.canvas.yview_scroll(-int(event.delta/120),'units');self.schedule()
    def clicked(self,event):
        index=int(self.canvas.canvasy(event.y)//self.tile_height)
        self.canvas.focus_set()
        if 0<=index<len(self.order):self.selection_set(self.order[index])
    def advance(self,delta):
        if not self.order:return
        index=self.order.index(self.chosen) if self.chosen in self.order else 0
        self.selection_set(self.order[max(0,min(len(self.order)-1,index+delta))])
    def photo(self,iid):
        if iid in self.photos:self.photos.move_to_end(iid);return self.photos[iid]
        item=self.items[iid]
        if not item['path']:return None
        try:
            with Image.open(item['path']) as src:
                im=ImageOps.exif_transpose(src).convert('RGB')
            if item['quad']:
                q=item['quad'];xs=[p[0]*im.width for p in q];ys=[p[1]*im.height for p in q]
                im=im.crop((min(xs),min(ys),max(xs),max(ys)))
            im.thumbnail((142,155),Image.Resampling.LANCZOS)
            result=ImageTk.PhotoImage(im)
        except (OSError,ValueError):return None
        self.photos[iid]=result
        while len(self.photos)>64:self.photos.popitem(last=False)
        return result
    def draw(self):
        self.pending=None;c=self.canvas;c.delete('all');width=max(60,c.winfo_width())
        total=len(self.order)*self.tile_height;c.configure(scrollregion=(0,0,width,total),yscrollincrement=28)
        top=max(0,int(c.canvasy(0)//self.tile_height)-1);last=min(len(self.order),int((c.canvasy(0)+c.winfo_height())//self.tile_height)+2)
        for n in range(top,last):
            iid=self.order[n];item=self.items[iid];y=n*self.tile_height;selected=iid==self.chosen
            c.create_rectangle(4,y+3,width-4,y+self.tile_height-3,fill='#1269cf' if selected else 'white',outline='#1269cf' if selected else '#cdd3dc',width=2)
            photo=self.photo(iid)
            if photo:c.create_image(width/2,y+84,image=photo)
            else:c.create_text(width/2,y+80,text='Önizleme yok',fill='#718096')
            c.create_text(width/2,y+168,text=item['text'],width=width-16,anchor='n',fill='white' if selected else '#182b40',font=('Segoe UI',9,'bold'))
            status=item.get('values',('Bekliyor',))[0]
            c.create_text(width/2,y+194,text=status,fill='white' if selected else ('#a45a00' if status=='Kontrol' else '#596a7c'),font=('Segoe UI',8))
