# ScanTailor OCR Unified 0.5.0

## Türkçe

Kitap işleme adımları artık **0 Girdi → 12 Önbellek temizliği** sırasıyla tek ekranda. Her aşamanın ayrı çalıştırma düğmesi ve süresi var. Ayarlar ve görselli yardım Türkçe ve İngilizce kullanılabilir.

- Bir sayfa düzeltildiğinde yalnız etkilenen sayfalar yenilenir; diğer sayfaların OCR metni korunur. Her PDF yeni sürüm olarak kaydedilir.
- PDF çıktı klasörü, geçici klasör, önbellek boyutu ve temizleme zamanı seçilebilir.
- CPU Hızlı / Tesseract Fast, OCR'ı sona bırakma, SQLite kitap kuyruğu ve PDF içe aktarma araçları eklendi.

**Windows x64:** AI araçları dahil kullanım için `windows-x64-full-setup.exe` ile biten dosyayı indirin. Daha küçük Tesseract paketi için `windows-x64-minimal-setup.exe` seçin.

**Linux x64:** `linux-x64-full.tar.gz` veya `linux-x64-minimal.tar.gz` arşivini açıp `ScanTailor-OCR-Unified` dosyasını çalıştırın. Menüye eklemek için aynı klasörde `sh install-user.sh` çalıştırın. Ubuntu 22.04 / glibc 2.35 veya yenisi gerekir.

Tam paket Paddle CPU, UVDoc ve AI modellerini içerir. Hafif paket Tesseract, PDF araçları ve görüntü düzenleme içindir. Kaynak kod ZIP'i kurulum dosyası değildir.

## English

The ordered **0 Input → 12 Cache cleanup** workflow now has separate play controls and timings, with settings and illustrated help in Turkish and English. Page edits refresh affected pages while retaining other pages' OCR. PDFs are saved as new versions. This release also adds startup folder settings, CPU Fast / Tesseract Fast, deferred OCR, a SQLite book queue and project cache controls.

Choose **full** for bundled CPU AI tools or **minimal** for the smaller Tesseract package. Windows users run the matching setup executable. Linux users extract the matching archive and run `ScanTailor-OCR-Unified`; `sh install-user.sh` adds a menu entry. Linux requires glibc 2.35 or later. The source ZIP is for development.

## Verification

The four packages were built from commit `fc147ccc0284493b54841453edca74afcdec4901`. [Windows package checks](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/actions/runs/38873814859), [Linux package checks](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/actions/runs/38873925078) and [source regression tests](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/actions/runs/38873819262) passed. Package checks cover CPU/PDF processing, native workflow controls, upgrade from 0.4.5, uninstall and preservation of user projects. Full packages also passed CPU AI checks.

`SHA256SUMS`, per-package SBOMs and `acceptance-results-0.5.0.json` accompany the downloads. Acceptance uses synthetic documents; review representative pages before processing a whole book.
