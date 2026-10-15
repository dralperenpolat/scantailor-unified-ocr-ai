# ScanTailor OCR Unified · 0.5.0 development

**English** | [Türkçe](README.tr.md)

The 0.5.0 work adds startup settings (including output and temporary folders), CPU Fast with Tesseract and deferred OCR, a SQLite queue, storage controls and a PDF/review workspace. See the [0.5.0 workflow and build notes](packaging/BUILD-0.5.0.md). The installation instructions below describe the published 0.4.5 release until 0.5.0 package checks finish.

A desktop application for cleaning up book photographs and creating searchable PDFs. It retains the interface and six processing stages of [ScanTailor Experimental](https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental), adding a book queue, OCR, PDF export, UVDoc dewarping suggestions and Paddle-based content review. Documents are processed on your computer. A GPU is optional.

[Download the latest release](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/releases/latest) · [Build instructions](packaging/BUILD.en.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

The repository is named **scantailor-unified-ocr-ai**; the application is called **ScanTailor OCR Unified**.

## Install

| System | Package and use |
| --- | --- |
| Windows x64 | Run `scantailor-ocr-unified-0.4.5-windows-x64-setup.exe`. Python, Qt, CPU OCR and AI models are included. No separate Python installation is needed. Open the app from the Start menu. |
| Linux x64 | Extract `scantailor-ocr-unified-0.4.5-linux-x64.tar.gz` and run `ScanTailor-OCR-Unified` inside the extracted folder. To add a menu entry, run `sh install-user.sh` there. |

The source-code ZIP is for development, not installation. Release assets include SHA-256 checksums and an acceptance-test summary.

The Windows package targets Windows 10 version 2004 or later and Windows 11 x64. The Linux package uses Ubuntu 22.04 x64 as its build/test baseline and requires glibc 2.35 or later. These packages do not cover older Linux systems, Alpine/musl, 32-bit systems, ARM or macOS. Desktop integration on other distributions needs separate testing.

The bundled CPU setup includes Tesseract Best data for Turkish, German, English and French, a Latin Paddle OCR model, UVDoc and layout-analysis models. Additional OCR languages and an optional GPU runtime can be downloaded in the app. GPU setup depends on a supported NVIDIA/CUDA environment. A small acceptance test can run with 4 GB of RAM; 8 GB or more is preferable for large books. Temporary disk-space requirements depend on the book.

## Choose the interface language

Open **Dil / Language → English** or **Türkçe**. Your choice is saved and takes effect when you reopen the app. Changing it does not stop a running book queue or change OCR language codes, page geometry or PDF settings. On first launch, Turkish systems use Turkish and other systems use English.

OCR language is selected separately under **OCR engine / Language packs**. Choosing English for the interface does not change a German book's OCR language.

## Process your first book

1. Under **OCR / Books**, choose **Add book from photo folder**. Use **Add subfolders as separate books** for a collection.
2. Click **Prepare projects**, then **Edit selected book**. Check orientation and whether each photograph contains one or two pages.
3. In **Geometric Distortions**, adjust skew, perspective or curved lines. Compare an AI suggestion with the existing geometry before applying it. Manually edited pages are protected from batch replacement.
4. In **Select Content**, check that footnotes, page numbers and marginal text remain inside the content area. Sorting thumbnails by width or height helps find unusual selections.
5. Use **Margins / PDF page layout** to choose margins and a consistent text scale. Filling A4 separately for each image can produce different text sizes between pages.
6. In **Output**, choose Experimental output, black and white, grayscale or soft black and white. Preview background whitening, small-text sharpening and remaining line correction. **Use for PDF** selects the appearance for export.
7. Choose the OCR language and whether OCR runs during processing, afterwards or is skipped. Create the PDF. Completed pages can be inspected while the rest of the book continues.

Start with a few pages containing small footnotes, curved text and a cover. Inspect a sample before applying settings to the whole book. **Save and update PDF** sends an editing snapshot to the queue; the worker handles it at a page boundary. A paused queue requires **Resume**. Run only one worker for a given queue.

### The separate ▶ buttons

- **Upper ▶ beside Stages:** processes the book up to the selected stage using each page's saved Auto / Manual settings.
- **▶ Prepare beside the method:** analyzes pages in the selected scope and prepares suggestions without applying a correction.
- **▶ Apply beside the candidate:** applies the selected candidate to this page. A different geometry candidate replaces the previous one; undo restores it. **▶ Batch** uses each page's own recommended candidate in the selected scope.

**Selected** and **Applied to this page** are displayed separately. Changing geometry, orientation, splitting or content invalidates the relevant old suggestion. If no reliable content suggestion is available, the interface shows **No suggestion** and disables Apply. Outdated suggestions do not overwrite new manual edits.

## Start from a PDF

Open the **Import PDF** tab and choose **Choose PDF and prepare**. Opening a PDF through **File → Open**, dropping one PDF onto the main window or passing a PDF to the launcher starts the same preparation step. The source PDF stays unchanged.

**Automatic** extracts a suitable scan at its native pixel size, retaining white margins and page rotation. It accepts extraction only when all pixels match the visible PDF page at that scale. Pages with visible text, drawings, masks, annotations or multiple images are rendered as a complete page at the selected DPI instead. Increasing DPI does not recover detail absent from the source. Existing OCR is retained separately; geometry changes receive fresh OCR.

Each PDF page becomes one ordered input image. **Single page · do not split again** is the default. Choose **Two book pages per PDF page** for photographed spreads. When preparation finishes, the book is added to the queue; continue with **Prepare projects** and inspect splitting before export. If the queue is running, use **Add prepared book to queue** once it is idle. Interrupted imports resume from verified pages. Re-importing the same book does not reset its edits.

The initial import folder follows the queue's output folder. Select a drive with enough space for extracted images and subsequent processing.

## Tools and output

- **Geometry review** checks curve length, intersections, coverage and local stretching. Invalid automatic models fall back to safer correction; manual models receive warnings without blocking editing.
- **Content review** compares suspicious extensions with the book's common layout and provides Paddle text/layout suggestions for one page or a selected scope. Suggestions still need visual review.
- **Finger and mark cleanup** provides fill zones and conservative edge cleanup. It does not reconstruct text hidden by a finger.
- **Live progress** shows the book, stage and page and provides access to completed-page previews. Check whether the preview shows the source, processed output or PDF.
- **Panels** can be reopened or reset from **View**. Drag the top edge of the lower panel to resize it. `Ctrl+S` saves; `Ctrl+0` fits the view without changing PDF scale.
- **Resource limits** constrain CPU threads and concurrent pages according to memory. OpenCL is optional. A hard cap on GPU utilization is not guaranteed.
- **Existing PDF tools** import page images, add OCR or repackage losslessly into new files. Preservation of digital signatures or interactive PDF features should not be assumed.

OCR uses a separate recognition copy and does not redraw visible letters. Valid OCR cache entries can be reused after appearance or layout changes. Final page images are embedded losslessly using CCITT for 1-bit images or Flate for grayscale/color. Repackaging checks image pixels, rendered pages and text.

Geometry correction, background cleanup and thresholding deliberately change the image. “Lossless” describes **PDF compression after those steps**. Soft gray transitions can increase file size. Enlarging an image does not recover missing detail, and blurred or obscured text is not guaranteed to be recognized correctly.

## Your data

Source photographs are not modified. Projects, caches and PDFs are kept in the selected work/output location. Additional languages, review data and settings are stored under `%LOCALAPPDATA%\scantailor-ocr-unified` on Windows and `${XDG_DATA_HOME:-~/.local/share}/scantailor-ocr-unified` on Linux. Uninstalling the app does not delete these user files.

Model/language downloads connect to official repositories. Book images are not sent to a cloud OCR service. Installation and source packages contain no user books or local queues.

## Development and licenses

Native sources: `native/scantailor-experimental`. Python bridge: `src/scantailor_ocr_unified`. See the [English](packaging/BUILD.en.md) or [Turkish](packaging/BUILD.md) build instructions. The installed-package smoke test uses two synthetic pages. The language regression test covers persistence, separate play controls, formatted progress, filenames and unchanged OCR codes.

The Python bridge is **AGPL-3.0-or-later**; ScanTailor sources are **GPL-3.0-or-later**. Native base: Experimental commit `67de7e6a8f9c4374b11f0ab23e10078a7afc890a`. Original notices are preserved. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), distribution license files and model notices describe other components.

This project builds on ScanTailor, Experimental, PaddleOCR, UVDoc and their dependencies. It does not claim to incorporate every feature from every fork or to have tested every OCR language equally. Share book images in bug reports only if you have the right to do so; synthetic examples are preferred.
