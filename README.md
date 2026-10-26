# ScanTailor OCR Unified · 0.6.6

**English** | [Türkçe](README.tr.md)

Version 0.6.6 compares balanced upper/lower curve candidates on pages with uneven line lengths, including contents pages. **Force curved-line correction** tries additional strengths and keeps curve mode for manual review when no valid result is found. Saved content boxes no longer need unnecessary recomputation on reopening. In queue book editing, **Save** and Ctrl+S save edits only; **Create or update PDF/OCR** is a separate action. **Back to batch queue** retains the open book and page. Cleanup now recognizes verified PDFs in a separate delivery folder.

Version 0.6.5 bounds full-project checkpoint work for large books. Pause, cancellation and stage completion save settings and their resume cursor together. Projects and intermediates can use a separate fast working disk; completed PDFs are copied and verified in the chosen output folder. AI review uses a separate validated CUDA geometry runtime when installed. After an abrupt process interruption, pages since the last durable checkpoint may need to be processed again.

Version 0.6.4 uses one Windows setup EXE, a stable installation path and one desktop shortcut. Legacy shortcuts are backed up; existing queues, settings and working data are preserved. Setup refuses to replace a running installation. The updated interface can monitor the existing worker and refresh book progress live.

A desktop application for cleaning up book photographs and creating searchable PDFs. It builds on the six image-processing filters of [ScanTailor Experimental](https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental), adding a book queue, OCR, PDF export, UVDoc dewarping suggestions and Paddle-based content review. Documents are processed on your computer. A GPU is optional.

