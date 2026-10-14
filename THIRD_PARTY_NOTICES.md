# Third-party components · 0.4.3

Original copyright belongs to the respective authors. This application is not an official release of ScanTailor, PaddleOCR, UVDoc or Qt. Upstream notices remain in the source and installed distribution.

| Component | License / source |
| --- | --- |
| ScanTailor Experimental | GPL-3.0-or-later; modified corresponding source is in `native/scantailor-experimental`. https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental at `67de7e6a8f9c4374b11f0ab23e10078a7afc890a`. |
| Unified Python bridge | AGPL-3.0-or-later; `LICENSE`, `src/scantailor_ocr_unified`. |
| Qt 5 | LGPL-3.0/GPL and component-specific notices; dynamically linked, replaceable libraries. https://download.qt.io/archive/qt/5.15/ and https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-qt5-base |
| PyMuPDF / MuPDF | AGPL-3.0; https://github.com/pymupdf/PyMuPDF/tree/1.28.0 and https://mupdf.com/releases/ |
| CPython 3.12.15 / python-build-standalone | PSF and bundled third-party licenses under `runtime`; https://www.python.org/downloads/source/ and https://github.com/astral-sh/python-build-standalone/releases/tag/20261013 |
| PaddlePaddle 3.3.0, PaddleOCR / PaddleX 3.7 | Apache-2.0; https://github.com/PaddlePaddle/Paddle and https://github.com/PaddlePaddle/PaddleOCR and https://github.com/PaddlePaddle/PaddleX |
| Paddle OCR and PP-DocLayout models | Apache-2.0. Official repositories, exact revisions and checksums: `packaging/assets.json`. |
| UVDoc model and model.py | MIT, copyright 2023 Tanguy MAGNE; https://github.com/tanguymagne/UVDoc . Original notice: `resources/UVDoc/LICENSE`. |
| PyTorch 2.11 CPU | BSD-style and third-party notices in its wheel; https://github.com/pytorch/pytorch/tree/v2.11.0 |
| Tesseract / tessdata_best | Apache-2.0; Tesseract is used through PyMuPDF OCR. https://github.com/tesseract-ocr/tesseract and https://github.com/tesseract-ocr/tessdata_best |
| OpenCV | Apache-2.0 and bundled third-party notices; https://github.com/opencv/opencv and https://github.com/opencv/opencv_contrib |
| NumPy / SciPy | BSD and bundled third-party licenses; https://numpy.org/ and https://scipy.org/ |
| Pillow / pypdf / img2pdf | HPND / BSD-3-Clause / LGPL-3.0; notices in the installed distribution metadata. |
| Native supporting libraries | `licenses/native-dependencies`. Windows uses MSYS2 UCRT64 packages; Linux uses Ubuntu 22.04 packages. Build manifests record installed versions. |

Keep the matching application source archive and build instructions available alongside installers. Python wheel `.dist-info` notices, runtime licenses and native library notices are retained. Large model files are downloaded with a pinned manifest instead of being committed into Git. Optional GPU runtimes have their own upstream notices.

Modified application source is supplied in the release source archive. Upstream dependency source locations and installed versions are provided for rebuilding and replacement. Do not remove notices when redistributing. No proprietary OCR engine, user books or models trained on user books are included.
