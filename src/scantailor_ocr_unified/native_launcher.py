"""Open the complete native Experimental fork, with the local OCR bridge."""
import argparse,os,subprocess,sys,json
from pathlib import Path

from .runtime_layout import app_root, data_root, initialize, python_path
ROOT=app_root()
SAMPLE=ROOT/'projects/Native_Deneme_03/Aden_Menno_Deneme_03.ScanTailor'

def main():
    parser=argparse.ArgumentParser(description='ScanTailor OCR Unified 0.4.3')
    parser.add_argument('project',nargs='?');parser.add_argument('--sample',action='store_true')
    parser.add_argument('--check',action='store_true',help='Check local launch files without opening a window')
    args=parser.parse_args();suffix='.exe' if os.name=='nt' else ''
    packaged=ROOT/('native/bin/scantailor-ocr-unified'+suffix)
    exe=packaged if packaged.is_file() else ROOT/('native/build/scantailor-ocr-unified-0.4.3'+suffix)
    try:
        for path in (exe,exe.parent/('scantailor-experimental-cli'+suffix),python_path(ROOT)):
            if not path.is_file():raise FileNotFoundError('Program dosyası eksik: '+str(path))
        project=Path(args.project).resolve() if args.project else SAMPLE if args.sample else None
        if project and not project.is_file():raise FileNotFoundError('Proje bulunamadı: '+str(project))
        if args.check:
            print('Native program, Qt, OCR köprüsü ve seçilen proje hazır.');return 0
        initialize(ROOT)
        env=os.environ.copy();env['STOU_ROOT']=str(ROOT);env['PYTHONUTF8']='1'
        env['PYTHONPATH']=str(ROOT/'src')+os.pathsep+env.get('PYTHONPATH','')
        active=data_root(ROOT)/'projects/active-batch.json'
        if active.is_file() and not args.project and not args.sample:
            batch=json.loads(active.read_text('utf8'))
            if Path(batch.get('queue','')).is_file() and Path(batch.get('request','')).is_file():
                env['STOU_QUEUE_PATH']=batch['queue'];env['STOU_MONITOR_REQUEST']=batch['request']
        if project and (binding_path:=Path(str(project)+'.unified-edit.json')).is_file():
            binding=json.loads(binding_path.read_text('utf8'))
            if Path(binding.get('queue','')).is_file():
                env['STOU_QUEUE_PATH']=binding['queue']
                if active.is_file():
                    batch=json.loads(active.read_text('utf8'))
                    if Path(batch.get('queue','')).resolve()==Path(binding['queue']).resolve() and Path(batch.get('request','')).is_file():
                        env['STOU_MONITOR_REQUEST']=batch['request']
        subprocess.Popen([str(exe),*([str(project)] if project else [])],cwd=ROOT,env=env)
        return 0
    except (OSError,ValueError) as error:
        if sys.stderr:print(str(error),file=sys.stderr)
        else:
            from tkinter import Tk,messagebox
            app=Tk();app.withdraw();messagebox.showerror('ScanTailor OCR Unified',str(error));app.destroy()
        return 1

if __name__=='__main__':raise SystemExit(main())
