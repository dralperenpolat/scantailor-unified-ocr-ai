# ScanTailor OCR Unified · 0.6.0

[English](README.md) | **Türkçe**

0.6.0; beş bölümlü sade gezinme, ayrıntılı aşama görünümü, beş PDF sıkıştırma profili, hedef dosya boyutu, örnek sayfa karşılaştırması ve iptal desteği ekler. PDF/OCR güncellemeleri kayıpsız ana dosyadan yapılır; sıkıştırılmış okuma kopyası ayrıca saklanır. Mevcut projeler ve önceki PDF sürümleri korunur.

Kitap fotoğraflarını düzenleyip aranabilir PDF hazırlayan masaüstü uygulaması. Depo adı **scantailor-unified-ocr-ai**; programın adı **ScanTailor OCR Unified**.

[ScanTailor Experimental](https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental) altı görüntü işleme filtresini temel alır. Kitap kuyruğu, OCR, PDF çıktısı, UVDoc eğrilik önerileri ve Paddle ile içerik incelemesi ekler. Belgeler bilgisayarda işlenir. GPU zorunlu değildir.

## Sıkıştırma

PDF / OCR ayarlarından **Kayıpsız, Dengeli, Küçük dosya, Hedef boyut veya Özel** seçin. Hedef boyut kalite sınırları içinde aranır; her PDF için sağlanamaz. Çıktı büyüyecekse kaynakla aynı içerikte bir kopya saklanır. Gelişmiş ayarlardaki yoğun kayıpsız işlem, mevcut Flate akışlarını yeniden sıkıştırır; önceden optimize edilmiş kitaplarda süre maliyeti kazançtan büyük olabilir.

Çalışma merkezi → PDF ve dışa aktarma bölümünde mevcut PDF'leri, sayfa aralığını ve şifreyi seçebilir; üç profili örnek sayfalarda karşılaştırabilirsiniz. Küçük yazı koruması varsayılan olarak açıktır. JPEG ve griye dönüşüm görünümü değiştirebilir; tam PDF'yi inceleyin. Sayfa sayısı, metin konumları, yer imleri, açıklamalar ve görünüm doğrulanmadan yeni sıkıştırılmış dosya yayımlanmaz.

Son PDF'yi aç düğmesi okuma kopyasını açar. Kaynak ana dosya sonraki sayfa düzeltmeleri ve OCR için korunur. Kuyruktaki kitap satırına çift tıklamak da son okuma kopyasını açar.

## Arayüz dili

**Dil / Language → Türkçe / English** menüsünden seçin. Seçim kaydedilir ve program yeniden açıldığında uygulanır. Çalışan kitap kuyruğu durmaz; OCR dili, sayfa ayarları ve PDF seçenekleri değişmez. İlk açılışta Türkçe sistemlerde Türkçe, diğer sistemlerde İngilizce seçilir. OCR dili ayrı olarak OCR motoru / dil paketleri bölümünden belirlenir.

## Kurulum

**Windows x64: [Kurulum dosyasını indir](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/releases).** Dosyayı çalıştırıp programı kurun; ardından Başlat menüsünden açın. Python, Qt, Tesseract, PDF araçları ve CPU AI modelleri dahildir.

