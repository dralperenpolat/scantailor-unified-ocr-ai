// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedRuntime.h"
#include "UnifiedPanel.h"
#include "UnifiedResources.h"
#include "UnifiedOcrOptions.h"
#include "UnifiedWheelGuard.h"
#include "UnifiedQueueSelection.h"
#include "UnifiedQueueEditing.h"
#include "UnifiedPdfToolsDialog.h"
#include <QCryptographicHash>
#include "BasicImageView.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QHeaderView>
#include <QTimer>
#include <QThread>
#include <QSettings>
#include <QMessageBox>
#include <QDateTime>
#include <QUuid>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <QScrollArea>
#include <QImageReader>
#include <QProcessEnvironment>
#include <QSignalBlocker>
#include <QTabBar>
#include <limits>
namespace {
QString trU(char const* s) { return QString::fromUtf8(s); }
QString uid() {return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QString queueReadPath(QString const& path){
    QFileInfo primary(path),pending(path+".pending.json");
    return pending.exists()&&(!primary.exists()||pending.lastModified()>primary.lastModified())?pending.absoluteFilePath():path;
}
void writeJson(QString const& path,QJsonObject const& value) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path); if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(value).toJson());f.commit();}
}
}
UnifiedPanel::UnifiedPanel(QWidget* parent):QWidget(parent) {
    m_root=qEnvironmentVariable("STOU_ROOT");
    if(m_root.isEmpty()) {
        QDir d(QCoreApplication::applicationDirPath());
        for(int i=0;i<5;++i){if(d.exists("local-tools.json")){m_root=d.path();break;}d.cdUp();}
    }
    if(m_root.isEmpty()) m_root=QCoreApplication::applicationDirPath();
    m_queuePath=qEnvironmentVariable("STOU_QUEUE_PATH",unified_runtime::data(m_root)+"/projects/Native_Kitaplar/kuyruk.json");
    m_controlPath=unified_runtime::data(m_root)+"/projects/Native_Kitaplar/control-"+uid()+".json";
    m_output=unified_runtime::data(m_root)+"/output/Native_Kitaplar";
    m_previewRefresh=new QTimer(this);m_previewRefresh->setSingleShot(true);m_previewRefresh->setInterval(300);
    connect(m_previewRefresh,&QTimer::timeout,this,&UnifiedPanel::refreshPreview);
    auto outer=new QVBoxLayout(this);
    outer->setContentsMargins(5,3,5,3);
    auto sizing=new QHBoxLayout;outer->addLayout(sizing);
    sizing->addWidget(new QLabel(trU("Panel yüksekliği ↕")));
    m_panelHeight=new QSlider(Qt::Horizontal);m_panelHeight->setRange(140,1800);m_panelHeight->setValue(QSettings().value("unified/panel_height",370).toInt());m_panelHeight->setMaximumWidth(260);
    m_panelHeight->setToolTip(trU("Alt panelin yüksekliğini ayarlar. Üst ayırıcıyı sürükleyerek de değiştirebilirsiniz."));sizing->addWidget(m_panelHeight);
    auto sizingNote=new QLabel(trU("Üst kenarı sürükle · İçerik sığmazsa panel içinde kaydır"));sizingNote->setWordWrap(true);sizingNote->setMinimumWidth(80);sizing->addWidget(sizingNote,1);
    connect(m_panelHeight,&QSlider::valueChanged,this,&UnifiedPanel::panelHeightRequested);
    auto tabs=new QTabWidget;tabs->setObjectName("unifiedToolTabs");tabs->setUsesScrollButtons(true);tabs->setElideMode(Qt::ElideRight);tabs->tabBar()->setExpanding(false);tabs->setMinimumWidth(100);outer->addWidget(tabs);
    auto jobs=new QWidget;auto jl=new QVBoxLayout(jobs);tabs->addTab(jobs,trU("İşler ve canlı ilerleme"));
    m_editorContext=new QLabel(trU("Düzenleyicide açık proje: yok"));m_editorContext->setObjectName("unifiedEditorContext");m_editorContext->setWordWrap(true);jl->addWidget(m_editorContext);
    m_queueContext=new QLabel(trU("Kuyrukta seçili kitap: yok"));m_queueContext->setObjectName("unifiedQueueContext");m_queueContext->setWordWrap(true);jl->addWidget(m_queueContext);
    auto buttons=new QGridLayout;jl->addLayout(buttons);int buttonIndex=0;
    auto button=[&](char const* text,char const* help,auto action) {auto b=new QPushButton(trU(text));b->setToolTip(trU(help));buttons->addWidget(b,buttonIndex/3,buttonIndex%3);++buttonIndex;connect(b,&QPushButton::clicked,action);return b;};
    m_capture=button("Açık projeyi kuyruğa ekle","Düzenleyicide açık projenin şu anki ayarlarıyla bir kopyasını kuyruğa ekler. Aşağıda seçilen kitabı kullanmaz.",[this]{if(busy()){m_status->setText(trU("Yeni kitap eklemek için önce kuyruğu durdurun. Mevcut kitabı düzeltmek için Kaydet ve PDF'yi güncelle kullanılabilir."));return;}emit captureRequested();});
    m_capture->setObjectName("unifiedCaptureProject");m_capture->setEnabled(false);
    button("Fotoğraf klasöründen kitap ekle","JPG / TIFF / PNG fotoğrafları içeren tek klasörü yeni kitap olarak ekler. Daha önce kaydedilmiş ScanTailor ayarlarını yüklemez.",[this]{if(busy()){m_status->setText(trU("Yeni kitap eklemek için önce kuyruğu durdurun."));return;}auto f=QFileDialog::getExistingDirectory(this,trU("Fotoğrafların bulunduğu kitap klasörü"));if(!f.isEmpty())addFolder(f);});
    button("Alt klasörleri ayrı kitap olarak ekle","Seçtiğiniz üst klasördeki fotoğraf içeren alt klasörlerin her birini ayrı kitap olarak kuyruğa ekler.",[this]{
        if(busy())return;auto f=QFileDialog::getExistingDirectory(this,trU("Alt klasörlerin her biri ayrı kitap"));if(f.isEmpty())return;
        QDirIterator it(f,QDir::Dirs|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
        while(it.hasNext()) {QString p=it.next();QString relative=QDir(f).relativeFilePath(p).toLower();
            auto parts=relative.split('/');bool skip=false;for(auto part:parts)if(part=="out"||part=="output"||part=="cache"||part.startsWith('.'))skip=true;
            if(!skip && !QFileInfo(p).isSymLink())addFolder(p);
        }
    });
    button("Kayıtlı ScanTailor projesini ekle","Diskten bir .ScanTailor dosyası seçer; kayıtlı bölme, geometri, içerik ve kenar ayarlarıyla kuyruğa ekler.",[this]{if(busy()){m_status->setText(trU("Yeni kitap eklemek için önce kuyruğu durdurun."));return;}for(auto p:QFileDialog::getOpenFileNames(this,trU("Kayıtlı ScanTailor projeleri"),{},"ScanTailor (*.ScanTailor)"))addProject(QFileInfo(p).baseName(),p,{});});
    button("PDF çıktı klasörünü seç","Yeni PDF dosyalarının kaydedileceği klasör. Kaynak fotoğrafları değiştirmez.",[this]{if(busy())return;auto f=QFileDialog::getExistingDirectory(this,trU("Kitap çıktıları"),m_output);if(!f.isEmpty()){m_output=f;saveQueue();m_status->setText(f);}});
    button("Mevcut PDF’leri işle","PDF’den sayfaları ve metni çıkar; düzeltme kuyruğuna ekle veya görüntüyü değiştirmeden sıkıştır.",[this]{
        openPdfTools();
    })->setObjectName("unifiedExistingPdfTools");
    m_books=new QTreeWidget;m_books->setHeaderLabels({trU("Kitap"),trU("Durum"),trU("Son işlem / hata")});m_books->setRootIsDecorated(false);m_books->setMaximumHeight(145);
    m_books->setObjectName("unifiedBooks");
    m_books->setToolTip(trU("Satırı seç: kitabın görüntülerini üstte incele. İşlem ve açık projedeki ayarlar korunur."));
    connect(m_books,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item,QTreeWidgetItem*){showBookSelection(item);});
    connect(m_books,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem* item,int){showBookSelection(item);});
    m_books->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);m_books->header()->setStretchLastSection(true);jl->addWidget(m_books);
    auto row=new QHBoxLayout;jl->addLayout(row);
    m_prepare=new QPushButton(trU("Projeleri hazırla"));m_start=new QPushButton(trU("PDF oluştur / devam et"));m_pause=new QPushButton(trU("Duraklat"));
    auto stopButton=new QPushButton(trU("Durdur"));auto edit=new QPushButton(trU("Seçili kitabı düzenle"));auto remove=new QPushButton(trU("Kuyruktan çıkar"));
    for(auto b:{m_prepare,m_start,m_pause,stopButton,edit,remove})row->addWidget(b);
    connect(m_prepare,&QPushButton::clicked,[this]{start(true);});connect(m_start,&QPushButton::clicked,[this]{start(false);});
    connect(m_pause,&QPushButton::clicked,[this]{if(!busy())return;QFile f(m_controlPath);QString state;if(f.open(QIODevice::ReadOnly)){state=QJsonDocument::fromJson(f.readAll()).object().value("state").toString();f.close();}if(state=="cancel")return;m_paused=state!="pause";control(m_paused?"pause":"run");m_pause->setText(trU(m_paused?"Devam et":"Duraklat"));m_status->setText(trU(m_paused?"Duraklatma istendi; mevcut sayfa bitince bekleyecek.":"Devam ediyor"));});
    connect(stopButton,&QPushButton::clicked,this,&UnifiedPanel::stop);
    connect(edit,&QPushButton::clicked,[this]{editPreview();});
    connect(remove,&QPushButton::clicked,[this]{if(busy()||!m_books->currentItem())return;m_jobs.removeAt(m_books->currentItem()->data(0,Qt::UserRole).toInt());saveQueue();refreshBooks();});
    auto placement=new QPushButton(trU("Sayfa yerleşimini incele"));row->addWidget(placement);
    connect(placement,&QPushButton::clicked,[this]{
        auto selected=m_books->currentItem();if(!selected)return;
        auto job=m_jobs.at(selected->data(0,Qt::UserRole).toInt()).toObject();QFile file(job.value("work").toString()+"/layout-plan.json");
        if(!file.open(QIODevice::ReadOnly)){m_status->setText(trU("Bu kitap için henüz PDF yerleşimi hesaplanmadı."));return;}
        auto plan=QJsonDocument::fromJson(file.readAll()).object();auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->resize(1200,500);dialog->setWindowTitle(job.value("title").toString()+trU(" · son hesaplanan PDF yerleşimi"));
        auto layout=new QVBoxLayout(dialog);layout->addWidget(new QLabel(trU("Bu değerler son hesaplanan çıktıya aittir. Yeni ayarlar, bir sonraki PDF oluşturulduğunda uygulanır.")));
        auto table=new QTreeWidget;table->setRootIsDecorated(false);table->setHeaderLabels({trU("Sayfa"),trU("Yerleşim"),trU("PDF mm"),trU("Görsel mm"),trU("Yazı yüksekliği mm"),trU("Kontrol / istisna")});layout->addWidget(table);
        for(auto value:plan.value("pages").toArray()){auto p=value.toObject();auto size=[](QJsonArray a){return a.size()==2?QString("%1 × %2").arg(a.at(0).toDouble(),0,'f',1).arg(a.at(1).toDouble(),0,'f',1):QString();};
            new QTreeWidgetItem(table,{QString::number(p.value("page").toInt())+" · "+QFileInfo(p.value("source").toString()).fileName(),p.value("paper").toString(),size(p.value("page_mm").toArray()),size(p.value("image_mm").toArray()),p.value("body_mm").isNull()?trU("Ölçülemedi"):QString::number(p.value("body_mm").toDouble(),'f',2),p.value("warning").toString()});}
        for(int i=0;i<5;++i)table->resizeColumnToContents(i);dialog->show();
    });
    m_status=new QLabel(trU("Hazır · Experimental'in mevcut sayfa ayarları korunur."));m_status->setWordWrap(true);jl->addWidget(m_status);
    m_revisionStatus=new QLabel;m_revisionStatus->setObjectName("revisionStatus");m_revisionStatus->setWordWrap(true);m_revisionStatus->hide();jl->addWidget(m_revisionStatus);
    auto revisionTimer=new QTimer(this);revisionTimer->setInterval(1000);connect(revisionTimer,&QTimer::timeout,this,&UnifiedPanel::updateRevisionStatus);revisionTimer->start();
    m_progress=new QProgressBar;m_progress->setValue(0);jl->addWidget(m_progress);
    m_pages=new QTreeWidget;m_pages->setHeaderLabels({trU("Kitap / sayfa"),trU("Aşama"),trU("Sonuç · seç: üstte / çift tık: ayrı aç")});m_pages->setRootIsDecorated(false);m_pages->setMaximumHeight(115);jl->addWidget(m_pages);
    m_pages->setObjectName("unifiedCompletedPages");
    connect(m_pages,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item,QTreeWidgetItem*){showPageSelection(item);});
    connect(m_pages,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem* item,int){showPageSelection(item);});
    connect(m_pages,&QTreeWidget::itemDoubleClicked,[this](QTreeWidgetItem* item,int){preview(item->data(0,Qt::UserRole).toString());});

    auto pdf=new QWidget;auto form=new QFormLayout(pdf);form->setRowWrapPolicy(QFormLayout::WrapLongRows);tabs->addTab(pdf,trU("PDF / OCR ayarları"));
    auto languageNote=new QLabel(trU("Motor, cihaz ve OCR dillerini ‘OCR motoru / dil paketleri’ sekmesinden seçin."));languageNote->setWordWrap(true);form->addRow(languageNote);
    m_ocrTiming=new QComboBox;m_ocrTiming->setObjectName("unifiedOcrTiming");m_ocrTiming->addItem(trU("Çıktıyla birlikte OCR yap"),"during");m_ocrTiming->addItem(trU("Görüntüler bitince OCR yap"),"after");m_ocrTiming->addItem(trU("OCR yapma · yalnız görüntülü PDF"),"none");m_ocrTiming->setCurrentIndex(qMax(0,m_ocrTiming->findData(QSettings().value("unified/ocr_timing","during").toString())));form->addRow(trU("OCR ne zaman?"),m_ocrTiming);
    connect(m_ocrTiming,qOverload<int>(&QComboBox::currentIndexChanged),[this]{QSettings().setValue("unified/ocr_timing",ocrTiming());emit ocrTimingChanged(ocrTiming());});
    m_outputFinish=QJsonDocument::fromJson(QSettings().value("unified/output_finish").toByteArray()).object();
    m_appearance=new QComboBox;m_appearance->addItem(trU("Experimental çıktısını koru"),"native");m_appearance->addItem(trU("Yumuşak siyah-beyaz · gri harf kenarları"),"soft");m_appearance->addItem(trU("Yerel yumuşak S/B · soluk çizgiler / gölge"),"soft_local");m_appearance->addItem(trU("Gri ton · eşikleme yok"),"gray");form->addRow(trU("PDF görünümü"),m_appearance);
    m_cleanup=new QCheckBox(trU("PDF’de metinden ayrık parmak / kitap kenarlarını temizle"));m_cleanup->setChecked(QSettings().value("unified/pdf_cleanup",true).toBool());form->addRow(m_cleanup);
    auto cleanupNote=new QLabel(trU("Gri / yumuşak PDF’de uygulanır. Tablo, resim ve yazıyla çakışan belirsiz bölgeler korunup kontrol için işaretlenir. Sonucu tamamlanan sayfanın PDF önizlemesinde görürsünüz."));cleanupNote->setWordWrap(true);form->addRow(cleanupNote);
    m_paper=new QComboBox;m_paper->addItem(trU("A4 · kitap boyunca aynı piksel ölçeği"),"a4");m_paper->addItem(trU("A4 · yazı yüksekliğini eşitle (kontrollü)"),"a4_text");m_paper->addItem(trU("A4'ü doldur · her sayfayı ayrı sığdır"),"a4_fill");m_paper->addItem(trU("Experimental boyutu · 300 DPI"),"native");form->addRow(trU("Sayfa boyutu"),m_paper);
    m_margin=new QDoubleSpinBox;m_margin->setRange(0,70);m_margin->setSuffix(" mm");m_margin->setValue(10);form->addRow(trU("A4 dış kenar payı · tüm sayfalar"),m_margin);
    m_fitContent=new QCheckBox(trU("Fazla beyaz kenarları çıkar · basılı alanı A4’e yerleştir"));m_fitContent->setObjectName("unifiedFitContent");m_fitContent->setChecked(QSettings().value("unified/pdf_content_fit",true).toBool());form->addRow(m_fitContent);
    auto fitNote=new QLabel(trU("Yazı eşitleme / A4’ü doldurma seçeneklerinde uygulanır. Dipnotlar ve tek başına duran sayfa numaraları korunur. Kısa sayfalar gereksiz büyütülmez. OCR ve görüntü aynı ölçekte taşınır; yeniden OCR gerekmez."));fitNote->setWordWrap(true);form->addRow(fitNote);
    m_targetText=new QDoubleSpinBox;m_targetText->setRange(0,5);m_targetText->setSingleStep(.1);m_targetText->setDecimals(2);m_targetText->setSpecialValueText(trU("Otomatik · kitap ölçümünden"));m_targetText->setSuffix(" mm");form->addRow(trU("Yazı eşitlemede küçük harf yüksekliği"),m_targetText);
    auto placementNote=new QLabel(trU("A4'ü doldur: oran korunur, yazı boyu sayfalar arasında değişebilir.\nYazıyı eşitle: gri kopyadaki gövde harfleri ölçülür. A4'e sığmayan / güvenilir ölçülemeyen sayfalar ayrıca işaretlenir.\nEkrana sığdır, bu PDF yerleşimlerinden bağımsızdır."));placementNote->setWordWrap(true);form->addRow(placementNote);
    m_split=new QComboBox;m_split->addItem(trU("Otomatik"),"auto");m_split->addItem(trU("Çift sayfa fotoğrafı"),"2");m_split->addItem(trU("Tek sayfa"),"1");form->addRow(trU("Yeni klasörlerde sayfa bölme"),m_split);
    m_autoWarp=new QCheckBox(trU("Yeni sayfalarda eğrilik düzelt · bozuk modelde güvenli eğikliğe dön"));m_autoWarp->setChecked(true);form->addRow(m_autoWarp);
    auto scope=new QLabel(trU("Kapsam: bu PDF ayarları kuyruktaki bütün kitapların bütün sayfalarına uygulanır.\nDeğişiklik yalnız sonraki oluşturmayı etkiler; önceki PDF dosyaları değişmez.\nMevcut projelerin elle ayarlanmış geometri ve alanları korunur."));scope->setWordWrap(true);form->addRow(scope);
    auto note=new QLabel(trU("Mevcut .ScanTailor projesinin bölme, geometri, içerik, kenar ve çıktı ayarları kullanılır.\nOCR, eşiklemeden önce kaydedilen gri kopyadan yapılır. Görünür sayfa ayrı tutulur.\nRenk modu sayfa geometrisini değiştirmez. Yumuşak çıktı, 1 bit çıktıya göre daha büyük olabilir.\nParmak: Output → Fill Zones / Parmak temizle. Metin üstünü beyazla silmeyin."));note->setWordWrap(true);form->addRow(note);

    m_ocr=new UnifiedOcrOptions(m_root);tabs->addTab(m_ocr,trU("OCR motoru / dil paketleri"));
    auto resources=new QWidget;auto rf=new QFormLayout(resources);rf->setRowWrapPolicy(QFormLayout::WrapLongRows);tabs->addTab(resources,trU("CPU / GPU ve yük sınırları"));
    QSettings settings;
    m_threads=new QSpinBox;m_threads->setRange(1,qMax(1,QThread::idealThreadCount()));m_threads->setValue(settings.value("settings/batch_processing_threads",qMin(4,QThread::idealThreadCount())).toInt());rf->addRow(trU("CPU iş parçacığı / eşzamanlı sayfa"),m_threads);
    m_cpu=new QSpinBox;m_cpu->setRange(5,100);m_cpu->setSuffix(" %");m_cpu->setValue(settings.value("unified/cpu_limit",75).toInt());rf->addRow(trU("Toplam CPU üst sınırı · 100 = kapalı"),m_cpu);
    m_singleGpu=new QCheckBox(trU("OpenCL kullanırken tek sayfa işle · GPU belleğini sınırla"));m_singleGpu->setChecked(settings.value("unified/single_gpu",true).toBool());rf->addRow(m_singleGpu);
    m_delay=new QSpinBox;m_delay->setRange(0,10000);m_delay->setSuffix(" ms");m_delay->setValue(settings.value("unified/page_delay_ms",0).toInt());rf->addRow(trU("Sayfalar arasında ek bekleme"),m_delay);
    m_numbers=new QCheckBox(trU("Otomatik içerikte küçük kenar / sayfa numaralarını koru"));m_numbers->setChecked(settings.value("unified/protect_numbers",true).toBool());rf->addRow(m_numbers);
    auto apply=new QPushButton(trU("Yük sınırlarını uygula"));rf->addRow(apply);connect(apply,&QPushButton::clicked,this,&UnifiedPanel::applyResources);
    auto gpuSettings=new QPushButton(trU("Experimental OpenCL / OpenGL cihaz ayarları"));rf->addRow(gpuSettings);connect(gpuSettings,&QPushButton::clicked,this,&UnifiedPanel::settingsRequested);
    m_limits=new QLabel;m_limits->setWordWrap(true);m_usage=new QLabel;m_gpuUsage=new QLabel(trU("GPU bilgisi sorgulanıyor…"));rf->addRow(m_limits);rf->addRow(m_usage);rf->addRow(m_gpuUsage);
    auto gpuNote=new QLabel(trU("GPU için yüzde üst sınırı uygulanmaz. Tek eşzamanlı sayfa ve sayfalar arası bekleme yükü azaltır.\nNumara koruma, yeni otomatik içerik seçimlerine uygulanır; elle çizilmiş kutular değişmez."));gpuNote->setWordWrap(true);rf->addRow(gpuNote);
    m_log=new QPlainTextEdit;m_log->setReadOnly(true);m_log->setMaximumBlockCount(2000);tabs->addTab(m_log,trU("İşlem ayrıntıları"));
    connect(&m_worker,&QProcess::readyReadStandardOutput,this,&UnifiedPanel::receive);
    connect(&m_worker,&QProcess::readyReadStandardError,[this]{auto line=QString::fromUtf8(m_worker.readAllStandardError());m_log->appendPlainText(line);});
    connect(&m_worker,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),[this](int code,QProcess::ExitStatus){receive();m_start->setEnabled(true);m_prepare->setEnabled(true);m_pause->setText(trU("Duraklat"));loadQueue();refreshBooks();refreshPreview();m_status->setText(trU(code==0?"İşlem tamamlandı. Sonuçları ve kontrol gereken kitapları inceleyebilirsiniz.":code==2?"Durduruldu. Tamamlanan sayfalar saklandı; devam edilebilir.":"İşlem hatayla bitti. Ayrıntılar sekmesini kontrol edin."));});
    connect(&m_worker,&QProcess::errorOccurred,[this](QProcess::ProcessError){m_status->setText(m_worker.errorString());m_start->setEnabled(true);m_prepare->setEnabled(true);});
    connect(&m_gpu,&QProcess::readyReadStandardOutput,[this]{m_gpuUsage->setText(trU("GPU toplam kullanımı / VRAM: ")+QString::fromUtf8(m_gpu.readAllStandardOutput()).trimmed());});
    connect(&m_gpu,&QProcess::errorOccurred,[this](QProcess::ProcessError){m_gpuUsage->setText(trU("GPU sayacı alınamadı; OpenCL cihazı Experimental ayarlarından seçilebilir."));});
    auto gpuProgram=QStandardPaths::findExecutable("nvidia-smi");if(gpuProgram.isEmpty())m_gpuUsage->setText(trU("NVIDIA GPU bulunmadı · CPU ile çalışabilirsiniz."));
    auto timer=new QTimer(this);timer->setInterval(3000);connect(timer,&QTimer::timeout,[this,gpuProgram]{m_usage->setText(unifiedUsage());if(!gpuProgram.isEmpty()&&m_gpu.state()==QProcess::NotRunning)m_gpu.start(gpuProgram,{"--query-gpu=utilization.gpu,memory.used,memory.total","--format=csv,noheader"});});timer->start();
    // Keep the main page usable when settings are opened on smaller screens.
    for(int i=0;i<tabs->count();++i){auto content=tabs->widget(i);QString title=tabs->tabText(i);tabs->removeTab(i);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(content);installUnifiedWheelGuard(scroll,content);tabs->insertTab(i,scroll,title);}
    tabs->setCurrentIndex(0);
    m_appearance->setCurrentIndex(qMax(0,m_appearance->findData(QSettings().value("unified/pdf_appearance","soft").toString())));
    m_appearance->setObjectName("unifiedPdfAppearance");
    m_cleanup->setObjectName("unifiedPdfCleanup");
    connect(m_appearance,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{
        QSettings().setValue("unified/pdf_appearance",m_appearance->currentData());
        emit pdfOutputSettingsChanged(appearance(),cleanupEnabled());
    });
    connect(m_cleanup,&QCheckBox::toggled,this,[this](bool enabled){
        QSettings().setValue("unified/pdf_cleanup",enabled);
        emit pdfOutputSettingsChanged(appearance(),enabled);
    });
    m_paper->setCurrentIndex(qMax(0,m_paper->findData(QSettings().value("unified/pdf_paper","a4").toString())));
    m_margin->setValue(QSettings().value("unified/pdf_margin",10).toDouble());m_targetText->setValue(QSettings().value("unified/pdf_text_height",0).toDouble());
    auto layoutChanged=[this]{auto v=pdfLayoutOptions();QSettings s;s.setValue("unified/pdf_paper",v.value("paper").toString());s.setValue("unified/pdf_margin",v.value("margin_mm").toDouble());s.setValue("unified/pdf_text_height",v.value("target_x_mm").toDouble());s.setValue("unified/pdf_content_fit",v.value("content_fit").toBool());emit pdfLayoutSettingsChanged(v);};
    connect(m_paper,qOverload<int>(&QComboBox::currentIndexChanged),this,layoutChanged);connect(m_margin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,layoutChanged);connect(m_targetText,qOverload<double>(&QDoubleSpinBox::valueChanged),this,layoutChanged);connect(m_fitContent,&QCheckBox::toggled,this,layoutChanged);
    loadQueue();refreshBooks();applyResources();
    m_monitorRequest=qEnvironmentVariable("STOU_MONITOR_REQUEST");
    if(!m_monitorRequest.isEmpty()){
        QFile request(m_monitorRequest);
        if(request.open(QIODevice::ReadOnly)){
            auto r=QJsonDocument::fromJson(request.readAll()).object();m_controlPath=r.value("control").toString(m_controlPath);m_monitorLog=r.value("event_log").toString();
            m_ocr->setOptions(r);
            if(r.contains("ocr_timing"))setOcrTiming(r.value("ocr_timing").toString());
            if(r.contains("output_finish"))setOutputFinish(r.value("output_finish").toObject());
            m_appearance->setCurrentIndex(qMax(0,m_appearance->findData(r.value("appearance").toString())));
            m_paper->setCurrentIndex(qMax(0,m_paper->findData(r.value("paper").toString())));m_margin->setValue(r.value("margin_mm").toDouble(10));m_targetText->setValue(r.value("target_x_mm").toDouble());m_cleanup->setChecked(r.value("page_cleanup").toBool(true));m_fitContent->setChecked(r.value("content_fit").toBool(true));
            // Existing completed-page records are already loaded. Only tail new events.
            m_monitorOffset=QFileInfo(m_monitorLog).size();pollExternal();
            auto monitor=new QTimer(this);monitor->setInterval(1000);connect(monitor,&QTimer::timeout,this,&UnifiedPanel::pollExternal);monitor->start();
        }
    }
}
QString UnifiedPanel::snapshotPath(){QString dir=unified_runtime::data(m_root)+"/projects/Native_Kitaplar/snapshots";QDir().mkpath(dir);return dir+"/"+uid()+".ScanTailor";}
QJsonObject UnifiedPanel::pdfLayoutOptions()const{return {{"paper",m_paper->currentData().toString()},{"margin_mm",m_margin->value()},{"target_x_mm",m_targetText->value()},{"content_fit",m_fitContent->isChecked()}};}
void UnifiedPanel::setPdfLayoutOptions(QJsonObject const& v){m_paper->setCurrentIndex(qMax(0,m_paper->findData(v.value("paper").toString())));m_margin->setValue(v.value("margin_mm").toDouble(10));m_targetText->setValue(v.value("target_x_mm").toDouble());m_fitContent->setChecked(v.value("content_fit").toBool(true));}
bool UnifiedPanel::busy()const{return m_externalRunning || m_worker.state()!=QProcess::NotRunning || (m_ocr&&m_ocr->busy());}
void UnifiedPanel::saveQueue(){writeJson(m_queuePath,{{"schema",1},{"output_root",m_output},{"jobs",m_jobs}});}
void UnifiedPanel::loadQueue(){
    QFile f(queueReadPath(m_queuePath));if(!f.open(QIODevice::ReadOnly))return;
    auto bytes=f.readAll();f.close();
    auto d=QJsonDocument::fromJson(bytes).object();if(d.value("schema").toInt()!=1)return;
    auto saved=d.value("jobs").toArray();
    for(auto value:saved){auto after=value.toObject();if(after.value("id").toString()!=m_previewJob)continue;
        for(auto old:m_jobs)if(old.toObject().value("id")==after.value("id")){adoptJobRevision(old.toObject(),after);break;}
    }
    m_jobs=saved;m_output=d.value("output_root").toString(m_output);
    QSignalBlocker block(m_pages);m_pages->clear();
    for(auto value:m_jobs){auto job=value.toObject();for(auto pageValue:job.value("pages").toArray()){
        auto page=pageValue.toObject();QString path=page.value("preview").toString();if(!QFileInfo::exists(path))continue;
        auto item=new QTreeWidgetItem(m_pages,{job.value("title").toString()+" · "+QString::number(page.value("page").toInt()),page.value("warning").toBool()?trU("Kontrol edin"):trU("Sayfa hazır"),trU("Kaydedilmiş PDF görünümü")});item->setData(0,Qt::UserRole,path);item->setData(0,Qt::UserRole+1,job.value("id").toString());
        while(m_pages->topLevelItemCount()>500)delete m_pages->takeTopLevelItem(0);
    }}
}
void UnifiedPanel::refreshBooks(){
    // Update rows in place: rebuilding the list on every progress event loses
    // focus/scroll and can spuriously navigate away from the user's selection.
    QSignalBlocker block(m_books);
    QSet<QString> ids;
    QHash<QString,QTreeWidgetItem*> rows;
    for(int n=0;n<m_books->topLevelItemCount();++n){auto item=m_books->topLevelItem(n);rows.insert(item->data(0,Qt::UserRole+1).toString(),item);}
    for(int i=0;i<m_jobs.size();++i){
        auto j=m_jobs.at(i).toObject();QString id=j.value("id").toString();ids.insert(id);
        QTreeWidgetItem* item=rows.value(id,nullptr);
        if(!item)item=new QTreeWidgetItem(m_books);
        item->setText(0,j.value("title").toString());item->setText(1,j.value("status").toString(trU("Bekliyor")));item->setText(2,j.value("detail").toString());
        item->setData(0,Qt::UserRole,i);item->setData(0,Qt::UserRole+1,id);
    }
    for(int n=m_books->topLevelItemCount()-1;n>=0;--n)if(!ids.contains(m_books->topLevelItem(n)->data(0,Qt::UserRole+1).toString()))delete m_books->takeTopLevelItem(n);
}
void UnifiedPanel::showBookSelection(QTreeWidgetItem* item){if(item)showSelection(item->data(0,Qt::UserRole+1).toString());}
void UnifiedPanel::showPageSelection(QTreeWidgetItem* item){
    if(!item)return;QString id=item->data(0,Qt::UserRole+1).toString();
    {QSignalBlocker block(m_books);for(int i=0;i<m_books->topLevelItemCount();++i)if(m_books->topLevelItem(i)->data(0,Qt::UserRole+1).toString()==id){m_books->setCurrentItem(m_books->topLevelItem(i));break;}}
    showSelection(id,item->data(0,Qt::UserRole).toString());
}
void UnifiedPanel::showSelection(QString const& id,QString const& path){
    quint64 request=++m_selectionRequest;
    QTimer::singleShot(0,this,[this,id,path,request]{
        if(request!=m_selectionRequest)return;
        for(auto value:m_jobs){auto job=value.toObject();if(job.value("id").toString()!=id)continue;
            m_previewJob=id;m_previewPath=path;
            m_queueContext->setText(trU("Kuyrukta seçili kitap: ")+job.value("title").toString());
            auto images=unifiedQueueImages(job,m_livePreviews.value(id),path);
            emit selectionPreviewRequested(job.value("title").toString(),images,path.isEmpty()?QString():QFileInfo(path).absoluteFilePath());return;
        }
    });
}
void UnifiedPanel::refreshPreview(){
    if(m_previewJob.isEmpty())return;
    for(auto value:m_jobs){auto job=value.toObject();if(job.value("id").toString()!=m_previewJob)continue;
        emit selectionPreviewUpdated(job.value("title").toString(),unifiedQueueImages(job,m_livePreviews.value(m_previewJob),m_previewPath),m_previewPath);return;
    }
}
void UnifiedPanel::previewBook(QString const& id,int page){
    QString path;
    for(auto value:m_jobs){auto job=value.toObject();if(job.value("id").toString()!=id)continue;
        for(auto item:unifiedQueueImages(job))if(item.toObject().value("page").toInt()==page&&page>0){path=item.toObject().value("path").toString();break;}
        break;
    }
    showSelection(id,path);
}
void UnifiedPanel::editPreview(int stage){
    QString id=m_previewJob;
    if(id.isEmpty()&&m_books->currentItem())id=m_books->currentItem()->data(0,Qt::UserRole+1).toString();
    for(auto value:m_jobs){auto job=value.toObject();if(job.value("id").toString()!=id)continue;
        auto target=unifiedEditTarget(job,m_previewPath,m_livePreviews.value(id));
        if(target.isEmpty()){m_status->setText(trU("Bu sayfanın kaynak projesi henüz hazır değil veya sayfa bağlantısı bulunamadı."));return;}
        emit editRequested(target,stage);return;
    }
}
QString UnifiedPanel::editCopy(QJsonObject const& target)const{
    QString drafts=unified_runtime::data(m_root)+"/projects/NativeEdits";
    auto key=QCryptographicHash::hash((QFileInfo(m_queuePath).absoluteFilePath()+"\n"+target.value("job_id").toString()).toUtf8(),QCryptographicHash::Sha256).toHex();
    QString index=drafts+"/bindings/"+QString::fromLatin1(key)+".json";
    QFile prior(index);
    if(prior.open(QIODevice::ReadOnly)){
        auto data=QJsonDocument::fromJson(prior.readAll()).object();prior.close();
        QString project=data.value("project").toString();
        if(QFileInfo::exists(project)&&QFileInfo::exists(project+".unified-edit.json"))return project;
    }
    QString project=unifiedCreateEditCopy(target,drafts,m_queuePath);
    QDir().mkpath(QFileInfo(index).absolutePath());QSaveFile file(index);
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(QJsonObject{{"project",project}}).toJson())<0||!file.commit())throw std::runtime_error("Düzenleme bağlantısı kaydedilemedi.");
    return project;
}
QString UnifiedPanel::revisionSnapshotPath()const{
    QString dir=m_queuePath+".edits/snapshots";QDir().mkpath(dir);
    return dir+"/"+uid()+".ScanTailor";
}
bool UnifiedPanel::submitRevision(QJsonObject const& binding,QString const& snapshot){
    if(QFileInfo(binding.value("queue").toString()).absoluteFilePath()!=QFileInfo(m_queuePath).absoluteFilePath()){
        m_status->setText(trU("Bu düzenleme başka bir kitap kuyruğuna ait."));return false;
    }
    QFile source(snapshot);if(!source.open(QIODevice::ReadOnly))return false;
    auto sha=QCryptographicHash::hash(source.readAll(),QCryptographicHash::Sha256).toHex();source.close();
    QString id=QFileInfo(snapshot).baseName();
    QJsonObject request{{"id",id},{"job_id",binding.value("job_id")},{"snapshot",snapshot},{"sha256",QString::fromLatin1(sha)},{"preserve_margins",binding.value("preserve_margins").toBool(true)},{"run_while_paused",true}};
    auto options=pdfLayoutOptions();auto ocr=m_ocr->options();for(auto it=ocr.begin();it!=ocr.end();++it)options.insert(it.key(),it.value());options.insert("appearance",appearance());options.insert("page_cleanup",cleanupEnabled());options.insert("ocr_timing",ocrTiming());options.insert("output_finish",outputFinish());request.insert("export_options",options);
    QSaveFile file(m_queuePath+".edits/"+id+".request.json");
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(request).toJson())<0||!file.commit()){
        m_status->setText(trU("Düzeltme isteği kaydedilemedi. Düzenleme projesi korunuyor."));return false;
    }
    m_revisionJob=binding.value("job_id").toString();m_revisionToken=id;m_revisionStatus->show();
    m_revisionStatus->setText(binding.value("title").toString()+trU(" · Düzenleme kaydedildi. PDF güncellemesi öncelikli sırada; eski PDF korunuyor."));
    if(busy()){
        QFile controlFile(m_controlPath);QString state;if(controlFile.open(QIODevice::ReadOnly)){state=QJsonDocument::fromJson(controlFile.readAll()).object().value("state").toString();controlFile.close();}
        m_revisionStartPending=state=="cancel";
        m_status->setText(trU("PDF düzeltmesi kaydedildi; çalışan sayfanın kaydından sonra öncelikli işlenecek. Duraklatılmış ana kuyruk beklemeyi sürdürür."));
    }else start(false,true);
    return true;
}
void UnifiedPanel::addProject(QString const& title,QString const& path,QJsonArray const& outputs){
    if(busy())return;for(auto value:m_jobs)if(value.toObject().value("source_project").toString()==path)return;
    m_jobs.append(QJsonObject{{"id",uid()},{"title",title},{"source_project",path},{"outputs",outputs},{"status",trU("Bekliyor")}});saveQueue();refreshBooks();
}
void UnifiedPanel::addFolder(QString const& path){
    QDir dir(path);if(dir.entryList({"*.jpg","*.jpeg","*.png","*.tif","*.tiff","*.bmp"},QDir::Files).isEmpty())return;
    for(auto value:m_jobs)if(QFileInfo(value.toObject().value("folder").toString()).absoluteFilePath()==QFileInfo(path).absoluteFilePath())return;
    m_jobs.append(QJsonObject{{"id",uid()},{"title",dir.dirName()},{"folder",dir.absolutePath()},{"split",m_split->currentData().toString()},{"status",trU("Bekliyor")}});saveQueue();refreshBooks();
}
void UnifiedPanel::applyResources(){
    QSettings s;s.setValue("settings/batch_processing_threads",m_threads->value());s.setValue("unified/cpu_limit",m_cpu->value());s.setValue("unified/single_gpu",m_singleGpu->isChecked());s.setValue("unified/page_delay_ms",m_delay->value());s.setValue("unified/protect_numbers",m_numbers->isChecked());s.sync();
    m_limits->setText(unifiedApplyCpuLimit(m_cpu->value()));emit accelerationChanged();
}
void UnifiedPanel::control(QString const& state){writeJson(m_controlPath,{{"state",state}});}
void UnifiedPanel::stop(){m_revisionStartPending=false;if(m_externalRunning || m_worker.state()!=QProcess::NotRunning){control("cancel");m_status->setText(trU("Durduruluyor; mevcut sayfanın tamamlanması bekleniyor."));}else if(m_ocr->busy())m_status->setText(trU("OCR paketi işleminin tamamlanması bekleniyor."));}
void UnifiedPanel::updateRevisionStatus(){
    QFile ctl(m_controlPath);if(ctl.open(QIODevice::ReadOnly)){auto state=QJsonDocument::fromJson(ctl.readAll()).object().value("state").toString();ctl.close();m_paused=state=="pause";m_pause->setText(trU(m_paused?"Devam et":"Duraklat"));}
    if(m_revisionStartPending&&!busy()){m_revisionStartPending=false;start(false,true);}
    if(m_revisionJob.isEmpty())return;
    QString sessionPath=m_worker.state()!=QProcess::NotRunning?m_requestPath:m_monitorRequest;
    QFile status(sessionPath+".session.json");if(status.open(QIODevice::ReadOnly)){auto state=QJsonDocument::fromJson(status.readAll()).object();status.close();auto event=state.value("event").toObject();
        if(event.value("job").toString()==m_revisionJob)m_revisionStatus->setText(trU("PDF güncellemesi · ")+event.value("stage").toString()+" · "+event.value("detail").toString());
    }
    for(auto value:m_jobs){auto job=value.toObject();if(job.value("id").toString()!=m_revisionJob)continue;
        if(job.value("accepted_edits").toArray().contains(m_revisionToken)&&!job.value("edit_completed_id").toString().isEmpty()&&job.value("edit_completed_id")==job.value("edit_request_id")){
            m_revisionStatus->setText(job.value("title").toString()+trU(" · PDF güncellendi. Yeni sürümü alttaki kitap satırından açabilirsiniz."));m_revisionJob.clear();break;
        }
    }
}
void UnifiedPanel::start(bool prepare,bool revisionsOnly){
    if(!prepare&&ocrTiming()!="none"){QString error=m_ocr->validation();if(!error.isEmpty()){m_status->setText(error);return;}}
    if(busy()||m_jobs.isEmpty())return;
    if(!m_monitorRequest.isEmpty())loadQueue();
    applyResources();if(!revisionsOnly)saveQueue();m_paused=false;control("run");
    QJsonObject request{{"queue",m_queuePath},{"control",m_controlPath},{"root",m_root},{"cli",unified_runtime::cli()},{"mode",prepare?"prepare":"export"},{"appearance",m_appearance->currentData().toString()},{"paper",m_paper->currentData().toString()},{"threads",m_threads->value()},{"delay_ms",m_delay->value()},{"auto_warp",m_autoWarp->isChecked()}};
    auto ocr=m_ocr->options();for(auto i=ocr.begin();i!=ocr.end();++i)request.insert(i.key(),i.value());
    request.insert("ocr_timing",ocrTiming());
    request.insert("output_finish",outputFinish());
    request.insert("margin_mm",m_margin->value());request.insert("target_x_mm",m_targetText->value());
    request.insert("content_fit",m_fitContent->isChecked());QSettings().setValue("unified/pdf_content_fit",m_fitContent->isChecked());
    request.insert("revision_only",revisionsOnly);request.insert("replay_cached_pages",false);
    request.insert("page_cleanup",m_cleanup->isChecked());request.insert("native_scale",3);request.insert("reclaim_intermediates",true);QSettings().setValue("unified/pdf_cleanup",m_cleanup->isChecked());
    QSettings stored;stored.setValue("unified/pdf_appearance",m_appearance->currentData());stored.setValue("unified/pdf_paper",m_paper->currentData());stored.setValue("unified/pdf_margin",m_margin->value());stored.setValue("unified/pdf_text_height",m_targetText->value());stored.sync();
    m_requestPath=unified_runtime::data(m_root)+"/projects/Native_Kitaplar/request-"+uid()+".json";writeJson(m_requestPath,request);
    m_buffer.clear();m_pages->clear();m_progress->setValue(0);m_start->setEnabled(false);m_prepare->setEnabled(false);
    auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("OMP_THREAD_LIMIT",QString::number(m_threads->value()));env.insert("OMP_NUM_THREADS",QString::number(m_threads->value()));
    m_worker.setProcessEnvironment(env);m_worker.setWorkingDirectory(m_root);m_worker.start(unified_runtime::python(m_root),{"-u","-m","scantailor_ocr_unified.native_worker",m_requestPath});
}
void UnifiedPanel::receive(){m_buffer+=m_worker.readAllStandardOutput();int i;while((i=m_buffer.indexOf('\n'))>=0){auto line=m_buffer.left(i);m_buffer.remove(0,i+1);auto value=QJsonDocument::fromJson(line);if(value.isObject())event(value.object());else if(!line.trimmed().isEmpty())m_log->appendPlainText(QString::fromUtf8(line));}}
void UnifiedPanel::adoptJobRevision(QJsonObject const& before,QJsonObject const& after){
    if(after.value("pdf")==before.value("pdf")||after.value("pdf").toString().isEmpty())return;
    QString id=before.value("id").toString();int pageNumber=0;
    if(id==m_previewJob)for(auto value:unifiedQueueImages(before,m_livePreviews.value(id),m_previewPath)){
        auto image=value.toObject();if(image.value("path").toString()==m_previewPath){pageNumber=image.value("page").toInt();break;}
    }
    m_livePreviews.remove(id);
    if(id==m_previewJob){
        for(auto value:after.value("pages").toArray()){auto page=value.toObject();if(page.value("page").toInt()==pageNumber){m_previewPath=page.value("preview").toString();break;}}
        if(!m_previewRefresh->isActive())m_previewRefresh->start();
    }
}
void UnifiedPanel::pollExternal(){
    if(m_monitorRequest.isEmpty()||m_worker.state()!=QProcess::NotRunning)return;
    QFile status(m_monitorRequest+".session.json");QJsonObject state;
    if(status.open(QIODevice::ReadOnly)){auto bytes=status.readAll();status.close();state=QJsonDocument::fromJson(bytes).object();}
    bool running=state.value("state").toString()=="running" && unifiedProcessRunning(qint64(state.value("pid").toDouble()));
    bool changed=running!=m_externalRunning;m_externalRunning=running;
    m_start->setEnabled(!running);m_prepare->setEnabled(!running);
    auto now=QDateTime::currentMSecsSinceEpoch();
    if(running && (changed || now-m_monitorQueueRead>=5000)){
        m_monitorQueueRead=now;
        // Bring newly prepared projects and final PDFs into the existing rows
        // without rebuilding page lists or stealing the user's selection.
        QFile queue(queueReadPath(m_queuePath));if(queue.open(QIODevice::ReadOnly)){
            auto bytes=queue.readAll();queue.close();
            auto saved=QJsonDocument::fromJson(bytes).object().value("jobs").toArray();
            for(int i=0;i<qMin(saved.size(),m_jobs.size());++i){auto a=m_jobs.at(i).toObject(),b=saved.at(i).toObject();
                if(a.value("id")!=b.value("id"))continue;
                adoptJobRevision(a,b);
                // Keep the latest live status, but refresh every durable field,
                // including revision IDs and manual export policies.
                for(auto key:{"status","detail"})if(a.contains(key))b.insert(key,a.value(key));
                m_jobs.replace(i,b);
            }
        }
    }
    QFile log(m_monitorLog);
    if(log.open(QIODevice::ReadOnly)){
        if(log.size()<m_monitorOffset)m_monitorOffset=0;
        log.seek(m_monitorOffset);
        QList<QByteArray> lines;
        while(!log.atEnd() && lines.size()<200){auto line=log.readLine();if(!line.endsWith('\n'))break;lines.append(line);m_monitorOffset=log.pos();}
        log.close();
        for(auto const& line:lines){auto value=QJsonDocument::fromJson(line);if(value.isObject())event(value.object());}
    }
    if(changed){loadQueue();refreshBooks();refreshPreview();}
    if(changed && !running)m_status->setText(trU("İşçi durdu / tamamlandı. Kaydedilmiş sonuçlar yüklendi; PDF oluştur / devam et ile sürdürebilirsiniz."));
    // The small status snapshot is authoritative even while a long event log
    // is being replayed. Do not show a stale stage or duplicate preview rows.
    auto latest=state.value("event").toObject();
    if(running && !latest.isEmpty()){
        m_status->setText(latest.value("book").toString()+" · "+latest.value("stage").toString()+" · "+latest.value("detail").toString());
        if(latest.contains("total")){m_progress->setRange(0,qMax(1,latest.value("total").toInt()));m_progress->setValue(latest.value("page").toInt());}
    }
}
void UnifiedPanel::event(QJsonObject const& o){
    QString detail=o.value("detail").toString(),stage=o.value("stage").toString(),title=o.value("book").toString();
    m_status->setText(title+" · "+stage+" · "+detail);m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss ")+m_status->text());
    if(o.contains("total")){m_progress->setRange(0,qMax(1,o.value("total").toInt()));m_progress->setValue(o.value("page").toInt());}
    for(int i=0;i<m_jobs.size();++i){auto j=m_jobs.at(i).toObject();if(j.value("id")==o.value("job")){j.insert("status",stage);j.insert("detail",detail);m_jobs.replace(i,j);break;}}
    refreshBooks();QString p=o.value("preview").toString();if(!p.isEmpty()){
        QString id=o.value("job").toString();auto& previews=m_livePreviews[id];
        bool found=false;for(int n=0;n<previews.size();++n)if(previews.at(n).toObject().value("preview").toString()==p){previews.replace(n,o);found=true;break;}
        if(!found)previews.append(o);
        if(id==m_previewJob && !m_previewRefresh->isActive())m_previewRefresh->start();
        auto item=new QTreeWidgetItem(m_pages,{title+" · "+QString::number(o.value("page").toInt()),stage,detail});item->setData(0,Qt::UserRole,p);item->setData(0,Qt::UserRole+1,id);
        while(m_pages->topLevelItemCount()>500)delete m_pages->takeTopLevelItem(0);
        if(!m_pages->currentItem())m_pages->scrollToItem(item);
    }
}
void UnifiedPanel::preview(QString const& path){
    if(path.isEmpty()||!QFileInfo::exists(path))return;if(path.endsWith(".pdf",Qt::CaseInsensitive)){QDesktopServices::openUrl(QUrl::fromLocalFile(path));return;}
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QFileInfo(path).fileName()+trU(" · tamamlanan sayfa"));dialog->resize(850,850);
    auto layout=new QVBoxLayout(dialog);auto bar=new QHBoxLayout;layout->addLayout(bar);
    QImage full(path);if(full.isNull()){dialog->deleteLater();m_status->setText(trU("Önizleme resmi okunamadı: ")+path);return;}
    auto view=new BasicImageView(std::make_shared<NonAcceleratedOperations>(),full);layout->addWidget(view);
    for(auto spec:QList<QPair<QString,double>>{{"+",1.25},{"−",.8},{trU("Ekrana sığdır"),0}}){auto b=new QPushButton(spec.first);bar->addWidget(b);connect(b,&QPushButton::clicked,[view,spec]{view->setZoomLevel(spec.second?qBound(1.,view->zoomLevel()*spec.second,30.):1.);if(!spec.second)view->moveTowardsIdealPosition(std::numeric_limits<double>::max());});}
    layout->addWidget(new QLabel(trU("Tekerlek / + −: yakınlaştır · Sol veya orta tuşla tutup kaydır · Çift tık: ekrana sığdır. PDF değişmez.")));
    dialog->show();
}

