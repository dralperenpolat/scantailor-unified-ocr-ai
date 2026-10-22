"""Explicit post-Output stages. No queue mutation; source TIFFs stay unchanged.

Cleanup approvals are bound to source bytes AND rendering settings. Every PDF
run is a new, atomically published version. OCR only overlays the final PDF.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import re
import time
import uuid
from pathlib import Path

import fitz
import numpy as np
from PIL import Image
from .settings import atomic_json
from .storage import ensure_space
from .runtime_layout import initialize

VERSION = 1


def file_hash(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def rendering_settings(req):
    return {"appearance": req.get("appearance", "native"),
            "output_finish": req.get("output_finish", {})}


def page_signature(row, req):
    payload = {"source": file_hash(row["source"]), "gray": file_hash(row["gray"]),
               "rendering": rendering_settings(req), "version": VERSION}
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


def render(row, req):
    from .output_finish import geometry_finish, tonal_finish
    with Image.open(row["gray"]) as image:
        gray = image.convert("L")
    with Image.open(row["source"]) as image:
        visible = image.convert("RGB") if image.mode in ("RGB", "RGBA", "P") else image.convert("L")
    opts = req.get("output_finish", {})
    gray, visible, _ = geometry_finish(gray, visible, opts)
    mode = req.get("appearance", "native")
    if mode != "native":
        visible = gray.copy()
        if mode == "soft":
            from .engine import soft_monochrome
            visible = Image.fromarray(soft_monochrome(np.asarray(visible)))
        elif mode == "soft_local":
            from .output_appearance import local_soft_monochrome
            visible = Image.fromarray(local_soft_monochrome(np.asarray(visible)))
    if opts.get("white") or opts.get("fine"):
        pixels, _ = tonal_finish(visible, opts.get("white", False), opts.get("fine", False))
        visible = Image.fromarray(pixels)
    return gray, visible


def thumbnail(image, path):
    small = image.copy()
    small.thumbnail((1400, 1400), Image.Resampling.LANCZOS)
    small.save(path)


def embed_image(page, rectangle, visible):
    """Retain true binary scans as lossless CCITT; never threshold gray pixels."""
    import img2pdf
    if visible.mode == "L" and sum(visible.histogram()[1:255]) == 0:
        visible = visible.convert("1", dither=Image.Dither.NONE)
    stream = io.BytesIO()
    if visible.mode == "1":
        visible.save(stream, format="TIFF", compression="group4", dpi=(72, 72))
        with fitz.open(stream=img2pdf.convert(stream.getvalue()), filetype="pdf") as binary:
            page.show_pdf_page(rectangle, binary, 0)
    else:
        visible.save(stream, format="PNG", optimize=False)
        page.insert_image(rectangle, stream=stream.getvalue())


def cleanup(req, notify):
    from .page_cleanup import analyze, apply
    work = Path(req["work"])
    rows = req.get("pages", [])
    if not rows:
        raise ValueError("No pages selected / Seçili sayfa yok.")
    result = {"ok": True, "version": VERSION, "mode": "cleanup", "applied": False,
              "project_fingerprint": req.get("project_fingerprint"), "rendering": rendering_settings(req),
              "receipt": str(work / "cleanup.json"), "pages": []}
    for number, row in enumerate(rows, 1):
        ensure_space([work], needed=32 * 1024**2, reserve_gib=req.get("reserve_gib", 2))
        stamp = page_signature(row, req)
        gray, visible = render(row, req)
        mask, report = analyze(gray)
        stem = work / ("page-%04d" % row["page"])
        before, after, mask_path = str(stem) + "-before.png", str(stem) + "-after.png", str(stem) + "-mask.png"
        thumbnail(visible, before)
        thumbnail(Image.fromarray(apply(visible, mask)), after)
        Image.fromarray(mask).save(mask_path)
        if page_signature(row, req) != stamp:
            raise ValueError("Output changed during cleanup / Temizlik sırasında çıktı değişti.")
        result["pages"].append({**row, "signature": stamp, "mask": mask_path,
                                "mask_sha256": file_hash(mask_path), "before": before, "after": after, "report": report})
        notify({"page": number, "total": len(rows)})
    result["review_scope"] = [row_key(r) for r in result["pages"]]
    previous = load_review(req)
    if previous:
        pending = set(result["review_scope"])
        result["pages"] += [r for r in previous["pages"] if row_key(r) not in pending and r.get("applied", previous.get("applied", False))]
    for row in result["pages"]:
        row.setdefault("applied", False)
    result["pages"].sort(key=lambda r: r["page"])
    result["applied"] = any(r["applied"] for r in result["pages"])
    atomic_json(result["receipt"], result)
    return result


def load_review(req, require=False):
    path = req.get("cleanup_review")
    if not path:
        if require:
            raise ValueError("Prepare cleanup first / Önce temizliği hazırlayın.")
        return None
    result = json.loads(Path(path).read_text(encoding="utf8"))
    if result.get("version") != VERSION:
        raise ValueError("Cleanup version changed; prepare again / Temizliği yeniden hazırlayın.")
    return result


def validate_review(result, req):
    current = {row_key(r): r for r in req["pages"]}
    scope = set(result.get("review_scope", current))
    for row in result["pages"]:
        if row_key(row) not in scope:
            continue
        now = current.get(row_key(row), row)
        fresh = row.get("input_stamp") == now.get("input_stamp") if "input_stamp" in now else result.get("project_fingerprint") == req.get("project_fingerprint")
        if not fresh or page_signature(now, req) != row["signature"] or file_hash(row["mask"]) != row["mask_sha256"]:
            raise ValueError("This page changed; prepare its cleanup again / Bu sayfa değişti; temizliğini yeniden hazırlayın.")


def approve_cleanup(req, applied):
    result = load_review(req, True)
    if applied:
        validate_review(result, req)
    scope = set(result.get("review_scope", [row_key(r) for r in result["pages"]]))
    for row in result["pages"]:
        if row_key(row) in scope:
            row["applied"] = applied
    result["applied"] = any(r.get("applied", False) for r in result["pages"])
    atomic_json(result["receipt"], result)
    return result


def output_path(req, suffix):
    folder = Path(req["output_dir"])
    folder.mkdir(parents=True, exist_ok=True)
    title = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", req.get("title", "Book")).strip(" .")[:120] or "Book"
    return folder / (title + "-" + time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6] + suffix + ".pdf")


def row_key(row):
    return row.get("key") or str(Path(row["source"]).resolve())


def stat_signature(row):
    return [{"size": Path(row[k]).stat().st_size, "mtime_ns": Path(row[k]).stat().st_mtime_ns}
            if Path(row[k]).is_file() else None for k in ("source", "gray")]


def pdf_settings(req):
    return {**rendering_settings(req), "paper": req.get("paper", "a4"),
            "margin_mm": float(req.get("margin_mm", 10)), "target_x_mm": float(req.get("target_x_mm", 0)),
            "content_fit": bool(req.get("content_fit", False))}


def previous_pdf(req, key="previous_pdf"):
    if not req.get(key):
        return None, {}
    path = Path(req[key])
    receipt = path.with_suffix(".workflow.json")
    if not path.is_file() or not receipt.is_file():
        return None, {}
    saved = json.loads(receipt.read_text(encoding="utf8"))
    if saved.get("sha256") != file_hash(path):
        raise ValueError("Previous PDF changed outside the app / Önceki PDF uygulama dışında değişti.")
    return path, saved


def valid_cleanup_rows(req, previous=None):
    review = load_review(req)
    if not review or not review.get("applied"):
        return {}, []
    current = {row_key(r): r for r in req["pages"]}
    masks, stale = {}, []
    for row in review["pages"]:
        if not row.get("applied", review.get("applied", False)):
            continue
        now = current.get(row_key(row))
        if now is None:
            continue  # Removed pages cannot invalidate the rest of the book.
        fresh_input = row.get("input_stamp") == now.get("input_stamp") if "input_stamp" in now else review.get("project_fingerprint") == req.get("project_fingerprint")
        try:
            old = (previous or {}).get(row_key(now), {})
            missing = any(value is None for value in stat_signature(now))
            same_rendering = all(old.get("pdf_settings", {}).get(k) == v for k, v in rendering_settings(req).items())
            signature = old.get("image_signature") if missing and same_rendering and old.get("input_stamp") == now.get("input_stamp") else page_signature(now, req)
            fresh = fresh_input and signature == row["signature"] and file_hash(row["mask"]) == row["mask_sha256"]
        except (FileNotFoundError, KeyError):
            fresh = False
        if fresh:
            masks[row_key(row)] = row
        elif "input_stamp" in now:
            stale.append(now["page"])
        else:
            raise ValueError("Output or cleanup mask changed; prepare again / Çıktı veya maske değişti; temizliği yeniden hazırlayın.")
    return masks, stale


def reading_copy(req, result, notify):
    """Compress a derivative while keeping the page-update/OCR master lossless."""
    from .pdf_profiles import compress, configuration
    profile = req.get("compression_profile", "lossless")
    options = req.get("compression_options") or {}
    configuration(profile, options)
    result["reading_pdf"] = result["pdf"]
    if profile != "lossless" or options.get("recompress_flate", False):
        target = output_path(req, "-" + profile)
        flag = req.get("cancel_file")
        result["compressed"] = compress(result["pdf"], target, profile, notify,
                                       options=options, cancelled=lambda: bool(flag and Path(flag).exists()))
        result["reading_pdf"] = str(target)
    atomic_json(Path(result["pdf"]).with_suffix(".workflow.json"), result)
    return result


def create_pdf(req, notify):
    from .book_layout import plan
    from .page_cleanup import apply
    from .pdf_profiles import publish, configuration
    configuration(req.get("compression_profile", "lossless"), req.get("compression_options"))
    rows = req.get("pages", [])
    if not rows:
        raise ValueError("No output pages / Çıktı sayfası yok.")
    if len({row_key(r) for r in rows}) != len(rows):
        raise ValueError("Duplicate output page / Yinelenen çıktı sayfası.")
    previous_path, previous = previous_pdf(req)
    old_by_key = {r["key"]: r for r in previous.get("records", [])}
    masks, stale_cleanup = valid_cleanup_rows(req, old_by_key)
    settings = pdf_settings(req)
    records, changed, reused = [], [], []
    for index, row in enumerate(rows):
        key = row_key(row)
        old = old_by_key.get(key, {})
        stat = stat_signature(row)
        # Most unchanged pages require only stat calls. A touched output file
        # is hashed before deciding whether its already embedded page is valid.
        same_rendering = all(old.get("pdf_settings", {}).get(k) == v for k, v in rendering_settings(req).items())
        signature = old.get("image_signature") if old.get("file_stat") == stat and same_rendering else None
        if any(value is None for value in stat):
            old_stat = old.get("file_stat", [])
            remaining_unchanged = len(old_stat) == len(stat) and all(value is None or value == prior for value, prior in zip(stat, old_stat))
            if not (previous_path and row.get("input_stamp") and row["input_stamp"] == old.get("input_stamp")
                    and old.get("pdf_settings") == settings and remaining_unchanged):
                raise ValueError("Regenerate this page at Output / Bu sayfayı Çıktı aşamasında yeniden üretin.")
            signature = old.get("image_signature")
        if not signature:
            signature = page_signature(row, req)
        cleanup_hash = masks[key]["mask_sha256"] if key in masks else ""
        same = (previous_path and old.get("pdf_settings") == settings
                and old.get("image_signature") == signature
                and old.get("input_stamp") == row.get("input_stamp")
                and old.get("cleanup_hash", "") == cleanup_hash)
        record = {**row, "key": key, "page": index+1, "image_signature": signature, "file_stat": stat,
                  "pdf_settings": settings, "cleanup_hash": cleanup_hash}
        if not same and any(value is None for value in stat):
            raise ValueError("Regenerate the changed page at Output / Değişen sayfayı Çıktı aşamasında yeniden üretin.")
        if same:
            record.update(layout=old["layout"], ocr_signature=old.get("ocr_signature", ""),
                          has_ocr=old.get("has_ocr", False), previous_page=old["page"])
            reused.append(index)
        else:
            changed.append(index)
        records.append(record)

    destination = output_path(req, "")
    work = Path(req["work"])
    ensure_space([work, destination.parent], needed=64*1024**2, reserve_gib=req.get("reserve_gib", 2))
    paper = settings["paper"]
    target = settings["target_x_mm"]
    compatible = [r for r in previous.get("records", []) if r.get("pdf_settings") == settings]
    if not target and compatible:
        target = next((r["layout"].get("target_body_mm") for r in compatible if r["layout"].get("target_body_mm")), 0)
    changed_paths = [rows[i]["source"] for i in changed]
    layout = plan(changed_paths, paper, settings["margin_mm"], target,
                  notify=lambda n, total: notify({"page": n, "total": total, "phase": "layout"}),
                  workers=min(2, max(1, int(req.get("threads", 2))))) if changed else {"pages": []}
    if compatible and paper == "a4":
        old_scale = min(r["layout"].get("pxmm", 25.4/300) for r in compatible)
        for row in layout["pages"]:
            with Image.open(row["source"]) as image:
                w, h = image.size
            capacity = min((210-2*settings["margin_mm"])/w, (297-2*settings["margin_mm"])/h)
            row["pxmm"] = min(old_scale, capacity)
            row["image_mm"] = [w*row["pxmm"], h*row["pxmm"]]
    partial = work / "changed-pages.part.pdf"
    fitted = work / "fitted-pdf.part.pdf"
    staged = work / "image-pdf.part.pdf"
    verification = {"page_count": len(rows), "ocr": False, "unchanged_pages_copied": True}
    try:
        if changed:
            with fitz.open() as doc:
                for local, index in enumerate(changed):
                    row, placement = rows[index], layout["pages"][local]
                    ensure_space([work, destination.parent], needed=32*1024**2, reserve_gib=req.get("reserve_gib", 2))
                    _, visible = render(row, req)
                    approved = masks.get(row_key(row))
                    if approved:
                        with Image.open(approved["mask"]) as image:
                            visible = Image.fromarray(apply(visible, np.asarray(image.convert("L"))))
                    pw, ph = [v*72/25.4 for v in placement["page_mm"]]
                    iw, ih = [v*72/25.4 for v in placement["image_mm"]]
                    page = doc.new_page(width=pw, height=ph)
                    embed_image(page, fitz.Rect((pw-iw)/2, (ph-ih)/2, (pw+iw)/2, (ph+ih)/2), visible)
                    notify({"page": local+1, "total": len(changed), "phase": "render"})
                    fitz.TOOLS.store_shrink(100)
                doc.save(partial, garbage=4, deflate=True, use_objstms=1)
            if settings["content_fit"] and paper in ("a4_text", "a4_fill"):
                from .pdf_layout import plan as content_plan, reframe
                fitted_layout = content_plan(partial, layout, paper, settings["margin_mm"], target)
                if paper == "a4_fill":
                    for row in fitted_layout["pages"]:
                        if row["blank"]:
                            continue
                        crop = fitz.Rect(row["crop_pt"])
                        scale = row["capacity"]
                        w, h = crop.width*scale, crop.height*scale
                        x, y = (210*72/25.4-w)/2, (297*72/25.4-h)/2
                        row.update(scale=scale, dest_pt=[x, y, x+w, y+h])
                verification.update(reframe(partial, fitted, fitted_layout))
                layout = fitted_layout
            for local, index in enumerate(changed):
                records[index].update(layout=layout["pages"][local], has_ocr=False, ocr_signature="")

        from contextlib import ExitStack
        with ExitStack() as stack:
            old_doc = stack.enter_context(fitz.open(previous_path)) if previous_path else None
            new_doc = stack.enter_context(fitz.open(fitted if fitted.exists() else partial)) if changed else None
            result_doc = stack.enter_context(fitz.open())
            changed_map = {index: local for local, index in enumerate(changed)}
            for index, record in enumerate(records):
                if index in changed_map:
                    n = changed_map[index]
                    result_doc.insert_pdf(new_doc, from_page=n, to_page=n)
                else:
                    n = record["previous_page"]-1
                    result_doc.insert_pdf(old_doc, from_page=n, to_page=n)
            result_doc.set_metadata({"title": req.get("title", ""), "creator": "ScanTailor Unified · page updates"})
            result_doc.save(staged, garbage=4, deflate=True, use_objstms=1)
        for index in changed:
            if page_signature(rows[index], req) != records[index]["image_signature"]:
                raise ValueError("Output changed during export / PDF oluşturulurken çıktı değişti.")
        # Reused images were not opened, measured, cleaned, rendered or OCRed.
        # Their original page objects, including OCR, were copied directly.
        for index in reused:
            if stat_signature(rows[index]) != records[index]["file_stat"]:
                raise ValueError("Output changed during export / PDF oluşturulurken çıktı değişti.")
        with fitz.open(staged) as doc:
            if len(doc) != len(rows):
                raise ValueError("PDF page count differs / PDF sayfa sayısı uyuşmuyor.")
        publish(staged, destination)
        result = {"ok": True, "mode": "pdf", "pdf": str(destination), "pages": len(rows),
                  "layout": {"paper": paper, "pages": [r["layout"] for r in records]},
                  "records": records, "changed_pages": [i+1 for i in changed], "reused_pages": [i+1 for i in reused],
                  "cleanup_needs_review": stale_cleanup, "verification": verification,
                  "cleanup_applied": bool(masks), "project_fingerprint": req.get("project_fingerprint"),
                  "sha256": file_hash(destination)}
        # The master remains recoverable even if a derivative is cancelled.
        atomic_json(destination.with_suffix(".workflow.json"), result)
        return reading_copy(req, result, notify)
    finally:
        for path in (partial, fitted, staged):
            path.unlink(missing_ok=True)


def add_ocr(req, notify):
    from .smart_pdf import process, copy_navigation
    from .pdf_profiles import publish, configuration
    configuration(req.get("compression_profile", "lossless"), req.get("compression_options"))
    source = Path(req["source_pdf"])
    stamp = file_hash(source)
    if req.get("source_pdf_sha256") and stamp != req["source_pdf_sha256"]:
        raise ValueError("Final PDF changed; create PDF again / Son PDF değişti; PDF'yi yeniden oluşturun.")
    receipt_path = source.with_suffix(".workflow.json")
    previous = json.loads(receipt_path.read_text(encoding="utf8")) if receipt_path.exists() else {}
    records = previous.get("records", [])
    options = {k: req.get(k) for k in ("ocr_engine", "ocr_device", "language", "paddle_language", "paddle_version", "tess_quality")}
    if req.get("ocr_cache"):
        from .pdf_export import recognition_signature
        from .runtime_layout import resource_path
        options["models"] = recognition_signature(req, req.get("ocr_engine", "tesseract"),
            resource_path("tessdata_"+req.get("tess_quality", "best"), req.get("root")), req.get("language", "deu"))
    signature = hashlib.sha256(json.dumps(options, sort_keys=True).encode()).hexdigest()
    with fitz.open(source) as doc:
        if records and len(records) != len(doc):
            raise ValueError("OCR page manifest differs / OCR sayfa kaydı uyuşmuyor.")
        if not records:
            records = [{"page": i+1, "key": str(i+1)} for i in range(len(doc))]
        changed = [i for i, row in enumerate(records) if row.get("ocr_signature") != signature]
    dest = output_path(req, "-OCR")
    conf = {k: v for k, v in req.items() if k != "pages"}
    partial = Path(req["work"]) / ("ocr-pages-"+uuid.uuid4().hex+".pdf")
    merged = Path(req["work"]) / "ocr-merged.part.pdf"
    try:
        if changed:
            conf["pages"] = ",".join(str(i+1) for i in changed)
            # If an already recognized page needs a new engine/language, strip
            # only its old invisible text before calling the smart overlay.
            fresh = Path(req["work"]) / "ocr-source.part.pdf"
            with fitz.open(source) as src:
                if any(records[i].get("has_ocr") for i in changed):
                    for i in changed:
                        for word in src[i].get_text("words"):
                            src[i].add_redact_annot(fitz.Rect(word[:4]), fill=False)
                        src[i].apply_redactions(images=0, graphics=0, text=0)
                    src.save(fresh, garbage=4, deflate=True)
            result = process(fresh if fresh.exists() else source, partial, conf, notify)
        else:
            result = {"ok": True, "pages": []}
        with fitz.open(source) as before, fitz.open() as after:
            layer = fitz.open(partial) if changed else None
            try:
                changed_map = {page: local for local, page in enumerate(changed)}
                for i in range(len(before)):
                    if i in changed_map:
                        local = changed_map[i]
                        after.insert_pdf(layer, from_page=local, to_page=local)
                        records[i].update(has_ocr=True, ocr_signature=signature)
                    else:
                        after.insert_pdf(before, from_page=i, to_page=i)
                copy_navigation(before, after, list(range(len(before))))
                after.save(merged, garbage=4, deflate=True, use_objstms=1)
            finally:
                if layer:
                    layer.close()
        if file_hash(source) != stamp:
            raise ValueError("Source PDF changed during OCR / OCR sırasında kaynak PDF değişti.")
        publish(merged, dest)
        result.update(mode="ocr", pdf=str(dest), records=records, cleanup_needs_review=previous.get("cleanup_needs_review", []),
                      changed_pages=[i+1 for i in changed], reused_pages=[i+1 for i in range(len(records)) if i not in changed],
                      source_pdf_sha256=stamp, project_fingerprint=req.get("project_fingerprint"), sha256=file_hash(dest))
        atomic_json(dest.with_suffix(".workflow.json"), result)
        return reading_copy(req, result, notify)
    finally:
        for path in (partial, partial.with_suffix(".verification.json"), merged, Path(req["work"])/"ocr-source.part.pdf"):
            path.unlink(missing_ok=True)


def run(req, notify=lambda event: None):
    work = Path(req["work"])
    work.mkdir(parents=True, exist_ok=True)
    initialize(req.get("root"))
    mode = req["mode"]
    if mode == "cleanup":
        result = cleanup(req, notify)
    elif mode in ("cleanup_apply", "cleanup_undo"):
        result = approve_cleanup(req, mode == "cleanup_apply")
    elif mode == "pdf":
        result = create_pdf(req, notify)
    elif mode == "ocr":
        result = add_ocr(req, notify)
    elif mode == "finalize":
        from .workflow_finalize import finalize
        result = finalize(req, notify)
    else:
        raise ValueError("Unknown workflow stage: " + mode)
    atomic_json(work / "result.json", result)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("request")
    args = parser.parse_args()
    req = json.loads(Path(args.request).read_text(encoding="utf8"))
    if req.get("cancel_file"):
        import os
        import threading
        def cancellation_watch():
            while not Path(req["cancel_file"]).exists():
                time.sleep(.15)
            try:
                import psutil
                children = psutil.Process().children(recursive=True)
                for child in children:
                    try: child.terminate()
                    except psutil.NoSuchProcess: pass
                _, live = psutil.wait_procs(children, timeout=1)
                for child in live:
                    try: child.kill()
                    except psutil.NoSuchProcess: pass
            finally:
                # Exact workspaces were registered before starting; a cancelled
                # process's leftovers can be removed by the project cache stage.
                os._exit(130)
        threading.Thread(target=cancellation_watch, daemon=True).start()
    run(req, lambda event: print("STOUWORK " + json.dumps(event), flush=True))


if __name__ == "__main__":
    main()
