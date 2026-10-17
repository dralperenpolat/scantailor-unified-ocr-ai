"""Build a fresh CPU/Tesseract runtime from a verified standalone Python."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--base',required=True);parser.add_argument('--destination',required=True);args=parser.parse_args()
    source=Path(args.base).resolve();dest=Path(args.destination).resolve()
    if dest.exists():raise FileExistsError('Minimal runtime destination must be new')
    if dest.is_relative_to(source):raise ValueError('Destination cannot be inside source runtime')
    ignore=shutil.ignore_patterns('site-packages','__pycache__','*.pyc','Scripts')
    shutil.copytree(source,dest,ignore=ignore)
    python=dest/('python.exe' if os.name=='nt' else 'bin/python3')
    subprocess.run([str(python),'-m','ensurepip','--upgrade'],check=True)
    subprocess.run([str(python),'-m','pip','install','--disable-pip-version-check','--no-cache-dir','-r',str(Path(__file__).with_name('requirements-minimal.txt'))],check=True)
    subprocess.run([str(python),'-m','pip','check'],check=True)
    subprocess.run([str(python),'-c','import importlib.util; assert importlib.util.find_spec("paddle") is None; assert importlib.util.find_spec("torch") is None; import fitz,cv2,img2pdf,psutil'],check=True)
    print(dest)

if __name__=='__main__':main()
