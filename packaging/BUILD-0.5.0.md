# ScanTailor OCR Unified 0.5.0

This version adds a startup wizard and a workspace for storage, review, PDF preparation and quality checks. Release status is determined by the acceptance results, not by the presence of this file.

## Startup and speed

On first launch choose OCR language and engine, timing, image processing, page size, compression, output folder, temporary folder, cache quota and reserved free space. Open **Startup settings** to change these later. Interface language takes effect on reopening; it does not change the OCR language.

| Profile | Recognition | Image processing | Expected cost |
| --- | --- | --- | --- |
| CPU fast | Tesseract Fast after images | 1× output; automatic dewarping, OCR-assisted edge cleanup and content fitting disabled | Lower CPU and memory demand |
| CPU quality | Tesseract Best after images | 2× output; dewarping, protected cleanup and content fitting enabled | More processing; accuracy still needs book-specific verification |
| GPU quality | PaddleOCR after images | Additional image corrections enabled | Requires a separately installed, compatible GPU runtime |

Deferred OCR publishes an image PDF before recognition starts. It does not eliminate OCR work. The searchable PDF is a separate, verified result. Fast models for German, Turkish and English are pinned by SHA-256 in `assets.json`.

## Workspace

- **Storage:** categories, free space, resource profiles and working-size estimates. Cleanup previews cover explicitly registered, reproducible cache files. Source photographs, project edits, OCR receipts and final PDFs are protected. The cache quota is not a cap on books, models or application files. Unexpected shutdowns can leave temporary files for inspection.
- **Queue and review:** SQLite WAL checkpoints, a small JSON view, 100-row pages, enqueue/prioritize commands during processing, source/output previews, warning reasons, confidence and alternate Tesseract text. Reviews are tied to the output hash. A changed page needs review again.
- **PDF preparation:** multi-file import, page ranges, preview, native scan extraction and a text-preserving route for PDFs needing no geometric changes. Existing text and vectors remain; image-only pages receive an invisible OCR layer. Imported metadata, bookmarks and labels are mapped back after processing.
- **Quality:** a reproducible 250-page sample, versioned manual references, CER/WER and manual defect tags. Unverified OCR is never used as ground truth. Layout roles and reading order are geometric suggestions; tables need inspection or optional structure analysis.
- **Export:** TXT, Markdown, ALTO and layout boxes; lossless, balanced and small PDFs; sample comparisons. Balanced/small use explicitly lossy JPEG settings. Binary images are retained. No lossy JBIG2 is used.
- **Recovery and metrics:** free-space checks, resource waiting, bounded transient retries, atomic PDF publication, stage wall time and sampled process-tree memory. These are not GPU-memory measurements or OCR accuracy scores.

## Optional components

PP-OCRv5 remains the default. PP-OCRv6 is an opt-in comparison candidate and may require model downloads; no superiority claim is made without a verified corpus. Kraken requires an executable and a user-selected compatible model. PP-StructureV3 runs in a separate selected Python environment and may download its own models. It does not replace the normal PDF rendering.

PDF/A requires separately installed OCRmyPDF and veraPDF. A result is published as PDF/A only when veraPDF reports compliance and page count/text checks pass. The ordinary verified PDF remains available if this optional step fails. Optional dependencies must be tested on the target machine before relying on them for an archive workflow.

## Packages and reproduction

`minimal` includes Qt, standalone Python, Tesseract, PDF processing and Fast/Best language data. `full` additionally includes the CPU AI runtime and models. GPU runtimes stay optional. Bundled large models are read from the application; downloaded models live in the user's data directory.

Windows:

```powershell
./packaging/build-windows.ps1 -Prefix <MSYS2-ucrt64>
python packaging/minimal_runtime.py --base <verified-python> --destination <new-minimal-runtime>
python packaging/stage_windows.py --destination <new-stage> --prefix <MSYS2-ucrt64> --runtime <runtime> --variant minimal
<stage>/runtime/python.exe packaging/smoke_cpu.py --root <stage> --work <new-qa> --fast
ISCC /DStageDir=<stage> /DReleaseDir=<release> /DVariant=minimal packaging/windows.iss
```

Linux uses `build-linux.sh`, `stage_linux.py --variant minimal|full`, `install-user.sh` and `uninstall-user.sh`. The installer only removes unchanged manifest-owned files; user data stays outside the application directory. Windows installer smoke tests require a disposable CI runner because installation changes its uninstall registry.

`requirements-minimal.txt` and full-runtime lockfiles pin Python dependencies; model downloads have checksums. `sbom.py` inventories the actual staged Python distributions and native/model files as CycloneDX. Both release workflows test installed CPU/PDF/UI behavior, upgrade from 0.4.5, uninstall and preservation of a user project, then request GitHub provenance and SBOM attestations. These checks must pass for the exact commit and package being delivered.

## Türkçe kısa kullanım

İlk açılışta **CPU hızlı** seçerek Tesseract Fast ve **OCR en son** ile başlayabilirsiniz. **PDF çıktı klasörü** ve **Geçici çalışma klasörü** üçüncü adımdadır. Ayarları daha sonra **Başlangıç ayarları** düğmesinden değiştirin. Çıktı klasörü boş bırakılamaz; klasörlerin yazılabilirliği kaydetmeden önce kontrol edilir.

**Çalışma merkezi** içinde depolamayı, sorunlu sayfaları, PDF araçlarını ve doğruluk derlemini bulabilirsiniz. Derlemdeki referans metinleri görüntüden kontrol etmeden doğruluk oranı oluşmaz. Küçük PDF profili görüntüyü değiştirebilir; önce örnek sayfaları karşılaştırın. PDF/A, Kraken ve PP-Structure ek kurulum gerektirir.
