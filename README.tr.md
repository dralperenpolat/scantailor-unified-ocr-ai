# ScanTailor OCR Unified · 0.4.5

[English](README.md) | **Türkçe**

Kitap fotoğraflarını düzenleyip aranabilir PDF hazırlayan masaüstü uygulaması. Depo adı **scantailor-unified-ocr-ai**; programın adı **ScanTailor OCR Unified**.

[ScanTailor Experimental](https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental) arayüzünü ve altı aşamasını korur. Kitap kuyruğu, OCR, PDF çıktısı, UVDoc eğrilik önerileri ve Paddle ile içerik incelemesi ekler. Belgeler bilgisayarda işlenir. GPU zorunlu değildir.

## Arayüz dili

**Dil / Language → Türkçe / English** menüsünden seçin. Seçim kaydedilir ve program yeniden açıldığında uygulanır. Çalışan kitap kuyruğu durmaz; OCR dili, sayfa ayarları ve PDF seçenekleri değişmez. İlk açılışta Türkçe sistemlerde Türkçe, diğer sistemlerde İngilizce seçilir. OCR dili ayrı olarak OCR motoru / dil paketleri bölümünden belirlenir.

## Kurulum

Dağıtım dosyaları GitHub deposunun **Releases** bölümüne yüklenir. Kaynak kod ZIP'i kurulum dosyası değildir.

| Sistem | Dosya ve kullanım |
| --- | --- |
| Windows x64 | `scantailor-ocr-unified-0.4.5-windows-x64-setup.exe` dosyasını çalıştırın. Python, Qt, CPU OCR ve AI modelleri dahildir; ayrıca Python kurmayın. Başlat menüsünden açın. |
| Linux x64 | `scantailor-ocr-unified-0.4.5-linux-x64.tar.gz` arşivini açın. İçindeki `ScanTailor-OCR-Unified` dosyasını çalıştırın. Menüye eklemek için aynı klasörde `sh install-user.sh` çalıştırın. |

Windows paketi Windows 10 2004 ve sonrası / Windows 11 x64 için hazırlanmıştır. Linux paketi Ubuntu 22.04 x64 üzerinde derlenmiş ve sınanmıştır; glibc 2.35 veya yenisi gerekir. Eski Linux, Alpine/musl, 32 bit, ARM ve macOS bu paketin kapsamı dışındadır. Diğer dağıtımlardaki masaüstü entegrasyonu ayrıca denenmelidir.

4 GB RAM ile Ubuntu 22.04 üzerinde iki sayfalık PDF, Tesseract, Paddle CPU, UVDoc ve yerleşim analizi kabul testi geçti. Büyük kitaplar için 8 GB ve üstü daha uygundur. Az boş bellekte Paddle daha hafif metin bulma modelini kullanır; PDF'nin görünür görüntüsü bu seçimden etkilenmez. Kitabın büyüklüğüne bağlı geçici disk alanı gerekir.

Paket Türkçe, Almanca, İngilizce ve Fransızca Tesseract Best dillerini, Latin Paddle OCR modelini ve AI inceleme modellerini içerir. Ek dil veya isteğe bağlı GPU paketi program içinden indirilir. GPU kurulumu desteklenen NVIDIA/CUDA ortamına bağlıdır; temel kullanım CPU ile hazırdır.

## İlk kitap

1. **OCR / Kitaplar → Fotoğraf klasöründen kitap ekle** ile kitabı ekleyin. Birden fazla kitap için **Alt klasörleri ayrı kitaplar olarak ekle** kullanın.
2. **Projeleri hazırla**, ardından **Seçili kitabı düzenle** ile sayfa yönünü ve tek/çift sayfa ayrımını kontrol edin.
3. **Geometric Distortions** içinde eğiklik, perspektif veya eğri çizgileri ayarlayın. AI önerisini mevcut görünümle karşılaştırın. Elle düzenlenen sayfalar otomatik olarak değiştirilmez.
4. **Select Content** içinde dipnot, sayfa numarası ve kenar yazılarının içeride kaldığını kontrol edin. Küçük resimleri genişlik ve yüksekliğe göre sıralamak hatalı seçimleri bulmayı kolaylaştırır.
5. **Margins / PDF yerleşimi** ile kenarları ve ortak yazı ölçeğini seçin. Her sayfayı ayrı ayrı A4'e doldurmak sayfalar arasında yazı boyutunu değiştirebilir.
6. **Output** içinde görünümü seçin: Experimental çıktısı, siyah-beyaz, gri veya yumuşak siyah-beyaz. Beyaz zemin, ince yazı ve satır düzeltme işlemlerini önizleyin. **PDF için seç** ile dışa aktarma görünümünü belirleyin.
7. OCR dilini ve **işlem sırasında / sonrasında / yapma** seçimini yapıp PDF üretin. Hazır sayfalar işlem tamamlanmadan incelenebilir.

