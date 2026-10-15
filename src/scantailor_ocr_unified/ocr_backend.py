"""Alternative recognition engines, always mapped to the unchanged page image."""
from __future__ import annotations
import atexit,json,os,subprocess,threading,queue,time,uuid
from pathlib import Path
import fitz
from .ocr_manager import runtime_path
from .runtime_layout import resource_path

# Lossless PNG encoding speed is independent of recognition/coordinate rules.
OCR_PIPELINE_ID = '043-cpu-memory-aware-v2'
_clients={}
class PaddleClient:
    def __init__(self,root,device):
        self.root=Path(root);python=runtime_path(self.root,device)
        if not python.exists():raise FileNotFoundError('PaddleOCR kurulmamış. OCR motoru ve dil paketleri sekmesinden kurun.')
        env=os.environ.copy();env['PYTHONUTF8']='1';env['PADDLE_PDX_CACHE_HOME']=str(resource_path('paddle-models',self.root,writable=True));env['PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK']='True'
        self.proc=subprocess.Popen([str(python),'-u',str(Path(__file__).with_name('paddle_runtime.py'))],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace',env=env,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.messages=queue.Queue();self.recent=[]
        def collect():
            for line in self.proc.stdout:
                if line.startswith('STOUOCR '):self.messages.put(json.loads(line[8:]))
                else:self.recent.append(line.strip());self.recent=self.recent[-12:]
            self.messages.put({'ok':False,'error':'Paddle işlemi kapandı. '+' '.join(self.recent)[-1200:]})
        self.thread=threading.Thread(target=collect,daemon=True);self.thread.start()
    def recognize(self,image,language,device,version='PP-OCRv5'):
        self.proc.stdin.write(json.dumps({'image':str(image),'language':language,'device':device,'version':version})+'\n');self.proc.stdin.flush()
        try:result=self.messages.get(timeout=1800)
        except queue.Empty:raise TimeoutError('PaddleOCR / model indirmesi 30 dakika içinde tamamlanmadı.')
        if not result.get('ok'):raise RuntimeError(result.get('error','PaddleOCR hatası'))
        return result
    def close(self):
        try:
            if self.proc.poll() is None:self.proc.stdin.write('{"action":"quit"}\n');self.proc.stdin.flush();self.proc.wait(timeout=10)
        except Exception:
            if self.proc.poll() is None:self.proc.terminate()
def close_clients():
    for client in _clients.values():client.close()
    _clients.clear()
atexit.register(close_clients)

def paddle_pdf(image,conf,work,metadata=None):
    root=Path(conf['root']);device=conf.get('ocr_device','cpu');key=(str(root),device)
    if key not in _clients:_clients[key]=PaddleClient(root,device)
    path=Path(work)/('paddle-'+uuid.uuid4().hex+'.png');image.save(path,compress_level=1)
    result=_clients[key].recognize(path,conf.get('paddle_language','de'),device,conf.get('paddle_version','PP-OCRv5'))
    if metadata is not None:
        metadata.update(result)
        weights=sum(len(line['text']) for line in result['lines'])
        metadata['confidence']=sum(line['score']*len(line['text']) for line in result['lines'])/weights if weights else None
        metadata['confidence_kind']='engine score, not calibrated accuracy'
    path.with_suffix('.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    return lines_pdf(image,result)


def recognize_pdf(image,conf,work,metadata=None):
    if conf.get('ocr_engine')!='kraken':return paddle_pdf(image,conf,work,metadata)
    from .optional_engines import kraken_lines
    result=kraken_lines(image,conf,work)
    if metadata is not None:metadata.update(result)
    return lines_pdf(image,result)


def lines_pdf(image,result):
    doc=fitz.open();page=doc.new_page(width=image.width,height=image.height)
    font=fitz.Font('helv')
    for line in result['lines']:
        rect=fitz.Rect(line['box'])&page.rect
        if rect.is_empty or rect.width<1 or rect.height<1:continue
        text=line['text'];height=rect.height;size=height/(font.ascender-font.descender)
        base=fitz.Point(rect.x0,rect.y0+font.ascender*size)
        writer=fitz.TextWriter(page.rect);writer.append(base,text,font=font,fontsize=size)
        width=writer.text_rect.width
        if width<=0:continue
        writer.write_text(page,render_mode=3,morph=(base,fitz.Matrix(rect.width/width,1)))
    doc.subset_fonts();data=doc.tobytes(garbage=4,deflate=True);doc.close()
    return data
