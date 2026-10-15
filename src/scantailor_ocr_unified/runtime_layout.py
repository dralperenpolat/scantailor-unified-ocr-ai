"""Relocatable application assets and persistent, per-user working data."""
from __future__ import annotations
import os
from pathlib import Path
import shutil
import sys


def native_model_directory(path):
    """Paddle's native JSON/model loader on Windows requires an ASCII path.

    Prefer the existing NTFS alias: no duplicate weights. Volumes without
    short names use an atomic, versioned model-only cache under an ASCII base.
    """
    path = Path(path).resolve()
    if os.name != 'nt' or str(path).isascii():
        return str(path)
    import ctypes
    from ctypes import wintypes
    fn = ctypes.windll.kernel32.GetShortPathNameW
    fn.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD]
    fn.restype = wintypes.DWORD
    buffer = ctypes.create_unicode_buffer(32768)
    if fn(str(path), buffer, len(buffer)) and buffer.value.isascii():
        return buffer.value
    import hashlib
    import json
    import uuid
    files = sorted(p for p in path.rglob('*') if p.is_file())
    stamp = [(str(p.relative_to(path)), p.stat().st_size, p.stat().st_mtime_ns) for p in files]
    key = hashlib.sha256((str(path)+json.dumps(stamp)).encode('utf8')).hexdigest()
    for name in ('LOCALAPPDATA', 'PROGRAMDATA', 'PUBLIC'):
        base = os.environ.get(name, '')
        if not base or not base.isascii():
            continue
        target = Path(base)/'ScanTailorOCRUnified'/'ModelCache'/key
        try:
            target.mkdir(parents=True, exist_ok=True)
            for source in files:
                dest = target/source.relative_to(path)
                if dest.is_file() and dest.stat().st_size == source.stat().st_size:
                    continue
                dest.parent.mkdir(parents=True, exist_ok=True)
                temporary = dest.with_name(dest.name+'.'+uuid.uuid4().hex+'.tmp')
                shutil.copyfile(source, temporary)
                os.replace(temporary, dest)
            return str(target)
        except OSError:
            continue
    raise RuntimeError('Paddle model yolu açılamadı. Programı Türkçe karakter içermeyen bir klasöre kurun.')


def prepare_math_libraries():
    """PaddleX imports Torch if present: on Windows load its DLLs first.

    The two official CPU wheels include DLLs with overlapping names. Loading
    Paddle before Torch breaks shm.dll with WinError 127. CUDA-only separate
    environments without Torch are unaffected.
    """
    if os.name=='nt':
        import importlib.util
        if importlib.util.find_spec('torch') is not None:
            import torch


def app_root():
    return Path(os.environ.get('STOU_ROOT', Path(__file__).resolve().parents[2])).resolve()


def data_root(root=None):
    root = Path(root) if root else app_root()
    if os.environ.get('STOU_DATA'):
        return Path(os.environ['STOU_DATA']).resolve()
    # Keep existing development queues and projects exactly where they were.
    if not (root / 'release.json').is_file():
        return root
    base = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) if os.name == 'nt' else Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    return base / 'scantailor-ocr-unified'


def resource_path(name, root=None, *, writable=False):
    root = Path(root) if root else app_root()
    local = data_root(root) / 'resources' / name
    if writable or local.exists():return local
    bundled=root/'resources'/name
    shared=os.environ.get('STOU_ASSET_ROOT')
    return Path(shared)/'resources'/name if shared and not bundled.exists() else bundled


def environment_python(directory):
    return Path(directory) / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python3')


def python_path(root=None):
    if os.environ.get('STOU_PYTHON'):return Path(os.environ['STOU_PYTHON'])
    root = Path(root) if root else app_root()
    for path in (root / 'runtime/python.exe', root / 'runtime/bin/python3', environment_python(root / '.venv')):
        if path.is_file():
            return path
    return Path(sys.executable)


def paddle_python(root, device):
    name = 'paddle' if device.startswith('gpu') else 'paddle_cpu'
    for base in (data_root(root), Path(root),Path(os.environ.get('STOU_ASSET_ROOT',root))):
        candidate = environment_python(base / 'resources/runtimes' / name)
        if candidate.is_file():
            return candidate
    # Distribution bundles the complete CPU stack in its own interpreter.
    bundled = python_path(root)
    if (Path(root) / 'release.json').is_file():
        import json
        if json.loads((Path(root)/'release.json').read_text('utf8')).get('cpu_bundled',True):return bundled
        return environment_python(data_root(root)/'resources/runtimes'/name)
    # Older local development installations used the CUDA wheel for CPU too.
    return environment_python(Path(os.environ.get('STOU_ASSET_ROOT',root)) / 'resources/runtimes/paddle')


def initialize(root=None):
    root = Path(root).resolve() if root else app_root()
    data = data_root(root)
    os.environ.update(STOU_ROOT=str(root), STOU_DATA=str(data), PYTHONUTF8='1', PYTHONNOUSERSITE='1', PYTHONDONTWRITEBYTECODE='1')
    sys.dont_write_bytecode = True
    data.mkdir(parents=True, exist_ok=True)
    if data != root:
        # Seed small, mutable language assets once. Never replace user models.
        for pattern in ('ocr-catalog.json', 'tessdata_best/*.traineddata', 'tessdata_fast/*.traineddata'):
            for source in (root / 'resources').glob(pattern):
                target = data / 'resources' / source.relative_to(root / 'resources')
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists():
                    try:
                        with target.open('xb') as dest, source.open('rb') as src:
                            shutil.copyfileobj(src, dest)
                    except FileExistsError:
                        pass
        # Models are assets, caches are user data; Paddle must not download into
        # a read-only application directory. Individual bundled model paths
        # are supplied explicitly by both Paddle workers.
    return data


if __name__ == '__main__':
    print(initialize())