**Linux x64: [Linux paketini indir](https://github.com/dralperenpolat/scantailor-unified-ocr-ai/releases).** Arşivi açıp `ScanTailor-OCR-Unified` dosyasını çalıştırın. Menüye eklemek için aynı klasörde `sh install-user.sh` çalıştırın. Python, Qt, Tesseract, PDF araçları ve CPU AI modelleri dahildir.

Sürüm sayfasında yayımlanmış platform ve paket çeşitleri listelenir. Tam (full) paket CPU AI modellerini içerir; minimal paket Tesseract ile başlar ve AI araçlarını isteğe bağlı kurar. GitHub'ın otomatik eklediği “Source code” bağlantıları geliştiriciler içindir. Teknik test kayıtları ve bağımlılık listeleri derleme çıktılarında saklanır.

Windows kurulumu Windows 10 2004 ve sonrası / Windows 11 x64 içindir. Linux paketi Ubuntu 22.04 / glibc 2.35 veya yenisini gerektirir. Diğer ortamlar için [derleme talimatlarına](packaging/BUILD.md) bakın.

Önceki CPU paketinde Ubuntu 22.04 üzerinde 4 GB RAM ile iki sayfalık kabul testi yapılmıştır. Bu sürümün Windows kontrolleri, Linux için yeni bir kabul testi yerine geçmez. Büyük kitaplar için 8 GB ve üstü daha uygundur. Az boş bellekte Paddle daha hafif metin bulma modelini kullanır; PDF'nin görünür görüntüsü bu seçimden etkilenmez. Kitabın büyüklüğüne bağlı geçici disk alanı gerekir.

Tam paketler Türkçe, Almanca ve İngilizce Tesseract Fast ile Türkçe, Almanca, İngilizce ve Fransızca Tesseract Best dillerini içerir. Latin Paddle OCR, UVDoc ve AI inceleme modelleri de dahildir. Ek dil veya isteğe bağlı GPU paketi program içinden indirilir. GPU kurulumu desteklenen NVIDIA/CUDA ortamına bağlıdır; temel kullanım CPU ile hazırdır.

## İlk kitap

İşlem sırası, ayrı çalıştırma düğmeleri ve süreleriyle **Aşamalar** bölümündedir:

**0 Girdi → 1 Yön → 2 Bölme → 3 Geometri → 4 AI geometri → 5 İçerik → 6 AI içerik → 7 Kenarlar → 8 Çıktı → 9 Parmak/leke → 10 PDF → 11 OCR → 12 Önbellek temizliği.**

1. **0 Girdi:** resim, klasör, üst klasör veya PDF seçin. Üst klasör ayrı kitapları kuyruğa ekler; bu ekrandan seçilen PDF doğrudan düzenlemeye açılır.
2. Normal geometri ve içerik ayarlarını kontrol edin. AI aşamalarının ▶ düğmesi öneri hazırlar; önizleyip ayrıca uygulayın. Normal ve AI ekranları aynı sayfa ayarını kullanır; ikisinde de elle düzeltme yapılabilir.
3. **8 Çıktı:** görünümü belirleyin. **9 Parmak/leke:** bu sayfa, seçili sayfalar veya kitap için temizlik önerisini hazırlayıp inceleyin ve uygulayın.
4. **10 PDF:** görünür **PDF çıktı klasörü / Gözat** alanından kayıt yerini seçin. Kâğıt, sığdırma, kenar ve yazı boyutunu ayarlayın. Yazı eşitleme OCR başlatmadan görüntüden ölçülür.
5. **11 OCR:** son PDF’ye aranabilir metin ekleyin. GPU olmayan cihazlar için CPU Hızlı / Tesseract Fast kullanılabilir.
6. **12 Önbellek temizliği:** kategori boyutlarını, saklama seçimlerini ve temizlik zamanını görün. **Ayarlar** ve düğme görselleri içeren aranabilir **Yardım** Türkçe/İngilizcedir.

Önce birkaç örnek sayfayı kontrol edin. Normal aşama ▶ düğmeleri kitabı seçili filtreye kadar işler. AI ▶ seçilen kapsamda öneri hazırlar; Uygula kayıtlı ayarı değiştirir. Çıktı, temizlik, PDF ve OCR ayrı çalışır.

Bir sayfadaki düzeltme yalnız o sayfanın sonraki görüntü ve PDF işlemlerini yeniler. Diğer sayfaların mevcut OCR metni korunur. Önceden OCR’lı PDF yenilendiğinde yalnız değişen sayfaya otomatik OCR eklenir. Uyuşmayan temizlik maskesi yalnız o sayfada atlanır ve inceleme uyarısı gösterilir. Ortak kâğıt boyutu gibi genel ayarlar ilgili bütün sayfaları; bir çift fotoğrafın yönü/bölünmesi iki alt sayfayı etkileyebilir. Her PDF ayrı sürüm olarak kaydedilir.

## PDF’den başlama

Tek kitabı düzenlemek için **0 Girdi → PDF seç ve düzenle**, kuyruğa eklemek için PDF araçları sekmesini kullanın. PDF’yi **Dosya → Aç** ile açmak, ana pencereye tek PDF sürüklemek veya başlatıcıya PDF yolu vermek de aynı hazırlık adımını başlatır. Kaynak PDF değişmez.

**Otomatik**, uygun tarama resmini özgün piksel boyutuyla çıkarır; beyaz kenarları ve sayfa dönüşünü korur. Çıkarma ancak bütün pikseller PDF’nin o ölçekteki görünümüyle aynıysa kabul edilir. Görünür metin, çizim, maske, açıklama veya birden çok resim bulunan sayfalarda sayfanın tamamı seçilen DPI ile görüntüye çevrilir. DPI artırmak kaynakta bulunmayan ayrıntıyı geri getirmez. Eski OCR ayrıca saklanır; geometri değişince yeniden OCR yapılır.

Her PDF sayfası, sırası korunmuş bir giriş resmine dönüşür. Varsayılan **Tek sayfa · yeniden bölme** seçeneğidir. Çift sayfa çekimlerinde **Her PDF sayfasında iki kitap sayfası** seçin. 0 Girdi’den yapılan içe aktarma doğrudan düzenleyicide açılır. Kuyruk araçlarından yapılan içe aktarma kitabı kuyruğa ekler; **Projeleri hazırla** ile ilerleyip bölmeyi kontrol edin. Kuyruk çalışıyorsa boşta olduğunda **Hazır kitabı kuyruğa ekle** düğmesini kullanın. Kesilen içe aktarma doğrulanmış sayfalardan sürer; aynı kitabı tekrar eklemek mevcut düzenlemelerini sıfırlamaz.

İlk içe aktarma klasörü kuyruğun çıktı klasörüne göre seçilir. Görüntüler ve sonraki işlem için boş alanı olan bir disk kullanın.

## Araçlar

- **Geometri:** üst/alt eğrinin uzunluğu, kesişme, kapsam ve yerel ölçek değişimi denetlenir. Geçersiz otomatik model daha güvenli düzeltmeye döner. Elle çizilen modele uyarı verilir; elle düzenleme engellenmez.
- **İçerik:** kitabın yaygın sayfa düzenine göre şüpheli uzantılar incelenir. Paddle metin kutuları ve sayfa düzeni önerileri aşama içinde, tek sayfada veya seçilen sayfalarda kullanılabilir. Öneriler hata yapabilir.
- **Parmak/leke:** Output doldurma alanları ve temkinli kenar temizliği vardır. Parmağın örttüğü yazıyı yeniden üreten bir özellik değildir.
- **Canlı ilerleme:** kitap, aşama ve sayfa görünür; tamamlanan sayfadan düzenleme ekranına geçilebilir. Kaynak / çıktı / PDF etiketine dikkat edin.
- **Paneller:** Görünüm menüsünden kapatılan paneli açın veya yerleşimi sıfırlayın. Aşama ayarları ayrı paneldedir; işlemler solda beş bölümde görünür; tüm aşamalar görünümü ayrıca açılabilir. `Ctrl+S` kaydeder; `Ctrl+0` yalnız görüntüyü ekrana sığdırır, PDF ölçeğini değiştirmez.
- **Kaynak sınırları:** CPU iş parçacığı ve eşzamanlı sayfa sayısı bellekle sınırlanır. OpenCL isteğe bağlıdır. GPU yüzdesine sert üst sınır garanti edilmez.
- **Mevcut PDF:** görüntü sayfalarını içe aktarma, OCR ekleme ve kayıpsız yeniden paketleme araçları vardır. Yeni çıktı oluşturulur; imzalı/etkileşimli PDF özelliklerinin aynen korunacağı varsayılmamalıdır.

## Görüntü ve OCR

OCR, görünür sayfadan ayrı bir tanıma kopyasında çalışır; görünür harfleri yeniden çizmez. Görünüm veya yerleşim değişirken geçerli OCR önbelleği tekrar kullanılır. Son aşamada seçilmiş görüntü CCITT (1 bit) veya Flate (gri/renkli) ile kayıpsız PDF içine yerleştirilir. Yeniden paketleme görüntü piksellerini, sayfa çizimini ve metni denetler.

Geometri, beyaz zemin ve siyah-beyaza dönüşüm görüntüyü bilinçli olarak değiştirir. “Kayıpsız” ifadesi bu işlemlerden **sonraki PDF sıkıştırmasını** anlatır. Yumuşak gri geçişler dosyayı büyütebilir. 9 kat büyütme kaybolmuş ayrıntıyı geri getirmez. Bulanık dipnot veya kapatılmış yazı için doğru metni geri getirme garantisi yoktur.

AI modelleri UVDoc, PaddleOCR ve PP-DocLayout tabanlıdır. Tüm ScanTailor çatallarının bütün özelliklerinin aktarılmış olduğu veya tüm dillerin eşit doğrulukta sınandığı iddia edilmez.

## Veriler

Kaynak fotoğraflar değiştirilmez. Kitap projeleri, önbellekler ve PDF'ler seçilen çalışma/çıktı konumunda tutulur. Ek dil, inceleme ve ayar dosyaları Windows'ta `%LOCALAPPDATA%\scantailor-ocr-unified`, Linux'ta `${XDG_DATA_HOME:-~/.local/share}/scantailor-ocr-unified` altındadır. Programın kaldırılması bu kullanıcı dosyalarını silmez.

**12 Önbellek temizliği** kategori boyutlarını ve silme sonucunu gösterir. Kaynak kopyaları, ara TIFF’ler, OCR metin önbelleği ve önizlemeler için saklama seçilebilir. Temizlik elle, başarılı PDF sonrasında, başarılı OCR sonrasında veya proje kapanırken yapılır. Seçimler projede ve yeni projelerin varsayılanlarında saklanır.

Varsayılan açık **kaydetmeden kapatılan yeni projeyi temizle** seçeneği, geçici girdi kopyalarını, OCR önbelleğini ve analiz dosyalarını kaldırır. Görünür Çıktı TIFF’leri, kaynak kitaplar ve nihai PDF’ler korunur. Kaydet/İptal bunu tetiklemez. Kayıtlı projenin gerekli girdileri ve onaylı düzeltmeleri tutulur. Ara TIFF’leri silmek için doğrulanmış PDF gerekir; sonradan yalnız bir sayfa düzeltilince diğer sayfalar bu PDF’den alınır. Yalnız sahipliği kayıtlı dosyalar temizlenir; başka klasörler ve modeller dahil değildir.

Model/dil indirmeleri resmi depolara bağlanır. Kitap görüntüsü bir bulut OCR hizmetine gönderilmez. Kurulum ve kaynak paketleri kullanıcı kitaplarını veya yerel kuyruklarını içermez.

## Geliştirme ve lisans

Native kaynaklar `native/scantailor-experimental`, Python köprüsü `src/scantailor_ocr_unified` altındadır. Komutlar [packaging/BUILD.md](packaging/BUILD.md) içindedir. `packaging/smoke_cpu.py` kurulu paketin kendi Python'u ile sentetik iki sayfayı işler; kullanıcı kitabına ihtiyaç duymaz.

0.4.4 dağıtım çalışmasında taşınabilir yollar, kullanıcıya yazılabilir model/önbellek konumu, CPU DLL yükleme sırası, Linux bellek sınırı ve paralel native görev hazırlama düzeltildi. Output erişim ihlalinde kayıtlı ayarlarla bir kez tek işçili tekrar yapılır; tekrar başarısız olursa hata saklanır. Bu koruma her olası çöküşün çözüldüğü anlamına gelmez.

Python köprüsü **AGPL-3.0-or-later**, ScanTailor kaynakları **GPL-3.0-or-later** lisanslıdır. Native taban: Experimental commit `67de7e6a8f9c4374b11f0ab23e10078a7afc890a`. Telif ve lisans başlıkları korunmuştur. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), `licenses/`, Python dağıtım lisansları ve model lisansları diğer bileşenleri açıklar.

Bu proje ScanTailor, Experimental, PaddleOCR, UVDoc ve bağımlılıklarının geliştiricilerinin çalışmalarına dayanır. Hata bildirirken görüntüyü ancak paylaşma hakkınız varsa ekleyin; aksi hâlde sorunu yeniden üreten sentetik örnek tercih edin.