Önce kapak, küçük dipnot ve eğri çekim içeren birkaç sayfayı deneyin. Bir ayarı bütün kitaba uygulamadan önce örneği kontrol edin. **Kaydet ve PDF'yi güncelle**, düzenleme anlık görüntüsünü kuyruğa aktarır. Kuyruk işçisi sayfa sınırında güvenli biçimde işler; duraklatılmışsa **Devam et** gerekir. Aynı kuyruğu iki pencerede eşzamanlı çalıştırmayın.

### Hangi ▶ düğmesi ne yapıyor?

- **Stages yanındaki üst ▶:** kitabı seçili aşamaya kadar, her sayfanın kayıtlı Auto / Manual ayarıyla işler.
- **Yöntemin yanındaki ▶ Hazırla:** seçtiğiniz kapsamdaki sayfaları inceler ve öneri hazırlar. Henüz sayfayı değiştirmez.
- **Adayın yanındaki ▶ Uygula:** seçili adayı bu sayfaya uygular. Başka bir geometri adayı seçilirse öncekinin yerine geçer; geri alma önceki adayı geri getirir. Toplu kapsamda her sayfanın kendi önerilen adayı kullanılır.

**Seçili** ve **Bu sayfada uygulanan** ayrı gösterilir. Sonradan geometri, yön, bölme veya içerik değişirse ilgili eski öneri **yeniden hesaplanmalı** olur. Güvenilir içerik önerisi bulunamazsa **Öneri yok** yazılır ve uygulama düğmesi kapanır. Elle yapılan yeni değişiklikler eski öneriyle ezilmez.

## PDF’den başlama

**PDF’den içe aktar** sekmesinde **PDF seç ve hazırla** düğmesini kullanın. PDF’yi **Dosya → Aç** ile açmak, ana pencereye tek PDF sürüklemek veya başlatıcıya PDF yolu vermek de aynı hazırlık adımını başlatır. Kaynak PDF değişmez.

**Otomatik**, uygun tarama resmini özgün piksel boyutuyla çıkarır; beyaz kenarları ve sayfa dönüşünü korur. Çıkarma ancak bütün pikseller PDF’nin o ölçekteki görünümüyle aynıysa kabul edilir. Görünür metin, çizim, maske, açıklama veya birden çok resim bulunan sayfalarda sayfanın tamamı seçilen DPI ile görüntüye çevrilir. DPI artırmak kaynakta bulunmayan ayrıntıyı geri getirmez. Eski OCR ayrıca saklanır; geometri değişince yeniden OCR yapılır.

Her PDF sayfası, sırası korunmuş bir giriş resmine dönüşür. Varsayılan **Tek sayfa · yeniden bölme** seçeneğidir. Çift sayfa çekimlerinde **Her PDF sayfasında iki kitap sayfası** seçin. Hazırlık bitince kitap kuyruğa eklenir; **Projeleri hazırla** ile ilerleyip bölmeyi kontrol edin. Kuyruk çalışıyorsa boşta olduğunda **Hazır kitabı kuyruğa ekle** düğmesini kullanın. Kesilen içe aktarma doğrulanmış sayfalardan sürer; aynı kitabı tekrar eklemek mevcut düzenlemelerini sıfırlamaz.

İlk içe aktarma klasörü kuyruğun çıktı klasörüne göre seçilir. Görüntüler ve sonraki işlem için boş alanı olan bir disk kullanın.

## Araçlar