void UnifiedPanel::setPanelHeight(int height){QSignalBlocker block(m_panelHeight);m_panelHeight->setValue(height);}
void UnifiedPanel::setEditorContext(QString const& title,QString const& path){
    m_editorContext->setText(trU("Düzenleyicide açık proje: ")+(title.isEmpty()?trU("yok"):title));
    m_editorContext->setToolTip(path);m_capture->setEnabled(!title.isEmpty());
    m_capture->setToolTip(trU("Kuyruğa eklenecek açık proje: ")+(title.isEmpty()?trU("yok"):title)+trU(". Şu anki ayarların kopyası eklenir; kuyrukta seçili kitap kullanılmaz."));
}
void UnifiedPanel::setAppearance(QString const& appearance){int index=m_appearance->findData(appearance);if(index>=0)m_appearance->setCurrentIndex(index);}
QString UnifiedPanel::ocrTiming()const{return m_ocrTiming->currentData().toString();}
QJsonObject UnifiedPanel::outputFinish()const{return m_outputFinish;}
void UnifiedPanel::setOutputFinish(QJsonObject const& value){if(m_outputFinish==value)return;m_outputFinish=value;QSettings().setValue("unified/output_finish",QJsonDocument(value).toJson(QJsonDocument::Compact));emit outputFinishChanged(value);}
void UnifiedPanel::setOcrTiming(QString const& mode){int i=m_ocrTiming->findData(mode);if(i>=0)m_ocrTiming->setCurrentIndex(i);}
void UnifiedPanel::showToolsTab(int index){if(auto tabs=findChild<QTabWidget*>("unifiedToolTabs"))tabs->setCurrentIndex(qBound(0,index,tabs->count()-1));}
void UnifiedPanel::openPdfTools(QString const& mode){
    if(auto existing=findChild<QDialog*>("unifiedPdfTools")){static_cast<UnifiedPdfToolsDialog*>(existing)->setMode(mode);existing->show();existing->raise();return;}
    auto d=new UnifiedPdfToolsDialog(m_root,this);d->setMode(mode);d->importReady=[this](QJsonObject const& result){if(busy())return false;QString folder=result.value("folder").toString();addFolder(folder);for(int i=0;i<m_jobs.size();++i){auto job=m_jobs[i].toObject();if(job.value("folder").toString()==QFileInfo(folder).absoluteFilePath()){job.insert("title",QFileInfo(result.value("source").toString()).completeBaseName());job.insert("split","1");job.insert("source_pdf",result.value("source"));job.insert("source_text",result.value("text_folder"));m_jobs[i]=job;}}saveQueue();refreshBooks();return true;};d->show();
}
void UnifiedPanel::setCleanupEnabled(bool enabled){m_cleanup->setChecked(enabled);}
