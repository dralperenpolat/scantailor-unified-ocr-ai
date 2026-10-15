// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QImageReader>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <functional>

class UnifiedWorkspaceDialog:public QDialog {
public:
    std::function<void(QJsonObject const&)> applyProfile;
    std::function<QJsonArray()> currentJobs;
    std::function<QJsonObject()> currentOptions;
    UnifiedWorkspaceDialog(QString root,QString queue,QString output,QWidget* parent):QDialog(parent),m_root(root),m_queue(queue),m_output(output) {
        setObjectName("unifiedWorkspace");setAttribute(Qt::WA_DeleteOnClose);resize(1080,800);setWindowTitle(tr("Çalışma merkezi"));
        auto outer=new QVBoxLayout(this);auto tabs=new QTabWidget;outer->addWidget(tabs,1);
        settingsTab(tabs);reviewTab(tabs);pdfTab(tabs);corpusTab(tabs);
        m_status=new QLabel(tr("Hazır"));m_status->setWordWrap(true);m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);outer->addWidget(m_status);
        connect(&m_process,&QProcess::started,[this]{m_process.write(QJsonDocument(m_request).toJson(QJsonDocument::Compact)+"\n");m_process.closeWriteChannel();});
        connect(&m_process,&QProcess::readyReadStandardOutput,[this]{m_buffer+=m_process.readAllStandardOutput();});
        connect(&m_process,&QProcess::readyReadStandardError,[this]{m_errors+=m_process.readAllStandardError();m_errors=m_errors.right(4000);});
        connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),[this](int code,QProcess::ExitStatus){
            m_buffer+=m_process.readAllStandardOutput();QJsonObject value;
            for(auto line:m_buffer.split('\n'))if(line.startsWith("STOUWORK "))value=QJsonDocument::fromJson(line.mid(9)).object();
            auto callback=std::move(m_callback);m_callback={};
            if(!value.value("ok").toBool()){m_status->setText(unified_i18n::display(value.value("error").toString())+QString::fromUtf8(m_errors.right(1000)));return;}
            m_status->setText(tr("İşlem tamamlandı."));if(callback)callback(value.value("result").toObject());
            Q_UNUSED(code);
        });
        connect(&m_process,&QProcess::errorOccurred,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)m_status->setText(m_process.errorString());});
        QTimer::singleShot(0,this,[this]{refreshJobs();request({{"action","settings"}},[this](QJsonObject const& result){
            m_settings=result.value("settings").toObject();m_profiles=result.value("profiles").toObject();
            for(auto it=m_profiles.begin();it!=m_profiles.end();++it)m_profile->addItem(profileName(it.key()),it.key());loadSettings();
        });});
    }
    ~UnifiedWorkspaceDialog(){if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(2000);}}
