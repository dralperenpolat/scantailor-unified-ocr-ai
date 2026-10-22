"""Render the full UVDoc grid for comparison with native curve candidates."""
from pathlib import Path
import argparse
import json
import sys
import time
import cv2
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src'))
from scantailor_ocr_unified.geometry_runtime import analyze
from scantailor_ocr_unified.mesh_geometry import remap_mesh

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path,help='Folder containing *-ai-input.png previews')
parser.add_argument('output',type=Path)
args=parser.parse_args()
folder=args.output;folder.mkdir(parents=True,exist_ok=True)
for image in sorted(args.source.glob('*-ai-input.png')):
    index=image.name.removesuffix('-ai-input.png')
    start=time.perf_counter();result=analyze({'image':str(image),'device':'cpu'});grid=np.array(result['grid'],dtype='float32')
    source=cv2.imread(str(image));warped=remap_mesh(source,grid)
    cv2.imwrite(str(folder/f'{index}-full-grid.png'),warped)
    (folder/f'{index}-grid.json').write_text(json.dumps(result,ensure_ascii=False),encoding='utf8')
    print(index,'seconds',round(time.perf_counter()-start,3),'shape',grid.shape,'reasons',result['reasons'],flush=True)