- **Geometri:** üst/alt eğrinin uzunluğu, kesişme, kapsam ve yerel ölçek değişimi denetlenir. Geçersiz otomatik model daha güvenli düzeltmeye döner. Elle çizilen modele uyarı verilir; elle düzenleme engellenmez.
- **İçerik:** kitabın yaygın sayfa düzenine göre şüpheli uzantılar incelenir. Paddle metin kutuları ve sayfa düzeni önerileri aşama içinde, tek sayfada veya seçilen sayfalarda kullanılabilir. Öneriler hata yapabilir.
- **Parmak/leke:** Output doldurma alanları ve temkinli kenar temizliği vardır. Parmağın örttüğü yazıyı yeniden üreten bir özellik değildir.
- **Canlı ilerleme:** kitap, aşama ve sayfa görünür; tamamlanan sayfadan düzenleme ekranına geçilebilir. Kaynak / çıktı / PDF etiketine dikkat edin.
- **Paneller:** Görünüm menüsünden kapatılan paneli açın veya yerleşimi sıfırlayın. Alt panelin üst kenarı sürüklenebilir; dar alanlarda kaydırma vardır. `Ctrl+S` kaydeder; `Ctrl+0` yalnız görüntüyü ekrana sığdırır, PDF ölçeğini değiştirmez.
- **Kaynak sınırları:** CPU iş parçacığı ve eşzamanlı sayfa sayısı bellekle sınırlanır. OpenCL isteğe bağlıdır. GPU yüzdesine sert üst sınır garanti edilmez.
- **Mevcut PDF:** görüntü sayfalarını içe aktarma, OCR ekleme ve kayıpsız yeniden paketleme araçları vardır. Yeni çıktı oluşturulur; imzalı/etkileşimli PDF özelliklerinin aynen korunacağı varsayılmamalıdır.

## Görüntü ve OCR

OCR, görünür sayfadan ayrı bir tanıma kopyasında çalışır; görünür harfleri yeniden çizmez. Görünüm veya yerleşim değişirken geçerli OCR önbelleği tekrar kullanılır. Son aşamada seçilmiş görüntü CCITT (1 bit) veya Flate (gri/renkli) ile kayıpsız PDF içine yerleştirilir. Yeniden paketleme görüntü piksellerini, sayfa çizimini ve metni denetler.

Geometri, beyaz zemin ve siyah-beyaza dönüşüm görüntüyü bilinçli olarak değiştirir. “Kayıpsız” ifadesi bu işlemlerden **sonraki PDF sıkıştırmasını** anlatır. Yumuşak gri geçişler dosyayı büyütebilir. 9 kat büyütme kaybolmuş ayrıntıyı geri getirmez. Bulanık dipnot veya kapatılmış yazı için doğru metni geri getirme garantisi yoktur.

AI modelleri UVDoc, PaddleOCR ve PP-DocLayout tabanlıdır. Tüm ScanTailor çatallarının bütün özelliklerinin aktarılmış olduğu veya tüm dillerin eşit doğrulukta sınandığı iddia edilmez.

## Veriler

Kaynak fotoğraflar değiştirilmez. Kitap projeleri, önbellekler ve PDF'ler seçilen çalışma/çıktı konumunda tutulur. Ek dil, inceleme ve ayar dosyaları Windows'ta `%LOCALAPPDATA%\scantailor-ocr-unified`, Linux'ta `${XDG_DATA_HOME:-~/.local/share}/scantailor-ocr-unified` altındadır. Programın kaldırılması bu kullanıcı dosyalarını silmez.

Model/dil indirmeleri resmi depolara bağlanır. Kitap görüntüsü bir bulut OCR hizmetine gönderilmez. Kurulum ve kaynak paketleri kullanıcı kitaplarını veya yerel kuyruklarını içermez.

## Geliştirme ve lisans

Native kaynaklar `native/scantailor-experimental`, Python köprüsü `src/scantailor_ocr_unified` altındadır. Komutlar [packaging/BUILD.md](packaging/BUILD.md) içindedir. `packaging/smoke_cpu.py` kurulu paketin kendi Python'u ile sentetik iki sayfayı işler; kullanıcı kitabına ihtiyaç duymaz.

0.4.4 dağıtım çalışmasında taşınabilir yollar, kullanıcıya yazılabilir model/önbellek konumu, CPU DLL yükleme sırası, Linux bellek sınırı ve paralel native görev hazırlama düzeltildi. Output erişim ihlalinde kayıtlı ayarlarla bir kez tek işçili tekrar yapılır; tekrar başarısız olursa hata saklanır. Bu koruma her olası çöküşün çözüldüğü anlamına gelmez.

Python köprüsü **AGPL-3.0-or-later**, ScanTailor kaynakları **GPL-3.0-or-later** lisanslıdır. Native taban: Experimental commit `67de7e6a8f9c4374b11f0ab23e10078a7afc890a`. Telif ve lisans başlıkları korunmuştur. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), `licenses/`, Python dağıtım lisansları ve model lisansları diğer bileşenleri açıklar.

Bu proje ScanTailor, Experimental, PaddleOCR, UVDoc ve bağımlılıklarının geliştiricilerinin çalışmalarına dayanır. Hata bildirirken görüntüyü ancak paylaşma hakkınız varsa ekleyin; aksi hâlde sorunu yeniden üreten sentetik örnek tercih edin.
