import argparse,json
from pathlib import Path
from .cache import assemble

def main():
    parser=argparse.ArgumentParser(prog='scantailor-ocr-unified',description='Araştırma sürümü — aynı geometriyi paylaşan siyah-beyaz görüntü ve gri OCR kopyasından PDF üretir.')
    parser.add_argument('project',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--cache',type=Path,default=Path('.stou-cache'))
    args=parser.parse_args()
    result=assemble(args.project,args.output,args.cache)
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0

if __name__=='__main__':raise SystemExit(main())