private:
    QString m_root,m_queue,m_output,m_manifest;QProcess m_process;QByteArray m_buffer,m_errors;
    QJsonObject m_request,m_settings,m_profiles,m_cleanup;QJsonArray m_rows,m_corpus;
    std::function<void(QJsonObject const&)> m_callback;
    QLabel *m_status,*m_storageSummary,*m_before,*m_after,*m_corpusImage,*m_quality;
    QComboBox *m_resource,*m_compression,*m_profile,*m_version,*m_format,*m_jobs;
    QDoubleSpinBox *m_quota,*m_reserve,*m_confidence;
    QLineEdit *m_temp,*m_ocrmypdf,*m_verapdf,*m_kraken,*m_krakenModel,*m_structure,*m_note,*m_pdf,*m_range,*m_tags;
    QCheckBox *m_compare,*m_text,*m_problems,*m_clipped,*m_footnote,*m_splitError,*m_orderError;QSpinBox *m_pilot;
    QTreeWidget *m_storage,*m_pages,*m_samples,*m_sizes;
    QPlainTextEdit *m_reasons,*m_reference,*m_hypothesis;
    int m_offset=0;QString m_resultPdf;
    static QString tr(char const* text){return unified_i18n::text(text);}
    static QString profileName(QString const& name){
        if(name=="cpu_fast")return tr("CPU hızlı · Tesseract Fast · OCR en son");if(name=="cpu_quality")return tr("CPU kalite · Tesseract Best · OCR en son");
        if(name=="de_legal")return tr("Almanca hukuk kitabı");if(name=="tr_legal")return tr("Türkçe hukuk kitabı");
        if(name=="small_print")return tr("Küçük punto / dipnot");if(name=="illustrated")return tr("Resimli kitap");return name;
    }
    QPushButton* button(QLayout* layout,char const* title,std::function<void()> action){
        auto b=new QPushButton(tr(title));layout->addWidget(b);connect(b,&QPushButton::clicked,this,std::move(action));return b;
    }
    QVBoxLayout* tab(QTabWidget* tabs,char const* title){
        auto scroll=new QScrollArea;scroll->setWidgetResizable(true);auto page=new QWidget;auto layout=new QVBoxLayout(page);scroll->setWidget(page);tabs->addTab(scroll,tr(title));return layout;
    }
    void request(QJsonObject value,std::function<void(QJsonObject const&)> callback={}){
        if(m_process.state()!=QProcess::NotRunning){m_status->setText(tr("Önce devam eden işlemin bitmesini bekleyin."));return;}
        value.insert("root",m_root);value.insert("queue",m_queue);value.insert("output",m_output);m_request=value;m_callback=std::move(callback);m_buffer.clear();m_errors.clear();
        auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONPATH",m_root+"/src");env.insert("PYTHONUTF8","1");
        m_process.setProcessEnvironment(env);m_process.setWorkingDirectory(m_root);m_status->setText(tr("İşleniyor…"));
        m_process.start(unified_runtime::python(m_root),{"-u","-m","scantailor_ocr_unified.workspace_service"});
    }
    void image(QLabel* label,QString const& path){
        QImageReader reader(path);auto size=reader.size();if(size.isValid())reader.setScaledSize(size.scaled(480,420,Qt::KeepAspectRatio));
        auto value=reader.read();label->setPixmap(QPixmap::fromImage(value));if(value.isNull())label->setText(tr("Önizleme yok"));
    }
    void open(QString const& path){if(!path.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(path));}
    QString outputFolder(QString const& suffix){return m_output+"/"+suffix+"-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");}
    QJsonObject job()const{return m_jobs->currentData().toJsonObject();}
    void refreshJobs(){
        auto selected=job().value("id").toString();m_jobs->clear();if(currentJobs)for(auto value:currentJobs()){auto j=value.toObject();m_jobs->addItem(j.value("title").toString(),j);if(j.value("id")==selected)m_jobs->setCurrentIndex(m_jobs->count()-1);}
    }
    void settingsTab(QTabWidget* tabs){
        auto layout=tab(tabs,"Depolama ve profiller");auto form=new QFormLayout;layout->addLayout(form);
        auto scope=new QLabel(tr("Kapsam: kaydettiğiniz tercihler sonraki işlemlere uygulanır. Çalışan kitap mevcut ayarlarıyla devam eder."));scope->setWordWrap(true);layout->addWidget(scope);
        m_temp=new QLineEdit;form->addRow(tr("Geçici çalışma klasörü"),m_temp);
        button(layout,"Çalışma diski seç",[this]{auto p=QFileDialog::getExistingDirectory(this,tr("Çalışma diski seç"),m_temp->text());if(!p.isEmpty())m_temp->setText(p);});
        m_quota=new QDoubleSpinBox;m_quota->setRange(0,100000);m_quota->setSuffix(" GiB");form->addRow(tr("Yeniden üretilebilir önizleme kotası"),m_quota);
        m_reserve=new QDoubleSpinBox;m_reserve->setRange(.25,10000);m_reserve->setSuffix(" GiB");form->addRow(tr("Diskte korunacak boş alan"),m_reserve);
        m_resource=new QComboBox;for(auto item:QList<QPair<QString,QString>>{{tr("Hızlı"),"fast"},{tr("Dengeli"),"balanced"},{tr("Sessiz"),"quiet"}})m_resource->addItem(item.first,item.second);form->addRow(tr("Kaynak kullanımı"),m_resource);
        m_compression=new QComboBox;for(auto item:QList<QPair<QString,QString>>{{tr("Kayıpsız"),"lossless"},{tr("Dengeli · JPEG 92 / 300 DPI"),"balanced"},{tr("Küçük · JPEG 80 / 200 DPI"),"small"}})m_compression->addItem(item.first,item.second);form->addRow(tr("PDF boyut profili"),m_compression);
        m_format=new QComboBox;m_format->addItem("PDF","pdf");m_format->addItem(tr("PDF/A-2 · veraPDF doğrulamalı"),"pdfa");form->addRow(tr("Arşiv çıktısı"),m_format);
        m_compare=new QCheckBox(tr("Şüpheli Paddle sayfalarını Tesseract ile karşılaştır"));form->addRow(m_compare);
        m_confidence=new QDoubleSpinBox;m_confidence->setRange(0,1);m_confidence->setSingleStep(.05);form->addRow(tr("Motor güven eşiği · doğruluk oranı değildir"),m_confidence);
        m_version=new QComboBox;m_version->addItem("PP-OCRv5","PP-OCRv5");m_version->addItem(tr("PP-OCRv6 · karşılaştırma adayı"),"PP-OCRv6");form->addRow(tr("Paddle modeli"),m_version);
        m_text=new QCheckBox(tr("TXT, Markdown, ALTO ve okuma sırası kutularını kaydet"));form->addRow(m_text);
        m_ocrmypdf=new QLineEdit;m_verapdf=new QLineEdit;m_kraken=new QLineEdit;m_krakenModel=new QLineEdit;m_structure=new QLineEdit;
        form->addRow("OCRmyPDF",m_ocrmypdf);form->addRow("veraPDF",m_verapdf);form->addRow("Kraken",m_kraken);form->addRow(tr("Kraken model dosyası"),m_krakenModel);form->addRow(tr("PP-StructureV3 Python ortamı"),m_structure);
        button(layout,"Tercihleri kaydet",[this]{saveSettings();});
        button(layout,"İsteğe bağlı araçları denetle",[this]{request({{"action","optional-probe"}},[this](QJsonObject const& value){QStringList rows;for(auto it=value.begin();it!=value.end();++it)rows<<it.key()+": "+(it.value().toObject().value("available").toBool()?tr("Hazır"):tr("Kurulu değil"));m_status->setText(rows.join(" · "));});});
        auto preset=new QHBoxLayout;layout->addLayout(preset);m_profile=new QComboBox;preset->addWidget(m_profile);
        button(preset,"Kitap profilini uygula",[this]{auto value=m_profiles.value(m_profile->currentData().toString()).toObject();if(applyProfile)applyProfile(value);for(auto it=value.begin();it!=value.end();++it)m_settings.insert(it.key(),it.value());loadSettings();saveSettings();});
        button(preset,"Geçerli ayarları profil olarak sakla",[this]{auto name="Profile "+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");auto value=currentOptions?currentOptions():QJsonObject();request({{"action","profile-save"},{"name",name},{"settings",value}},[this,name](QJsonObject const& saved){m_profiles.insert(name,saved);m_profile->addItem(name,name);});});
        m_storage=new QTreeWidget;m_storage->setHeaderLabels({tr("Kategori"),"GiB",tr("Dosya"),tr("Konum")});m_storage->setRootIsDecorated(false);m_storage->setMinimumHeight(150);layout->addWidget(m_storage);
        m_storageSummary=new QLabel;m_storageSummary->setWordWrap(true);layout->addWidget(m_storageSummary);
        button(layout,"Depolama kullanımını ölç",[this]{request({{"action","storage"}},[this](QJsonObject const& result){
            m_storage->clear();auto categories=result.value("categories").toObject();for(auto it=categories.begin();it!=categories.end();++it){auto row=it.value().toObject();new QTreeWidgetItem(m_storage,{it.key(),QString::number(row.value("bytes").toDouble()/1073741824.,'f',2),QString::number(row.value("files").toInt()),row.value("path").toString()});}
            m_cleanup=result.value("cleanup").toObject();m_storageSummary->setText(tr("Boş alan: %1 GiB · Temizlenebilir önizleme: %2 GiB. Kaynaklar, projeler ve elle kaydedilen düzeltmeler korunur.").arg(result.value("free_bytes").toDouble()/1073741824.,0,'f',2).arg(m_cleanup.value("reclaimable_bytes").toDouble()/1073741824.,0,'f',2));
        });});
        button(layout,"Ölçülen önizleme önbelleğini kotaya indir",[this]{if(m_cleanup.isEmpty()){m_status->setText(tr("Önce depolama kullanımını ölçün."));return;}request({{"action","cleanup"},{"plan",m_cleanup}},[this](QJsonObject const& result){m_cleanup={};m_status->setText(tr("Temizlendi: %1 dosya · %2 MiB").arg(result.value("files").toInt()).arg(result.value("bytes").toDouble()/1048576.,0,'f',1));});});
        button(layout,"Fotoğraf klasörü için alan tahmini",[this]{auto folder=QFileDialog::getExistingDirectory(this,tr("Fotoğrafların bulunduğu kitap klasörü"));if(!folder.isEmpty())request({{"action","estimate"},{"folder",folder}},[this](QJsonObject const& r){m_status->setText(tr("%1 sayfa · tahmini çalışma alanı %2 GiB. Gerçek kullanım ölçek ve sıkıştırmaya göre değişir.").arg(r.value("pages").toInt()).arg(r.value("estimated_work_bytes").toDouble()/1073741824.,0,'f',2));});});
        layout->addStretch();
    }
    void loadSettings(){
        m_temp->setText(m_settings.value("temp_root").toString());m_quota->setValue(m_settings.value("cache_quota_gib").toDouble(10));m_reserve->setValue(m_settings.value("reserve_gib").toDouble(2));
        auto set=[this](QComboBox* box,char const* key){int i=box->findData(m_settings.value(key).toString());if(i>=0)box->setCurrentIndex(i);};
        set(m_resource,"resource_profile");set(m_compression,"compression_profile");set(m_version,"paddle_version");set(m_format,"output_format");
        m_compare->setChecked(m_settings.value("ocr_compare").toBool());m_confidence->setValue(m_settings.value("ocr_confidence_threshold").toDouble(.85));m_text->setChecked(m_settings.value("structured_text").toBool(true));
        m_ocrmypdf->setText(m_settings.value("ocrmypdf").toString());m_verapdf->setText(m_settings.value("verapdf").toString());m_kraken->setText(m_settings.value("kraken").toString());m_krakenModel->setText(m_settings.value("kraken_model").toString());m_structure->setText(m_settings.value("structure_python").toString());
    }
    void saveSettings(){
        m_settings.insert("temp_root",m_temp->text());m_settings.insert("cache_quota_gib",m_quota->value());m_settings.insert("reserve_gib",m_reserve->value());
        m_settings.insert("resource_profile",m_resource->currentData().toString());m_settings.insert("compression_profile",m_compression->currentData().toString());m_settings.insert("output_format",m_format->currentData().toString());
        m_settings.insert("paddle_version",m_version->currentData().toString());m_settings.insert("ocr_compare",m_compare->isChecked());m_settings.insert("ocr_confidence_threshold",m_confidence->value());m_settings.insert("structured_text",m_text->isChecked());
        m_settings.insert("ocrmypdf",m_ocrmypdf->text());m_settings.insert("verapdf",m_verapdf->text());m_settings.insert("kraken",m_kraken->text());m_settings.insert("kraken_model",m_krakenModel->text());m_settings.insert("structure_python",m_structure->text());
        request({{"action","settings-save"},{"settings",m_settings}});
    }
    void reviewTab(QTabWidget* tabs){
        auto layout=tab(tabs,"Sayfa kontrolü ve kuyruk");auto row=new QHBoxLayout;layout->addLayout(row);m_jobs=new QComboBox;row->addWidget(m_jobs,1);
        button(row,"Kitapları yenile",[this]{refreshJobs();});button(row,"Seçili kitabı öne al",[this]{request({{"action","prioritize"},{"job_id",job().value("id")},{"priority",10}});});
        m_pilot=new QSpinBox;m_pilot->setRange(1,10);m_pilot->setValue(3);row->addWidget(m_pilot);
        button(row,"Seçili kitaptan pilot ekle",[this]{request({{"action","pilot"},{"job",job()},{"count",m_pilot->value()}});});
        auto filter=new QHBoxLayout;layout->addLayout(filter);m_problems=new QCheckBox(tr("Yalnız kontrol bekleyen sorunlu sayfalar"));m_problems->setChecked(true);filter->addWidget(m_problems);
        button(filter,"Sayfaları getir",[this]{m_offset=0;pages();});button(filter,"Önceki 100",[this]{m_offset=qMax(0,m_offset-100);pages();});button(filter,"Sonraki 100",[this]{m_offset+=100;pages();});
        m_pages=new QTreeWidget;m_pages->setObjectName("workspaceReviewPages");m_pages->setRootIsDecorated(false);m_pages->setHeaderLabels({tr("Sayfa"),tr("Neden"),tr("Motor güveni"),tr("Kontrol")});m_pages->setMinimumHeight(160);layout->addWidget(m_pages);
        connect(m_pages,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item,QTreeWidgetItem*){if(!item)return;auto value=item->data(0,Qt::UserRole).toJsonObject();
            QStringList reasons;for(auto entry:value.value("reasons").toArray()){auto r=entry.toObject();reasons<<"["+r.value("severity").toString()+"] "+unified_i18n::display(r.value("detail").toString());}
            auto comparison=value.value("ocr_comparison").toObject();if(!comparison.isEmpty())reasons<<tr("Tesseract karşılaştırması · özgün OCR korunur")+"\n"+comparison.value("text").toString()+comparison.value("error").toString();
            m_reasons->setPlainText(reasons.join('\n'));m_note->setText(value.value("review_note").toString());image(m_before,value.value("source").toString());
            auto path=value.value("preview").toString();if(QFileInfo::exists(path))image(m_after,path);else request({{"action","preview"},{"pdf",value.value("page_pdf")}},[this](QJsonObject const& result){image(m_after,result.value("preview").toString());});
        });
        auto pair=new QHBoxLayout;layout->addLayout(pair);m_before=new QLabel(tr("Kaynak sayfa"));m_after=new QLabel(tr("Son PDF görünümü"));for(auto label:{m_before,m_after}){label->setAlignment(Qt::AlignCenter);label->setMinimumSize(200,220);pair->addWidget(label,1);}
        auto views=new QHBoxLayout;layout->addLayout(views);button(views,"Kaynağı ayrı aç",[this]{if(m_pages->currentItem())open(m_pages->currentItem()->data(0,Qt::UserRole).toJsonObject().value("source").toString());});button(views,"Son PDF sayfasını ayrı aç",[this]{if(m_pages->currentItem())open(m_pages->currentItem()->data(0,Qt::UserRole).toJsonObject().value("page_pdf").toString());});
        m_reasons=new QPlainTextEdit;m_reasons->setReadOnly(true);m_reasons->setMaximumHeight(90);layout->addWidget(m_reasons);m_note=new QLineEdit;m_note->setPlaceholderText(tr("Kontrol notu"));layout->addWidget(m_note);
        button(layout,"Seçili sayfayı kontrol edildi olarak işaretle",[this]{if(!m_pages->currentItem())return;auto row=m_pages->currentItem()->data(0,Qt::UserRole).toJsonObject();request({{"action","review"},{"job",job().value("id")},{"page",row.value("page")},{"sha",row.value("pdf_sha256")},{"note",m_note->text()},{"reviewed",true}},[this](QJsonObject const&){pages();});});
        button(layout,"Seçili kitabın süre ve bellek ölçümlerini aç",[this]{open(job().value("work").toString()+"/metrics.json");});
        button(layout,"Seçili kitabın OCR bekleyen PDF’sini aç",[this]{open(job().value("visual_pdf").toString());});
    }
    void pages(){if(job().isEmpty())return;request({{"action","pages"},{"job",job().value("id")},{"offset",m_offset},{"limit",100},{"problems",m_problems->isChecked()}},[this](QJsonObject const& result){
        m_pages->clear();m_rows=result.value("pages").toArray();for(auto value:m_rows){auto row=value.toObject();QStringList reasons;for(auto r:row.value("reasons").toArray())reasons<<r.toObject().value("code").toString();auto item=new QTreeWidgetItem(m_pages,{QString::number(row.value("page").toInt()),reasons.join(", "),row.value("confidence").isDouble()?QString::number(row.value("confidence").toDouble()*100,'f',1)+"%":QString(),row.value("reviewed").toBool()?tr("Kontrol edildi"):tr("Bekliyor")});item->setData(0,Qt::UserRole,row);}
        m_status->setText(tr("%1 kayıt · gösterilen %2–%3").arg(result.value("total").toInt()).arg(m_rows.isEmpty()?0:m_offset+1).arg(m_offset+m_rows.size()));
    });}
    void pdfTab(QTabWidget* tabs){
        auto layout=tab(tabs,"PDF ve dışa aktarma");auto form=new QFormLayout;layout->addLayout(form);m_pdf=new QLineEdit;form->addRow(tr("Kaynak PDF"),m_pdf);m_range=new QLineEdit;m_range->setPlaceholderText("1-5,8,12-20");form->addRow(tr("Sayfa aralığı · boşsa tümü"),m_range);
        button(layout,"PDF seç",[this]{auto path=QFileDialog::getOpenFileName(this,tr("Kaynak PDF"),{},"PDF (*.pdf)");if(!path.isEmpty())m_pdf->setText(path);});
        button(layout,"PDF sayfalarını analiz et",[this]{request({{"action","pdf-probe"},{"source",m_pdf->text()},{"pages",m_range->text()}},[this](QJsonObject const& r){int preserved=0;for(auto p:r.value("pages").toArray())if(p.toObject().value("recommended_route")=="preserve")++preserved;m_status->setText(tr("%1 sayfa seçildi · %2 sayfanın mevcut metni korunacak · %3 sayfaya OCR eklenecek.").arg(r.value("selected").toInt()).arg(preserved).arg(r.value("selected").toInt()-preserved));open(r.value("preview").toString());});});
        button(layout,"Metni ve çizimleri koruyarak OCR ekle",[this]{auto value=currentOptions?currentOptions():QJsonObject();value.insert("action","pdf-smart");value.insert("source",m_pdf->text());value.insert("pages",m_range->text());auto target=outputFolder("SmartPDF")+".pdf";value.insert("destination",target);request(value,[this,target](QJsonObject const&){m_resultPdf=target;m_status->setText(tr("PDF hazır: ")+target);});});
        auto note=new QLabel(tr("Bu yol geometriyi değiştirmez. Metni bulunan sayfalar korunur; metinsiz sayfalara görünmez OCR katmanı eklenir. Düzeltme için PDF’den kitap ekle sekmesini kullanın."));note->setWordWrap(true);layout->addWidget(note);
        button(layout,"Üç boyut profilini örnek sayfalarda karşılaştır",[this]{request({{"action","compression-preview"},{"source",m_pdf->text()},{"destination",outputFolder("SizePreview")},{"pages",m_range->text().isEmpty()?"1-3":m_range->text()}},[this](QJsonObject const& result){m_sizes->clear();for(auto value:result.value("profiles").toArray()){auto row=value.toObject();auto item=new QTreeWidgetItem(m_sizes,{row.value("profile").toString(),QString::number(row.value("after_bytes").toDouble()/1048576.,'f',2),row.value("lossless").toBool(row.value("renders_identical").toBool())?tr("Kayıpsız"):tr("Görüntü değişir"),row.value("pdf").toString()});item->setData(0,Qt::UserRole,row.value("pdf").toString());}});});
        m_sizes=new QTreeWidget;m_sizes->setRootIsDecorated(false);m_sizes->setHeaderLabels({tr("Profil"),"MiB",tr("Görüntü"),tr("PDF · çift tıkla aç")});layout->addWidget(m_sizes);connect(m_sizes,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int){open(item->data(0,Qt::UserRole).toString());});
        button(layout,"Seçili boyut profiliyle yeni PDF oluştur",[this]{auto target=outputFolder("Compressed")+".pdf";request({{"action","compress"},{"source",m_pdf->text()},{"destination",target},{"profile",m_compression->currentData().toString()}},[this,target](QJsonObject const&){m_resultPdf=target;m_status->setText(tr("PDF hazır: ")+target);});});
        button(layout,"TXT / Markdown / ALTO dışa aktar",[this]{auto target=outputFolder("Text");request({{"action","text-export"},{"source",m_pdf->text()},{"destination",target}},[this,target](QJsonObject const&){open(target);});});
        button(layout,"PDF/A oluştur ve veraPDF ile doğrula",[this]{auto target=outputFolder("PDFA")+".pdf";request({{"action","pdfa"},{"source",m_pdf->text()},{"destination",target}},[this,target](QJsonObject const&){m_resultPdf=target;m_status->setText(tr("Doğrulanmış PDF/A: ")+target);});});
        button(layout,"PP-StructureV3 ile tablo / düzen analizi",[this]{auto target=outputFolder("Structure");request({{"action","structure"},{"source",m_pdf->text()},{"destination",target}},[this,target](QJsonObject const&){open(target);});});
        button(layout,"Son oluşturulan PDF’yi aç",[this]{open(m_resultPdf);});layout->addStretch();
    }
    void corpusTab(QTabWidget* tabs){
        auto layout=tab(tabs,"Doğruluk derlemi");auto note=new QLabel(tr("Gerçek doğruluk yalnız insan tarafından kontrol edilmiş referans metinle ölçülür. OCR çıktısı referans olarak kabul edilmez. 250 sayfalık örnek farklı kitap ve uyarı türlerinden seçilir."));note->setWordWrap(true);layout->addWidget(note);
        auto row=new QHBoxLayout;layout->addLayout(row);
        button(row,"250 sayfalık derlem hazırla",[this]{auto target=outputFolder("QualityCorpus");request({{"action","corpus-create"},{"destination",target},{"count",250}},[this](QJsonObject const& result){m_manifest=result.value("manifest").toString();loadCorpus();});});
        button(row,"Derlem aç",[this]{auto path=QFileDialog::getOpenFileName(this,tr("Derlem aç"),{},"JSON (*.json)");if(!path.isEmpty()){m_manifest=path;loadCorpus();}});
        button(row,"Doğrulanmış sayfalarda CER / WER ölç",[this]{if(m_manifest.isEmpty())return;request({{"action","corpus-evaluate"},{"manifest",m_manifest}},[this](QJsonObject const& r){auto t=r.value("totals").toObject();m_quality->setText(tr("Doğrulanmış: %1 / %2 · CER: %3 · WER: %4").arg(r.value("verified").toInt()).arg(r.value("sampled").toInt()).arg(t.value("CER").isDouble()?QString::number(t.value("CER").toDouble()*100,'f',2)+"%":tr("Ölçülmedi")).arg(t.value("WER").isDouble()?QString::number(t.value("WER").toDouble()*100,'f',2)+"%":tr("Ölçülmedi")));});});
        m_quality=new QLabel;layout->addWidget(m_quality);m_samples=new QTreeWidget;m_samples->setRootIsDecorated(false);m_samples->setHeaderLabels({tr("Örnek"),tr("Kitap"),tr("Sayfa"),tr("Doğrulama")});m_samples->setMaximumHeight(150);layout->addWidget(m_samples);
        auto pair=new QHBoxLayout;layout->addLayout(pair);m_corpusImage=new QLabel;m_corpusImage->setAlignment(Qt::AlignCenter);pair->addWidget(m_corpusImage,1);auto text=new QVBoxLayout;pair->addLayout(text,1);
        text->addWidget(new QLabel(tr("OCR önerisi · referans değildir")));m_hypothesis=new QPlainTextEdit;m_hypothesis->setReadOnly(true);text->addWidget(m_hypothesis);
        text->addWidget(new QLabel(tr("Görüntüden doğruladığınız referans metin")));m_reference=new QPlainTextEdit;text->addWidget(m_reference);m_tags=new QLineEdit;m_tags->setPlaceholderText(tr("Etiketler: dipnot, iki sütun, küçük punto…"));layout->addWidget(m_tags);
        auto defects=new QHBoxLayout;layout->addLayout(defects);m_clipped=new QCheckBox(tr("Metin kesilmiş"));m_footnote=new QCheckBox(tr("Dipnot kayıp"));m_splitError=new QCheckBox(tr("Sayfa bölme hatalı"));m_orderError=new QCheckBox(tr("Okuma sırası hatalı"));for(auto box:{m_clipped,m_footnote,m_splitError,m_orderError})defects->addWidget(box);
        button(layout,"Örneğin özgün fotoğrafını aç",[this]{if(m_samples->currentItem())open(m_samples->currentItem()->data(0,Qt::UserRole).toJsonObject().value("original_source").toString());});
        connect(m_samples,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item,QTreeWidgetItem*){if(!item)return;auto value=item->data(0,Qt::UserRole).toJsonObject();image(m_corpusImage,value.value("image").toString());m_reference->setPlainText(value.value("reference_text").toString());QFile f(QFileInfo(m_manifest).dir().filePath(value.value("hypothesis").toString()));if(f.open(QIODevice::ReadOnly))m_hypothesis->setPlainText(QString::fromUtf8(f.readAll()));QStringList tags;for(auto t:value.value("tags").toArray())tags<<t.toString();m_tags->setText(tags.join(", "));});
        connect(m_samples,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item,QTreeWidgetItem*){if(!item)return;auto tags=item->data(0,Qt::UserRole).toJsonObject().value("tags").toArray();m_clipped->setChecked(tags.contains("clipped_text"));m_footnote->setChecked(tags.contains("missing_footnote"));m_splitError->setChecked(tags.contains("wrong_split"));m_orderError->setChecked(tags.contains("wrong_order"));});
        button(layout,"Referans metni doğrulandı olarak kaydet",[this]{if(!m_samples->currentItem())return;auto sample=m_samples->currentItem()->data(0,Qt::UserRole).toJsonObject();QJsonArray tags;for(auto s:m_tags->text().split(',',Qt::SkipEmptyParts))if(!QStringList{"clipped_text","missing_footnote","wrong_split","wrong_order"}.contains(s.trimmed()))tags.append(s.trimmed());if(m_clipped->isChecked())tags.append("clipped_text");if(m_footnote->isChecked())tags.append("missing_footnote");if(m_splitError->isChecked())tags.append("wrong_split");if(m_orderError->isChecked())tags.append("wrong_order");request({{"action","corpus-annotate"},{"manifest",m_manifest},{"id",sample.value("id")},{"reference",m_reference->toPlainText()},{"tags",tags}},[this](QJsonObject const&){loadCorpus();});});
    }
    void loadCorpus(){request({{"action","corpus-open"},{"manifest",m_manifest}},[this](QJsonObject const& result){m_corpus=result.value("pages").toArray();m_samples->clear();for(auto value:m_corpus){auto row=value.toObject();auto item=new QTreeWidgetItem(m_samples,{row.value("id").toString(),row.value("book").toString(),QString::number(row.value("page").toInt()),row.value("verified").toBool()?tr("Doğrulandı"):tr("Bekliyor")});item->setData(0,Qt::UserRole,row);}});}
};
