"""Versioned preferences. Output, OCR language and UI language are independent."""
from __future__ import annotations
import json
import os
from pathlib import Path
import uuid
from .runtime_layout import data_root

DEFAULTS = {
    'schema': 1, 'cache_quota_gib': 10, 'reserve_gib': 2, 'temp_root': '',
    'resource_profile': 'balanced', 'compression_profile': 'lossless', 'compression_options': {},
    'ocr_compare': False, 'ocr_confidence_threshold': .85,
    'paddle_version': 'PP-OCRv5', 'structured_text': True,
    'output_format': 'pdf', 'verapdf': '', 'ocrmypdf': '',
}
PROFILES = {
    'cpu_fast': {'ocr_engine':'tesseract','ocr_device':'cpu','tess_quality':'fast','ocr_timing':'after',
        'resource_profile':'balanced','appearance':'native','native_scale':1,'ocr_max_edge':3000,
        'auto_warp':False,'page_cleanup':False,'content_fit':False,'paper':'a4','ocr_compare':False},
    'cpu_quality': {'ocr_engine':'tesseract','ocr_device':'cpu','tess_quality':'best','ocr_timing':'after',
        'resource_profile':'balanced','appearance':'soft_local','native_scale':2,'ocr_max_edge':4200,
        'auto_warp':True,'page_cleanup':True,'content_fit':True,'paper':'a4_text','ocr_compare':False},
    'de_legal': {'language': 'deu', 'paddle_language': 'de', 'appearance': 'soft_local', 'margin_mm': 10, 'paper': 'a4_text', 'ocr_compare': True},
    'tr_legal': {'language': 'tur', 'paddle_language': 'tr', 'appearance': 'soft_local', 'margin_mm': 10, 'paper': 'a4_text', 'ocr_compare': True},
    'small_print': {'appearance': 'gray', 'native_scale': 2, 'ocr_max_edge': 6000, 'page_cleanup': False, 'ocr_compare': True},
    'illustrated': {'appearance': 'native', 'paper': 'native', 'page_cleanup': False, 'compression_profile': 'lossless'},
}

def atomic_json(path, value):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name('.'+path.name+'.'+uuid.uuid4().hex+'.tmp')
    try:
        with temporary.open('x', encoding='utf8') as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)

def load(root=None):
    path = data_root(root)/'preferences.json'
    if not path.exists(): return dict(DEFAULTS)
    value = json.loads(path.read_text('utf8'))
    if value.get('schema') != 1: raise ValueError('Unsupported preferences version')
    return {**DEFAULTS, **value}

def save(value, root=None):
    current = {**load(root), **value, 'schema': 1}
    for key in ('cache_quota_gib', 'reserve_gib'):
        current[key] = float(current[key])
        if not 0 <= current[key] <= 100000: raise ValueError('Invalid storage limit')
    if current['resource_profile'] not in ('fast','balanced','quiet'): raise ValueError('Invalid resource profile')
    from .pdf_profiles import configuration
    configuration(current['compression_profile'],current.get('compression_options'))
    if current['output_format'] not in ('pdf','pdfa'): raise ValueError('Invalid output format')
    if not 0 <= float(current['ocr_confidence_threshold']) <= 1: raise ValueError('Invalid OCR threshold')
    atomic_json(data_root(root)/'preferences.json', current)
    return current

def book_profiles(root=None):
    path = data_root(root)/'book-profiles.json'
    return {**PROFILES, **(json.loads(path.read_text('utf8')) if path.exists() else {})}

def save_profile(name, value, root=None):
    if not name.strip() or len(name)>100: raise ValueError('Invalid profile name')
    allowed = {'language','paddle_language','appearance','margin_mm','paper','ocr_compare','native_scale','ocr_max_edge','page_cleanup','compression_profile','ocr_engine','ocr_device','tess_quality','resource_profile','ocr_timing','auto_warp','content_fit'}
    path = data_root(root)/'book-profiles.json'
    custom = json.loads(path.read_text('utf8')) if path.exists() else {}
    allowed.add('compression_options')
    custom[name] = {k:v for k,v in value.items() if k in allowed}
    atomic_json(path, custom)
    return custom[name]
