// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include <QWizard>
#include <QWizardPage>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QJsonObject>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QTemporaryFile>

class UnifiedStartupDialog:public QWizard {
public:
    explicit UnifiedStartupDialog(QJsonObject const& saved,QString output,QWidget* parent=nullptr):QWizard(parent){
        setObjectName("unifiedStartup");setWizardStyle(QWizard::ModernStyle);setWindowTitle(tr("Başlangıç ayarları"));resize(820,650);
        setButtonText(QWizard::BackButton,tr("Geri"));setButtonText(QWizard::NextButton,tr("İleri"));setButtonText(QWizard::FinishButton,tr("Ayarları kaydet"));setButtonText(QWizard::CancelButton,tr("İptal"));
        auto first=new QWizardPage;first->setTitle(tr("İşlem profili ve OCR"));auto form=new QFormLayout(first);
        auto intro=new QLabel(tr("GPU olmayan cihazlarda CPU hızlı seçeneği önerilir. Bütün seçimleri işlem başlamadan değiştirebilirsiniz."));intro->setWordWrap(true);form->addRow(intro);
        m_profile=new QComboBox;m_profile->setObjectName("setupProfile");m_profile->addItem(tr("CPU hızlı · Tesseract Fast · OCR en son"),"cpu_fast");m_profile->addItem(tr("CPU kalite · Tesseract Best · OCR en son"),"cpu_quality");m_profile->addItem(tr("GPU · PaddleOCR · ayrıntılı düzeltme"),"gpu_quality");form->addRow(tr("Hazır profil"),m_profile);
        m_engine=new QComboBox;m_engine->setObjectName("setupOcrEngine");m_engine->addItem("Tesseract · CPU","tesseract");m_engine->addItem("PaddleOCR · GPU","paddle");form->addRow(tr("OCR motoru"),m_engine);
        m_quality=new QComboBox;m_quality->addItem(tr("Fast · daha hızlı, küçük dil modeli"),"fast");m_quality->addItem(tr("Best · daha yavaş, doğruluk öncelikli"),"best");form->addRow(tr("Tesseract modeli"),m_quality);
        m_language=new QComboBox;m_language->addItem(tr("Almanca"),"deu");m_language->addItem(tr("Türkçe"),"tur");m_language->addItem(tr("İngilizce"),"eng");form->addRow(tr("OCR dili"),m_language);
        m_timing=new QComboBox;m_timing->setObjectName("setupOcrTiming");m_timing->addItem(tr("En son · önce görüntülü PDF hazır olsun"),"after");m_timing->addItem(tr("Her sayfayla birlikte"),"during");m_timing->addItem(tr("OCR yapma"),"none");form->addRow(tr("OCR zamanı"),m_timing);
        auto timing=new QLabel(tr("OCR’ı sona almak toplam tanıma işini azaltmaz; görüntülü PDF’yi daha erken incelemenizi sağlar. Fast ve Best doğruluğu kitap türüne göre değişir."));timing->setWordWrap(true);form->addRow(timing);
        m_ui=new QComboBox;m_ui->addItem("Türkçe","tr");m_ui->addItem("English","en");m_ui->setCurrentIndex(qMax(0,m_ui->findData(unified_i18n::selectedLanguage())));form->addRow(tr("Arayüz dili · sonraki açılışta"),m_ui);addPage(first);
        auto processing=new QWizardPage;processing->setTitle(tr("Görüntü, hız ve kalite"));auto process=new QFormLayout(processing);
        m_warp=new QCheckBox(tr("Otomatik eğrilik düzeltme · daha yavaş"));process->addRow(m_warp);
        m_cleanup=new QCheckBox(tr("OCR ile korumalı kenar temizleme · daha yavaş"));process->addRow(m_cleanup);
        m_fit=new QCheckBox(tr("Kitap boyunca yazı yüksekliğini eşitle · ek ölçüm"));process->addRow(m_fit);
        m_scale=new QSpinBox;m_scale->setRange(1,3);m_scale->setSuffix("×");process->addRow(tr("Çıktı ölçeği · 1× hızlı, 3× daha çok bellek"),m_scale);
        m_appearance=new QComboBox;m_appearance->addItem(tr("Experimental çıktısını koru · hızlı"),"native");m_appearance->addItem(tr("Gri ton · eşikleme yok"),"gray");m_appearance->addItem(tr("Yerel yumuşak siyah-beyaz · ek işlem"),"soft_local");process->addRow(tr("PDF görünümü"),m_appearance);
        m_paper=new QComboBox;m_paper->addItem("A4","a4");m_paper->addItem(tr("A4 · yazı yüksekliği eşitle"),"a4_text");m_paper->addItem(tr("Özgün boyut"),"native");process->addRow(tr("Sayfa boyutu"),m_paper);
        m_margin=new QDoubleSpinBox;m_margin->setRange(0,70);m_margin->setValue(10);m_margin->setSuffix(" mm");process->addRow(tr("Kenar payı"),m_margin);
        m_compression=new QComboBox;m_compression->addItem(tr("Kayıpsız"),"lossless");m_compression->addItem(tr("Dengeli · JPEG 92 / 300 DPI"),"balanced");m_compression->addItem(tr("Küçük · JPEG 80 / 200 DPI"),"small");process->addRow(tr("PDF boyut profili"),m_compression);
        m_text=new QCheckBox(tr("TXT / Markdown / ALTO da oluştur"));m_text->setChecked(true);process->addRow(m_text);
        auto manual=new QLabel(tr("Mevcut projelerin elle ayarlanmış geometri ve alanları korunur. Boyut küçültme seçenekleri ayrı PDF üretir; görüntü kalitesini pilot sayfalarda karşılaştırın."));manual->setWordWrap(true);process->addRow(manual);addPage(processing);
        auto disks=new QWizardPage;disks->setTitle(tr("Çalışma diski ve sınırlar"));auto paths=new QFormLayout(disks);
        m_output=new QLineEdit(output.isEmpty()?saved.value("preferred_output").toString():output);m_output->setObjectName("setupOutputFolder");pathRow(paths,tr("PDF çıktı klasörü"),m_output);
        m_temp=new QLineEdit(saved.value("temp_root").toString());m_temp->setObjectName("setupTempFolder");pathRow(paths,tr("Geçici çalışma klasörü · boşsa sistem"),m_temp);
        m_quota=new QDoubleSpinBox;m_quota->setRange(0,100000);m_quota->setValue(saved.value("cache_quota_gib").toDouble(10));m_quota->setSuffix(" GiB");paths->addRow(tr("Önizleme önbelleği kotası"),m_quota);
        m_reserve=new QDoubleSpinBox;m_reserve->setRange(.25,10000);m_reserve->setValue(saved.value("reserve_gib").toDouble(2));m_reserve->setSuffix(" GiB");paths->addRow(tr("Diskte korunacak boş alan"),m_reserve);
        m_resource=new QComboBox;m_resource->addItem(tr("Dengeli"),"balanced");m_resource->addItem(tr("Sessiz · düşük eşzamanlılık"),"quiet");m_resource->addItem(tr("Hızlı · daha fazla kaynak"),"fast");paths->addRow(tr("Kaynak kullanımı"),m_resource);
        auto scope=new QLabel(tr("Başlangıç ayarları yalnız sonraki işlemlere uygulanır. Programı daha sonra Çalışma merkezi ve PDF / OCR sekmelerinden ayrıntılı ayarlayabilirsiniz. Eksik OCR modelleri OCR motoru / dil paketleri sekmesinden kurulur."));scope->setWordWrap(true);paths->addRow(scope);addPage(disks);
        connect(m_profile,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{profile();});profile();
        if(saved.value("setup_complete").toBool()){
            auto set=[&](QComboBox* box,char const* key){int i=box->findData(saved.value(key).toString());if(i>=0)box->setCurrentIndex(i);};
            set(m_engine,"ocr_engine");set(m_quality,"tess_quality");set(m_language,"language");set(m_timing,"ocr_timing");set(m_appearance,"appearance");set(m_paper,"paper");set(m_compression,"compression_profile");set(m_resource,"resource_profile");
            m_warp->setChecked(saved.value("auto_warp").toBool());m_cleanup->setChecked(saved.value("page_cleanup").toBool());m_fit->setChecked(saved.value("content_fit").toBool());m_scale->setValue(saved.value("native_scale").toInt(1));m_margin->setValue(saved.value("margin_mm").toDouble(10));m_text->setChecked(saved.value("structured_text").toBool(true));
            m_profile->blockSignals(true);m_profile->addItem(tr("Kaydedilmiş özel ayarlar"),"custom");m_profile->setCurrentIndex(m_profile->count()-1);m_profile->blockSignals(false);
        }
    }
    bool validateCurrentPage() override {
        if(currentId()!=2)return true;
        for(auto edit:{m_output,m_temp}){
            auto path=edit->text().trimmed();if(edit==m_temp&&path.isEmpty())continue;
            if(!QDir::isAbsolutePath(path)||!QDir().mkpath(path)){
                QMessageBox::warning(this,tr("Klasör seç"),tr("Yazılabilir, tam bir klasör yolu seçin."));return false;
            }
#ifdef Q_OS_WIN
            if(edit==m_temp&&(path.size()>120||path.toUtf8()!=path.toLatin1())){
                QMessageBox::warning(this,tr("Klasör seç"),tr("Geçici klasör için en çok 120 karakterli, Türkçe karakter içermeyen bir yol seçin."));return false;
            }
#endif
            QTemporaryFile probe(QDir(path).filePath(".stou-write-XXXXXX"));
            if(!probe.open()){QMessageBox::warning(this,tr("Klasör seç"),tr("Seçilen klasöre yazılamıyor."));return false;}
            edit->setText(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
        }
        return true;
    }
    QJsonObject values()const{
        auto lang=m_language->currentData().toString();bool paddle=m_engine->currentData()=="paddle";
        return {{"schema",1},{"setup_complete",true},{"ocr_engine",m_engine->currentData().toString()},{"ocr_device",paddle?"gpu":"cpu"},
            {"tess_quality",m_quality->currentData().toString()},{"language",lang},{"paddle_language",lang=="deu"?"de":lang=="tur"?"tr":"en"},
            {"ocr_timing",m_timing->currentData().toString()},{"auto_warp",m_warp->isChecked()},{"page_cleanup",m_cleanup->isChecked()},{"content_fit",m_fit->isChecked()},
            {"native_scale",m_scale->value()},{"ocr_max_edge",m_scale->value()==1?3000:4200},{"appearance",m_appearance->currentData().toString()},
            {"paper",m_paper->currentData().toString()},{"margin_mm",m_margin->value()},{"compression_profile",m_compression->currentData().toString()},
            {"resource_profile",m_resource->currentData().toString()},{"structured_text",m_text->isChecked()},{"ocr_compare",false},
            {"temp_root",m_temp->text().trimmed()},{"cache_quota_gib",m_quota->value()},{"reserve_gib",m_reserve->value()},
            {"preferred_output",m_output->text().trimmed()},{"ui_language",m_ui->currentData().toString()}};
    }
private:
    QComboBox *m_profile,*m_engine,*m_quality,*m_language,*m_timing,*m_ui,*m_appearance,*m_paper,*m_compression,*m_resource;
    QCheckBox *m_warp,*m_cleanup,*m_fit,*m_text;QSpinBox* m_scale;QDoubleSpinBox *m_margin,*m_quota,*m_reserve;QLineEdit *m_output,*m_temp;
    static QString tr(char const* text){return unified_i18n::text(text);}
    void pathRow(QFormLayout* form,QString const& title,QLineEdit* edit){auto row=new QHBoxLayout;row->addWidget(edit);auto button=new QPushButton(tr("Klasör seç"));row->addWidget(button);form->addRow(title,row);connect(button,&QPushButton::clicked,this,[this,edit,title]{auto value=QFileDialog::getExistingDirectory(this,title,edit->text());if(!value.isEmpty())edit->setText(value);});}
    void profile(){if(m_profile->currentData()=="custom")return;bool fast=m_profile->currentData()=="cpu_fast",gpu=m_profile->currentData()=="gpu_quality";m_engine->setCurrentIndex(gpu?1:0);m_quality->setCurrentIndex(fast?0:1);m_timing->setCurrentIndex(0);m_warp->setChecked(!fast);m_cleanup->setChecked(!fast);m_fit->setChecked(!fast);m_scale->setValue(fast?1:2);m_appearance->setCurrentIndex(fast?0:2);m_paper->setCurrentIndex(fast?0:1);}
};
