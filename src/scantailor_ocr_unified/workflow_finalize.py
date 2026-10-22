"""Finish an unsaved book after verifying its searchable PDF.

Only registered, owned generated files are removed. Originals, unregistered
files, changed PDFs and saved projects are never adopted by a directory scan.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import fitz


def read(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf8"))
    except (OSError, ValueError):
        return {}


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def safe(path, root):
    path, root = Path(path).absolute(), Path(root).absolute()
    if not root.is_dir() or not path.is_file():
        return False
    for item in (path, *path.parents, root, *root.parents):
        if item.is_symlink() or (hasattr(item, "is_junction") and item.is_junction()):
            return False
    return path.resolve().is_relative_to(root.resolve()) and path.resolve() != root.resolve()


def finalize(req, notify=lambda event: None):
    def cancelled():
        return bool(req.get('cancel_file') and Path(req['cancel_file']).exists())
    if cancelled():
        raise ValueError('Temizlik durduruldu; hiçbir dosya silinmedi.')
    project = Path(req["cache_owner"]).absolute()
    owner = project.as_posix()
    registry = read(project / ".project-cache.json")
    if read(project / ".stou-cache-owner.json").get("project") != owner:
        raise ValueError("Projenin dosya sahipliği doğrulanamadı; hiçbir dosya silinmedi.")
    saved = registry.get("saved_projects", []) + [req.get("saved_project", "")]
    if any(p and Path(p).is_file() for p in saved):
        raise ValueError("Kayıtlı projenin girdileri korunuyor. Seçime göre temizle işlemini kullanın.")
    pdf = Path(req["source_pdf"]).absolute()
    expected = req.get("source_pdf_sha256")
    receipt = read(pdf.with_suffix(".workflow.json"))
    notify({"phase": "verify", "page": 0, "total": 0})
    if not expected or not safe(pdf, pdf.parent) or sha(pdf) != expected or receipt.get("sha256") != expected:
        raise ValueError("Son PDF değişmiş veya doğrulanamıyor; hiçbir dosya silinmedi.")
    if receipt.get("project_fingerprint") != req.get("project_fingerprint"):
        raise ValueError("PDF güncel ayarları içermiyor; önce OCR’ı yeniden çalıştırın.")
    records = receipt.get("records", [])
    if not records or not all(row.get("has_ocr") for row in records):
        raise ValueError("Bütün sayfalarda OCR tamamlanmadan temizlik yapılamaz.")
    with fitz.open(pdf) as document:
        if len(document) != len(records) or document.is_encrypted:
            raise ValueError("Son PDF’nin sayfa sayısı doğrulanamadı.")
        text_pages = 0
        for index, page in enumerate(document):
            if cancelled():
                raise ValueError('Doğrulama durduruldu; hiçbir dosya silinmedi.')
            text_pages += bool(page.get_text().strip())
            # Decode a thumbnail too: a valid text layer alone is insufficient.
            page.get_pixmap(matrix=fitz.Matrix(.1, .1), alpha=False)
            notify({"phase": "verify", "page": index+1, "total": len(document)})
        if not text_pages:
            raise ValueError("PDF’de aranabilir metin bulunamadı; hiçbir dosya silinmedi.")

    candidates = {}
    retained = []
    roots = []
    work = Path(req["work"]).resolve()
    inputs = {Path(row[k]).absolute() for row in req.get("pages", []) for k in ("source", "gray") if row.get(k)}
    output_names = {Path(row['source']).name for row in req.get('pages', []) if row.get('source')}
    def add(path, root, marker=True):
        path = Path(path).absolute()
        if path == pdf or not safe(path, root):
            return
        stat = path.stat()
        candidates[path] = (Path(root), marker, stat.st_size, stat.st_mtime_ns)

    for row in registry.get("roots", []):
        root, role = Path(row["path"]).absolute(), row.get("role")
        if root.resolve() == work or read(root/".stou-cache-owner.json").get("project") != owner:
            continue
        if role not in {"native", "work", "ocr", "sources", "temporary", "review"}:
            continue
        roots.append(root)
        for path in root.rglob("*"):
            if not safe(path, root):
                continue
            name = path.name
            if role == "native":
                native_cache = name in output_names and path.parent in {root/'cache'/kind for kind in ('automask','predespeckle','speckles')}
                allowed = path.absolute() in inputs or native_cache or name in {".workflow-state.json", ".project-cache.json"} or path.is_relative_to(root/"cache"/"thumbs")
            elif role == "sources":
                allowed = name == "import.json" or (name.startswith("page-") and path.suffix in {".png", ".json"})
            elif role in {"ocr", "temporary", "review"}:
                allowed = not name.startswith(".stou-")
            else:
                allowed = name in {"request.json", "result.json", "cleanup.json", "cancel"} or name.endswith(("-mask.png", "-before.png", "-after.png", ".part.pdf")) or name.startswith(("ocr-pages-", ".smart-"))
            if allowed:
                add(path, root)
            elif not name.startswith('.stou-'):
                retained.append(str(path))

    # PDF ownership is established by a receipt and its digest, not a filename.
    compressed = {}
    for name in registry.get("pdfs", []):
        record = read(Path(name).with_suffix('.workflow.json'))
        master = Path(name)
        if master.is_file() and record.get('sha256') == sha(master):
            copy = record.get('compressed') or {}
            if copy.get('pdf') and copy.get('output_sha256'):
                compressed[Path(copy['pdf']).absolute()] = copy['output_sha256']
    for name in registry.get("pdfs", []):
        older = Path(name).absolute()
        if older == pdf:
            continue
        sidecar = older.with_suffix(".workflow.json")
        record = read(sidecar)
        if older.is_file() and record.get("sha256") and sha(older) == record["sha256"]:
            add(older, older.parent, False)
            add(sidecar, older.parent, False)
        elif older.is_file() and compressed.get(older) == sha(older):
            add(older, older.parent, False)
        elif older.exists():
            retained.append(str(older))
    add(pdf.with_suffix(".workflow.json"), pdf.parent, False)

    # Revalidate immediately before deletion. Keep recovery metadata until last.
    if sha(pdf) != expected:
        raise ValueError("Doğrulama sırasında PDF değişti; hiçbir dosya silinmedi.")
    ordered = sorted(candidates, key=lambda p: (p.name in {".project-cache.json", ".workflow-state.json"} or p.suffix == ".json", str(p)))
    removed = 0
    count = 0
    skipped = []
    for number, path in enumerate(ordered, 1):
        if cancelled():
            skipped.append('Temizlik kullanıcı tarafından durduruldu.')
            break
        root, marker, size, modified = candidates[path]
        # Retain recovery receipts when a preceding deletion failed.
        if skipped and (path.suffix == ".json" or path == pdf.with_suffix(".workflow.json")):
            continue
        valid = safe(path, root) and (not marker or read(root/".stou-cache-owner.json").get("project") == owner)
        try:
            stat = path.stat()
            if not valid or (stat.st_size, stat.st_mtime_ns) != (size, modified):
                raise OSError("Dosya değişti veya sahipliği doğrulanamadı")
            path.unlink()
            removed += size
            count += 1
        except OSError:
            skipped.append(str(path))
        notify({"phase": "cleanup", "page": number, "total": len(ordered), "bytes": removed, "file": path.name})

    # Remove empty directories only. Never recurse when deleting a directory.
    for root in sorted(set(roots), key=lambda p: len(p.parts), reverse=True):
        if not root.exists():
            continue
        for directory in sorted((p for p in root.rglob("*") if p.is_dir() and not p.is_symlink() and not (hasattr(p,"is_junction") and p.is_junction())), key=lambda p: len(p.parts), reverse=True):
            try: directory.rmdir()
            except OSError: pass
        marker_names = {".stou-cache-owner.json", ".stou-owner.json", ".stou-import-session.json"}
        children = list(root.iterdir())
        if all(p.name in marker_names and safe(p, root) for p in children):
            for path in children: path.unlink()
            try: root.rmdir()
            except OSError: pass
    if not project.exists():
        try: project.parent.rmdir()
        except OSError: pass
    return {"ok": not skipped, "mode": "finalize", "pdf": str(pdf), "sha256": expected,
            "bytes": removed, "files": count, "skipped": skipped, "retained": sorted({p for p in retained if Path(p).exists()}), "text_pages": text_pages}
