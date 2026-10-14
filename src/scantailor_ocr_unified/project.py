"""Local, non-destructive book projects for the desktop preview."""
from __future__ import annotations
from pathlib import Path
import hashlib
import json
import os
import re
import uuid
from PIL import Image, ImageOps

from .runtime_layout import app_root, data_root
APP_ROOT = app_root()
IMAGE_TYPES = {'.jpg', '.jpeg', '.png', '.tif', '.tiff', '.bmp', '.webp'}

def natural_key(path):
    return [int(s) if s.isdigit() else s.lower() for s in re.split(r'(\d+)', str(path))]

def file_hash(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()

def read_rgb(path):
    with Image.open(path) as im:
        return ImageOps.exif_transpose(im).convert('RGB')

def new_project(path, title='Yeni kitap'):
    path = Path(path).resolve()
    return {'schema': 2, 'title': title, 'project_file': str(path),
            'language': 'deu', 'target_body_px': 30, 'output_dpi': 300,'output_mode':'soft',
            'normalize_text': True, 'pages': []}

def save_project(project):
    path = Path(project['project_file']).resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    data = json.dumps(project, ensure_ascii=False, indent=2)
    with temp.open('x', encoding='utf8') as f:
        f.write(data)
    os.replace(temp, path)

def open_project(path):
    path = Path(path).resolve()
    data = json.loads(path.read_text(encoding='utf8'))
    if data.get('schema') != 2 or not isinstance(data.get('pages'), list):
        raise ValueError('Bu dosya masaüstü uygulamasının kitap projesi değil.')
    data['project_file'] = str(path)
    return data

def page_entry(source, label, quad, source_hash=None):
    return {'id': uuid.uuid4().hex[:12], 'source': str(Path(source).resolve()),
            'source_hash': source_hash or file_hash(source), 'label': label,
            'quad': quad, 'method': 'auto', 'rotation': 0,
            'content': None, 'white_zones': [], 'threshold': 'otsu',
            'threshold_adjust': 0, 'normalize_light': True,
            'body_override': None, 'status': 'Bekliyor', 'warnings': []}

def cache_root(project):
    return Path(project['project_file']).parent / '.unified-cache'

def collect_images(folder):
    return sorted((p for p in Path(folder).iterdir() if p.is_file() and p.suffix.lower() in IMAGE_TYPES), key=natural_key)
