# 0.6.4 paketini yeniden oluşturma

[English](BUILD.en.md) | **Türkçe**

Windows ve Linux kendi sistemlerinde ayrı derlenir. Git deposuna kitap, kuyruk, özel proje, sanal ortam veya ikili model eklemeyin. Kaynak ZIP'i yalnız uygulama kaynakları, lisanslar, testler ve bu betiklerden oluşur.

## Ortak dosyalar

Python 3.12 kullanın. `python packaging/fetch_assets.py` resmi depolardan sabit commit ve SHA-256 ile modelleri alır. `--verify-only` indirmeden denetler. `assets.json` ve `ocr-catalog.json` kaynak paketinin parçasıdır.

Taşınabilir Python sürümü: python-build-standalone **20261013 / CPython 3.12.15**. Windows arşivi `x86_64-pc-windows-msvc-install_only_stripped`, Linux arşivi `x86_64-unknown-linux-gnu-install_only_stripped`. İndirme kaynağı: https://github.com/astral-sh/python-build-standalone/releases/tag/20261013 . Sürümün yayımladığı özetleri indirme sonrası karşılaştırın.

Taşınabilir yorumlayıcıya sırasıyla kurun:

```sh
python -m pip install -r packaging/requirements-cpu.txt
python -m pip install paddlepaddle==3.3.0 --index-url https://www.paddlepaddle.org.cn/packages/stable/cpu/
python -m pip install torch==2.11.0 --index-url https://download.pytorch.org/whl/cpu
python -m pip check
```

Bu sürümde kullanılan tam Python paket listeleri `requirements-windows-lock.txt` ve `requirements-linux-lock.txt` içindedir. Torch CPU ve Paddle için resmi indeksleri koruyun. Kilit listeleri sürüm kaydıdır; farklı mimari için uyumluluk garantisi değildir.

## Windows x64

MSYS2 UCRT64 ile GCC, CMake, Ninja, Qt5 base/tools/svg, Boost, Eigen3, TIFF/JPEG/PNG/zlib ve OpenCL başlık/loader paketlerini kurun. Bu sürümün paket sürümleri `native-windows-packages.txt` içindedir. Varsayılan konum `.build-tools/msys64/ucrt64`; farklı konum `build-windows.ps1 -Prefix ...` ve `stage_windows.py --prefix ...` ile verilebilir.

Taşınabilir Python'u `output/release043-build/windows-runtime/python` içine açın ve yukarıdaki bağımlılıkları bu klasördeki `python.exe` ile kurun. Ardından:

```powershell
./packaging/build-windows.ps1
python packaging/fetch_assets.py
python packaging/stage_windows.py --destination output/stage-windows
output/stage-windows/runtime/python.exe packaging/smoke_cpu.py --root output/stage-windows --work output/qa-windows --ai
ISCC.exe /DStageDir="C:\tam\yol\stage-windows" /DReleaseDir="C:\tam\yol\release" packaging/windows.iss
```

Inno Setup 7.1 ile tek EXE oluşur. Kullanıcıya özel kurulumdur; yönetici yetkisi gerektirmez. Kod imzalama sertifikası bu projeye dahil değildir. Yayıncı kendi imza sürecini kullanabilir.

## Linux x64

Ubuntu 22.04 derleme tabanıdır. `bash packaging/bootstrap-linux.sh` derleme bağımlılıklarını ve taşınabilir Python'u hazırlar. Python bağımlılıklarını `$HOME/stou-tools/python/bin/python3` ile kurun. Sonra:

```sh
python3 packaging/fetch_assets.py
bash packaging/build-linux.sh
$HOME/stou-tools/python/bin/python3 packaging/stage_linux.py --runtime "$HOME/stou-tools/python" --destination "$HOME/stage-linux"
LD_LIBRARY_PATH="$HOME/stage-linux/native/bin" "$HOME/stage-linux/runtime/bin/python3" packaging/smoke_cpu.py --root "$HOME/stage-linux" --work "$HOME/qa-linux" --ai
tar -C "$HOME/stage-linux" -czf scantailor-ocr-unified-0.6.4-linux-x64.tar.gz .
```

glibc, çekirdek ve ekran sunucusu sistemden gelir; diğer tespit edilen native kitaplıklar pakete alınır. Qt platform eklentileri dahildir. glibc 2.35 altı veya musl sistemleri için bu ikiliyi kullanmayın. Kaynaktan ayrı derleme gerekir.

## Kabul testi ve yayın

Native arayüz kontrolleri için derleme dizininde `cmake --build ... --target unified-review-state-test unified-stage-tools-test` çalıştırın. İki test yürütülebilirini `QT_QPA_PLATFORM=offscreen` ile başlatın. `unified-review-state-test` sentetik sayfalarda canlı geometri değişikliği, öneri geçerliliği, seçili adayın uygulanması, aday değiştirme/geri alma ve toplu kapsamı denetler; kullanıcı kitaplarına veya AI model indirmesine ihtiyaç duymaz.

`smoke_cpu.py`, kurulumun kendi Python'uyla iki sentetik sayfa üretir. Native çıktı, OCR metni, kaynak dosya özetleri, PDF çizimi, Paddle CPU, UVDoc CPU ve yerleşim modelini denetler. `--ai` tüm AI bileşenlerini de çalıştırır. Çıktı PDF ve PNG'leri ayrıca gözle inceleyin; başarılı model çalışması her gerçek kitabın doğru işlendiğini göstermez.

Windows EXE'yi boş bir klasöre kurup aynı testi **kurulan** yorumlayıcıyla tekrar çalıştırın. Linux arşivini başka bir konuma açıp başlatıcıyı ve testi yeniden çalıştırın. Boşluk ve Türkçe karakter içeren yolları dahil edin. `QT_QPA_PLATFORM=offscreen`, `STOU_SETTINGS_NAME=release-test` ve `STOU_SMOKE_SCREENSHOT=/tam/yol/gui.png` ile native arayüzün test ekranı kaydedilebilir. Windows offscreen fontları için `QT_QPA_FONTDIR=C:\Windows\Fonts` gerekebilir.

GitHub Releases'e yalnızca **Windows tam kurulum EXE'sini ve Linux tam paket TAR.GZ arşivini** yükleyin. Her işletim sistemi için tek indirme bağlantısı verin. Hafif paketleri, SBOM, SHA256SUMS ve test sonuçlarını derleme çıktılarında veya yerel arşivde saklayın. Kaynak kodu paketlerle eşleşen Git etiketi üzerinden erişilebilir tutun; GitHub kaynak ZIP/TAR bağlantılarını otomatik ekler. Ayrı kaynak arşivi gerekiyorsa `python packaging/source_archive.py --destination ...` ile yerelde üretin. Lisans ve bildirimleri koruyun.

Arayüz dili testi: `unified-i18n-test` hedefini derleyip `QT_QPA_PLATFORM=offscreen` ile çalıştırın. Test dil seçimi, kalıcılık, ayrı play düğmeleri, ilerleme mesajları ve OCR dil kodlarının korunmasını denetler.

Geometri testi: `unified-fast-geometry-test`, bilinen eğrilik ve perspektifle üretilmiş tek ve çift sütunlu sayfalarda gerçek native dönüşümü ölçer. Düz sayfa, az metinli kapak, boş sayfa, dikey kenar açı sınırı ve iptal de denetlenir. `unified-geometry-pilot` kayıtlı inceleme oturumunu kaynakları değiştirmeden tekrar işler; `STOU_PILOT_PAGES=1,2,3` kapsamı, `STOU_PILOT_AI=0` AI olmadan çalışmayı seçer.
