"""On-demand official OCR packages. No image/document leaves the computer."""
from __future__ import annotations
import argparse,hashlib,json,os,re,subprocess,sys,time,urllib.request,uuid,venv
from pathlib import Path
from .runtime_layout import data_root, resource_path, environment_python, paddle_python

NAMES={'tur':'Türkçe','deu':'Almanca','eng':'İngilizce','fra':'Fransızca','ita':'İtalyanca','spa':'İspanyolca',
       'ara':'Arapça','fas':'Farsça','osd':'Yön / yazı sistemi','lat':'Latince','grc':'Eski Yunanca','ell':'Yunanca',
       'deu_latf':'Almanca Fraktur','rus':'Rusça','ukr':'Ukraynaca','por':'Portekizce','nld':'Felemenkçe',
       'pol':'Lehçe','heb':'İbranice','chi_sim':'Çince (basitleştirilmiş)','chi_tra':'Çince (geleneksel)',
       'jpn':'Japonca','kor':'Korece','aze':'Azerbaycanca','swe':'İsveççe','dan':'Danca','fin':'Fince',
       'hin':'Hintçe','hun':'Macarca','ces':'Çekçe','ron':'Rumence','bul':'Bulgarca','srp':'Sırpça'}

def event(message,**values): print('STOUMGMT '+json.dumps({'message':message,**values},ensure_ascii=False),flush=True)
def save(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);tmp=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    tmp.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8');os.replace(tmp,path)
def get_json(url):
    request=urllib.request.Request(url,headers={'User-Agent':'scantailor-ocr-unified/0.3'})
    with urllib.request.urlopen(request,timeout=45) as response:return json.load(response)
def catalog(root):
    result={'schema':1,'updated':time.time(),'sources':{}}
    for quality in ('best','fast'):
        repo='tesseract-ocr/tessdata_'+quality
        commit=get_json('https://api.github.com/repos/'+repo+'/commits/main')['sha']
        tree=get_json('https://api.github.com/repos/'+repo+'/git/trees/'+commit+'?recursive=1')
        models=[]
        for item in tree['tree']:
            name=item['path']
            if not name.endswith('.traineddata') or item['type']!='blob':continue
            code=name[:-12]
            if not re.fullmatch(r'(?:script/)?[A-Za-z0-9_]+',code):continue
            models.append({'code':code,'name':NAMES.get(code,code),'bytes':item['size'],'sha1':item['sha'],
                           'url':f'https://raw.githubusercontent.com/{repo}/{commit}/{name}'})
        result['sources'][quality]={'commit':commit,'repo':repo,'models':models}
    save(resource_path('ocr-catalog.json',root,writable=True),result);event('Dil kataloğu güncellendi.',catalog=str(resource_path('ocr-catalog.json',root,writable=True)))
    return result

def blob_hash(path):
    path=Path(path);h=hashlib.sha1(f'blob {path.stat().st_size}\0'.encode())
    with path.open('rb') as f:
        for data in iter(lambda:f.read(1024*1024),b''):h.update(data)
    return h.hexdigest()
def download(root,quality,codes):
    path=resource_path('ocr-catalog.json',root,writable=True);data=json.loads(path.read_text(encoding='utf8')) if path.exists() else catalog(root)
    models={item['code']:item for item in data['sources'][quality]['models']}
    for code in codes:
        if code not in models:raise ValueError('Katalogda bulunmayan dil: '+code)
        item=models[code];target=resource_path('tessdata_'+quality,root,writable=True)/(code+'.traineddata')
        if target.exists():
            if blob_hash(target)==item['sha1']:event(code+' zaten kurulu ve doğrulandı.');continue
            # Preserve a locally customized/older model, never overwrite silently.
            raise ValueError(code+' mevcut dosyası katalogdan farklı; dosya korundu. Başka kalite paketini seçin.')
        target.parent.mkdir(parents=True,exist_ok=True);temp=target.with_name(target.name+'.'+uuid.uuid4().hex+'.part')
        read=0;last=-1
        try:
            with urllib.request.urlopen(item['url'],timeout=60) as response,temp.open('xb') as output:
                while True:
                    chunk=response.read(256*1024)
                    if not chunk:break
                    output.write(chunk);read+=len(chunk);percent=int(read*100/item['bytes'])
                    if percent//5!=last:last=percent//5;event(code+' indiriliyor',percent=percent,bytes=read,total=item['bytes'])
            if temp.stat().st_size!=item['bytes'] or blob_hash(temp)!=item['sha1']:raise ValueError('Dil dosyasının bütünlük kontrolü başarısız: '+code)
            os.link(temp,target);event(code+' indirildi ve doğrulandı.',installed=code)
        finally:
            if temp.exists():temp.unlink()

def runtime_path(root,device):
    return paddle_python(root,device)
def install_paddle(root,device):
    if device=='cpu' and (root/'release.json').is_file() and json.loads((root/'release.json').read_text('utf8')).get('cpu_bundled',True):
        event('CPU paketi kurulumun içinde; yeniden indirmeden test ediliyor.')
        return probe(root,device)
    target=data_root(root)/'resources/runtimes'/('paddle' if device=='gpu' else 'paddle_cpu')
    python=environment_python(target)
    if not python.exists():event('Yalıtılmış PaddleOCR ortamı hazırlanıyor.');venv.EnvBuilder(with_pip=True).create(target)
    package,index=('paddlepaddle-gpu==3.3.1','cu129') if device=='gpu' else ('paddlepaddle==3.3.0','cpu')
    event('PaddleOCR '+device.upper()+' indiriliyor; GPU bağımlılıkları birkaç GB yer kaplayabilir.')
    subprocess.run([str(python),'-m','pip','install','--disable-pip-version-check',package,'--index-url','https://www.paddlepaddle.org.cn/packages/stable/'+index+'/'],check=True)
    subprocess.run([str(python),'-m','pip','install','--disable-pip-version-check','paddleocr==3.7.0'],check=True)
    probe(root,device)
def probe(root,device):
    python=runtime_path(root,device)
    if not python.exists():raise FileNotFoundError('PaddleOCR henüz kurulu değil.')
    script=Path(__file__).with_name('paddle_runtime.py');run=subprocess.run([str(python),str(script),'--probe',device],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=180)
    if run.returncode:raise RuntimeError((run.stderr+'\n'+run.stdout)[-2000:])
    lines=[line[8:] for line in run.stdout.splitlines() if line.startswith('STOUOCR ')]
    if not lines:raise RuntimeError('PaddleOCR doğrulama sonucu alınamadı.')
    result=json.loads(lines[-1])
    if not result.get('ok'):raise RuntimeError(result.get('error','PaddleOCR test hatası'))
    save(resource_path('paddle-'+device+'-ready.json',root,writable=True),result);event('PaddleOCR '+device.upper()+' cihaz testi geçti.',probe=result)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['catalog','download','install-paddle','probe']);parser.add_argument('--root',required=True);parser.add_argument('--quality',choices=['best','fast'],default='best');parser.add_argument('--codes',default='');parser.add_argument('--device',choices=['cpu','gpu'],default='cpu');a=parser.parse_args();root=Path(a.root).resolve()
    try:
        if a.action=='catalog':catalog(root)
        elif a.action=='download':download(root,a.quality,[c for c in a.codes.split('+') if c])
        elif a.action=='install-paddle':install_paddle(root,a.device)
        else:probe(root,a.device)
        return 0
    except Exception as exc:event(str(exc),error=True);return 1
if __name__=='__main__':raise SystemExit(main())