[Download the latest release](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/releases/latest) · [Build instructions](packaging/BUILD.en.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

The repository is named **scantailor-unified-ocr-ai**; the application is called **ScanTailor OCR Unified**.

## Compression

Choose **Lossless, Balanced, Small file, Target size or Custom** in PDF / OCR settings. Target size respects quality limits and cannot always be reached. If compression would increase file size, an identical source copy is retained. Deep lossless repacking is optional: recompressing existing Flate streams can cost substantial time for a small saving on an already optimized book.

Workspace → PDF and export offers page ranges, passwords and three-profile sample comparisons. Small-text protection is enabled by default. JPEG and grayscale conversion may alter appearance; inspect the full PDF. New compressed files are published only after page count, text positions, navigation, annotations and appearance checks.

Open latest PDF opens the reading copy. The lossless master remains available for page updates and OCR. Double-clicking a book in the queue also opens its latest reading copy.

## Install

**Windows x64: [Download the installer](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/releases).** Run it and open the app from the Start menu. Python, Qt, Tesseract, PDF tools and CPU AI models are included.

**Linux x64: [Download the Linux package](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/releases).** Extract it and run `ScanTailor-OCR-Unified`. Run `sh install-user.sh` in the same folder to add a menu entry. Python, Qt, Tesseract, PDF tools and CPU AI models are included.

The release provides one package per platform: the Windows installer and the Linux archive. GitHub automatically adds source-code archive links for developers. Technical test records and dependency inventories remain in the build artifacts.

The Windows installer targets Windows 10 version 2004 or later and Windows 11 x64. The Linux package requires Ubuntu 22.04 / glibc 2.35 or later; see the [build instructions](packaging/BUILD.en.md) for other environments.

Both packages include Tesseract Fast data for Turkish, German and English and Tesseract Best data for Turkish, German, English and French. They also include a Latin Paddle OCR model, UVDoc and layout-analysis models. Additional OCR languages and an optional GPU runtime can be downloaded in the app. GPU setup depends on a supported NVIDIA/CUDA environment. A small acceptance test can run with 4 GB of RAM; 8 GB or more is preferable for large books. Temporary disk-space requirements depend on the book.

## Choose the interface language

Open **Dil / Language → English** or **Türkçe**. Your choice is saved and takes effect when you reopen the app. Changing it does not stop a running book queue or change OCR language codes, page geometry or PDF settings. On first launch, Turkish systems use Turkish and other systems use English.

OCR language is selected separately under **OCR engine / Language packs**. Choosing English for the interface does not change a German book's OCR language.

## Process your first book

The full sequence is displayed in **Stages**, with a separate play button and elapsed time for each operation:

**0 Input → 1 Orientation → 2 Split → 3 Geometry → 4 AI geometry → 5 Content → 6 AI content → 7 Margins → 8 Output → 9 Fingers/stains → 10 PDF → 11 OCR → 12 Cache cleanup.**

1. At **0 Input**, choose images, one folder, a parent folder or a PDF. A parent folder adds separate queue books. PDF import from this screen opens an editable book directly.
2. Check the normal geometry and content stages. The AI stage play button optimizes the selected scope and applies suitable improvements. Use **Prepare** for comparison only and **Apply** for one candidate. AI support can be disabled at startup. Normal and AI screens share the same page settings and allow manual corrections.
3. At **8 Output**, set the visible page appearance. At **9 Fingers/stains**, prepare, inspect and apply edge-cleanup proposals for this page, selected pages or the book.
4. At **10 PDF**, choose the **PDF output folder** with Browse, paper size, fitting, margins and text size. Matching text size measures the image without starting OCR.
5. At **11 OCR**, add searchable text to the finished PDF. CPU Fast / Tesseract Fast is available for devices without a GPU.
6. At **12 Cache cleanup**, inspect category sizes, retention choices and cleanup timing. **Settings** and the illustrated, searchable **Help** are available in English and Turkish.

Start with a few representative pages before applying settings to a whole book. Each normal-stage play processes the book through that filter. AI play optimizes the selected scope; **Prepare** leaves saved settings unchanged for review. Output, cleanup, PDF and OCR have separate actions.

A manual page edit updates only that page's downstream image and PDF content. Unchanged PDF pages retain their OCR. If a previously searchable PDF is refreshed, changed pages receive OCR automatically. A stale cleanup mask is skipped only on the affected page with a review notice. Book-wide paper/layout changes intentionally update all affected pages; rotating or splitting a source spread can affect both halves. Each PDF is a new version.

**Selected** and **Applied to this page** are displayed separately. Changing geometry, orientation, splitting or content invalidates the relevant old suggestion. If no reliable content suggestion is available, the interface shows **No suggestion** and disables Apply. Outdated suggestions do not overwrite new manual edits.

## Start from a PDF

Use **0 Input → Select and edit PDF** for a single editable book, or the PDF tools tab for queue imports. Opening a PDF through **File → Open**, dropping one PDF onto the main window or passing a PDF to the launcher starts the same preparation step. The source PDF stays unchanged.

**Automatic** extracts a suitable scan at its native pixel size, retaining white margins and page rotation. It accepts extraction only when all pixels match the visible PDF page at that scale. Pages with visible text, drawings, masks, annotations or multiple images are rendered as a complete page at the selected DPI instead. Increasing DPI does not recover detail absent from the source. Existing OCR is retained separately; geometry changes receive fresh OCR.

Each PDF page becomes one ordered input image. **Single page · do not split again** is the default. Choose **Two book pages per PDF page** for photographed spreads. Imports from 0 Input open in the editor. Imports from the queue tools are added to the queue; continue with **Prepare projects** and inspect splitting before export. If the queue is running, use **Add prepared book to queue** once it is idle. Interrupted imports resume from verified pages. Re-importing the same book does not reset its edits.

The initial import folder follows the queue's output folder. Select a drive with enough space for extracted images and subsequent processing.

## Tools and output

- **Geometry review** checks curve length, intersections, coverage and local stretching. Invalid automatic models fall back to safer correction; manual models receive warnings without blocking editing.
- **Content review** compares suspicious extensions with the book's common layout and provides Paddle text/layout suggestions for one page or a selected scope. Suggestions still need visual review.
- **Finger and mark cleanup** provides fill zones and conservative edge cleanup. It does not reconstruct text hidden by a finger.
- **Live progress** shows the book, stage and page and provides access to completed-page previews. Check whether the preview shows the source, processed output or PDF.
- **Panels** can be reopened or reset from **View**. Stage settings occupy a separate dock; the full workflow remains visible. `Ctrl+S` saves; `Ctrl+0` fits the view without changing PDF scale.
- **Resource limits** constrain CPU threads and concurrent pages according to memory. OpenCL is optional. A hard cap on GPU utilization is not guaranteed.
- **Existing PDF tools** import page images, add OCR or repackage losslessly into new files. Preservation of digital signatures or interactive PDF features should not be assumed.

OCR uses a separate recognition copy and does not redraw visible letters. Valid OCR cache entries can be reused after appearance or layout changes. Final page images are embedded losslessly using CCITT for 1-bit images or Flate for grayscale/color. Repackaging checks image pixels, rendered pages and text.

Geometry correction, background cleanup and thresholding deliberately change the image. “Lossless” describes **PDF compression after those steps**. Soft gray transitions can increase file size. Enlarging an image does not recover missing detail, and blurred or obscured text is not guaranteed to be recognized correctly.

## Your data

Source photographs are not modified. Projects, caches and PDFs are kept in the selected work/output location. Additional languages, review data and settings are stored under `%LOCALAPPDATA%\scantailor-ocr-unified` on Windows and `${XDG_DATA_HOME:-~/.local/share}/scantailor-ocr-unified` on Linux. Uninstalling the app does not delete these user files.

**12 Cache cleanup** shows actual file sizes and the effect of deleting each category. Keep input copies, intermediate TIFFs, OCR text cache and previews independently. Choose manual cleanup, after successful PDF export, after successful OCR, or on project close. The policy is saved per project and as defaults for new projects.

The enabled-by-default **discard unsaved project** rule removes temporary input copies, OCR cache and analysis files when a new project is closed without saving. Visible Output TIFFs, original books and final PDFs remain. Save/Cancel does not trigger this rule. Saved projects retain required input images and approved corrections. Intermediate TIFF removal requires a verified final PDF; unchanged pages can still be copied from it without rebuilding the entire book. Only registered, owned files are eligible; external folders and models are excluded.

Model/language downloads connect to official repositories. Book images are not sent to a cloud OCR service. Installation and source packages contain no user books or local queues.

## Development and licenses

Native sources: `native/scantailor-experimental`. Python bridge: `src/scantailor_ocr_unified`. See the [English](packaging/BUILD.en.md) or [Turkish](packaging/BUILD.md) build instructions. The installed-package smoke test uses two synthetic pages. The language regression test covers persistence, separate play controls, formatted progress, filenames and unchanged OCR codes.

The Python bridge is **AGPL-3.0-or-later**; ScanTailor sources are **GPL-3.0-or-later**. Native base: Experimental commit `67de7e6a8f9c4374b11f0ab23e10078a7afc890a`. Original notices are preserved. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), distribution license files and model notices describe other components.

This project builds on ScanTailor, Experimental, PaddleOCR, UVDoc and their dependencies. It does not claim to incorporate every feature from every fork or to have tested every OCR language equally. Share book images in bug reports only if you have the right to do so; synthetic examples are preferred.
