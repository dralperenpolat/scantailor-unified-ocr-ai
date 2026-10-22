# Rebuild the 0.6.2 packages

**English** | [Türkçe](BUILD.md)

Build Windows and Linux on their respective systems. Keep books, queues, private projects, virtual environments and binary models out of Git. The source archive contains application sources, licenses, tests and packaging scripts.

## Shared assets and Python

Use Python 3.12. `python packaging/fetch_assets.py` downloads assets from official repositories using pinned revisions and SHA-256 checksums; `--verify-only` checks existing files without downloading. `assets.json` and `ocr-catalog.json` are included in the source package.

The portable runtime is python-build-standalone **20261013 / CPython 3.12.15**, using `x86_64-pc-windows-msvc-install_only_stripped` on Windows and `x86_64-unknown-linux-gnu-install_only_stripped` on Linux. Download from the [pinned upstream release](https://github.com/astral-sh/python-build-standalone/releases/tag/20261013) and compare its published checksums.

Install into that portable interpreter:

```sh
python -m pip install -r packaging/requirements-cpu.txt
python -m pip install paddlepaddle==3.3.0 --index-url https://www.paddlepaddle.org.cn/packages/stable/cpu/
python -m pip install torch==2.11.0 --index-url https://download.pytorch.org/whl/cpu
python -m pip check
```

Exact package inventories are in `requirements-windows-lock.txt` and `requirements-linux-lock.txt`. Retain the official CPU indexes for Torch and Paddle. These inventories record the release environment; they do not guarantee compatibility with other architectures.

## Windows x64

Install MSYS2 UCRT64 GCC, CMake, Ninja, Qt5 base/tools/svg, Boost, Eigen3, TIFF/JPEG/PNG/zlib and OpenCL headers/loader. Recorded versions are in `native-windows-packages.txt`. The default prefix is `.build-tools/msys64/ucrt64`; override it with `build-windows.ps1 -Prefix ...` and `stage_windows.py --prefix ...`.

Extract portable Python into `output/release043-build/windows-runtime/python` and install the dependencies above using its `python.exe`. The build directory names are retained for incremental builds; `release.json` and package filenames carry the release version.

```powershell
./packaging/build-windows.ps1
python packaging/fetch_assets.py
python packaging/stage_windows.py --destination output/stage-windows
output/stage-windows/runtime/python.exe packaging/smoke_cpu.py --root output/stage-windows --work output/qa-windows --ai
ISCC.exe /DStageDir="C:\absolute\path\stage-windows" /DReleaseDir="C:\absolute\path\release" packaging/windows.iss
```

Inno Setup 7.1 produces one EXE with a per-user installation; administrator privileges are not required. A signing certificate is not included. Publishers can use their own signing process.

## Linux x64

Ubuntu 22.04 is the build baseline. `bash packaging/bootstrap-linux.sh` installs build dependencies and prepares portable Python. Install the Python dependencies using `$HOME/stou-tools/python/bin/python3`, then run:

```sh
python3 packaging/fetch_assets.py
bash packaging/build-linux.sh
$HOME/stou-tools/python/bin/python3 packaging/stage_linux.py --runtime "$HOME/stou-tools/python" --destination "$HOME/stage-linux"
LD_LIBRARY_PATH="$HOME/stage-linux/native/bin" "$HOME/stage-linux/runtime/bin/python3" packaging/smoke_cpu.py --root "$HOME/stage-linux" --work "$HOME/qa-linux" --ai
tar -C "$HOME/stage-linux" -czf scantailor-ocr-unified-0.6.2-linux-x64.tar.gz .
```

The system supplies glibc, the kernel and the display server. Other detected native dependencies and Qt platform plugins are bundled. This binary requires glibc 2.35 or later; musl systems need a separate source build.

## Tests and release

Run the synthetic Python suite with `python -m unittest discover -s tests -p "test_*.py"`. Build native test targets `unified-review-state-test`, `unified-stage-tools-test` and `unified-i18n-test` in the configured CMake directory. Run them with `QT_QPA_PLATFORM=offscreen`; the language test accepts an optional output directory for screenshots.

The language test checks Turkish/English selection and persistence, separate Prepare/Apply controls, translated progress templates, preserved filenames and unchanged OCR language codes. The embedded catalog is `native/scantailor-experimental/src/resources/i18n/unified-en.json`. Translate presentation strings through `unified_i18n::text`; use `display` only for known message fields, never for user titles or paths. Queue state and OCR identifiers remain independent of interface language.

`smoke_cpu.py` uses the installed package's Python and two synthetic pages to check native output, OCR text, source-image hashes and PDF rendering. `--ai` also exercises Paddle CPU, UVDoc and layout analysis. Inspect output PDF/PNG files visually; a model successfully running does not establish accuracy for every real book.

Install the Windows EXE into a clean folder and repeat the smoke test with its installed interpreter. Extract the Linux archive to another location and test its launcher and runtime. Include paths with spaces and Turkish characters. For GUI captures, use `QT_QPA_PLATFORM=offscreen`, `STOU_SETTINGS_NAME=release-test` and `STOU_SMOKE_SCREENSHOT=/absolute/path/gui.png`. `STOU_UI_LANGUAGE=en` or `tr` forces the language for an isolated test. Windows offscreen fonts may require `QT_QPA_FONTDIR=C:\Windows\Fonts`.

Upload only the **Windows full installer EXE and Linux full package TAR.GZ archive** to GitHub Releases. Provide one download link per operating system. Keep minimal packages, SBOMs, `SHA256SUMS` and test results in build artifacts or local archives. Keep the matching source available through the release's Git tag; GitHub adds source ZIP/TAR links automatically. If a separate source archive is needed, generate it locally with `python packaging/source_archive.py --destination ...`. Preserve license notices and keep the source version aligned with the binaries.
