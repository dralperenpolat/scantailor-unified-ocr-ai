// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include <QWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalBlocker>

class UnifiedOcrOptions : public QWidget {
public:
    explicit UnifiedOcrOptions(QString const& root,QWidget* parent=nullptr):QWidget(parent),m_root(root) {
        auto form=new QFormLayout(this);form->setRowWrapPolicy(QFormLayout::WrapLongRows);QSettings s;
        m_engine=new QComboBox;m_engine->addItem("Tesseract · CPU","tesseract");m_engine->addItem("PaddleOCR · PP-OCRv5","paddle");form->addRow(unified_i18n::text("OCR motoru"),m_engine);
        m_device=new QComboBox;m_device->addItem("CPU","cpu");m_device->addItem("NVIDIA GPU · CUDA","gpu");form->addRow(unified_i18n::text("PaddleOCR cihazı"),m_device);
        m_paddleLang=new QComboBox;
        for(auto p:QList<QPair<QString,QString>>{{"Almanca","de"},{"Türkçe","tr"},{"İngilizce","en"},{"Fransızca","fr"},{"İtalyanca","it"},{"İspanyolca","es"},{"Portekizce","pt"},{"Felemenkçe","nl"},{"Lehçe","pl"},{"Rusça","ru"},{"Ukraynaca","uk"},{"Arapça","ar"},{"Farsça","fa"},{"Yunanca","el"},{"Çince","ch"},{"Japonca","japan"},{"Korece","korean"}})m_paddleLang->addItem(unified_i18n::display(p.first),p.second);
        form->addRow(unified_i18n::text("PaddleOCR dili"),m_paddleLang);
        m_quality=new QComboBox;m_quality->addItem(unified_i18n::text("Tesseract Best · kalite"),"best");m_quality->addItem(unified_i18n::text("Tesseract Fast · hız"),"fast");form->addRow(unified_i18n::text("Tesseract model paketi"),m_quality);
        m_codes=new QLineEdit(s.value("unified/ocr_languages","deu").toString());form->addRow(unified_i18n::text("Tesseract dilleri · + ile birleştir"),m_codes);
        auto search=new QLineEdit;search->setPlaceholderText(unified_i18n::text("Dil adı / kodu ara…"));form->addRow(search);
        m_models=new QTreeWidget;m_models->setHeaderLabels({unified_i18n::text("OCR'da kullanılacak dil"),unified_i18n::text("Durum"),unified_i18n::text("İndirme boyutu")});m_models->setRootIsDecorated(false);m_models->setMaximumHeight(185);m_models->setColumnWidth(0,290);form->addRow(m_models);
        connect(search,&QLineEdit::textChanged,[this](QString const& text){for(int i=0;i<m_models->topLevelItemCount();++i){auto row=m_models->topLevelItem(i);row->setHidden(!row->text(0).contains(text,Qt::CaseInsensitive));}});
        connect(m_models,&QTreeWidget::itemChanged,[this](QTreeWidgetItem*,int){QStringList codes;for(int i=0;i<m_models->topLevelItemCount();++i){auto row=m_models->topLevelItem(i);if(row->checkState(0)==Qt::Checked)codes<<row->data(0,Qt::UserRole).toString();}m_codes->setText(codes.join('+'));});
        auto buttons=new QGridLayout;form->addRow(buttons);int actionIndex=0;
        auto action=[&](QString text,auto callback){auto button=new QPushButton(text);buttons->addWidget(button,actionIndex/2,actionIndex%2);++actionIndex;connect(button,&QPushButton::clicked,callback);};
        action(unified_i18n::text("Seçilen eksik dilleri indir"),[this]{manage({"download","--quality",m_quality->currentData().toString(),"--codes",m_codes->text().trimmed()});});
        action(unified_i18n::text("Dil kataloğunu güncelle"),[this]{manage({"catalog"});});
        action(unified_i18n::text("PaddleOCR cihaz paketini kur"),[this]{manage({"install-paddle","--device",m_device->currentData().toString()});});
        action(unified_i18n::text("Paddle cihazını test et"),[this]{manage({"probe","--device",m_device->currentData().toString()});});
        m_status=new QLabel;m_status->setWordWrap(true);form->addRow(m_status);
        auto note=new QLabel(unified_i18n::text("Yalnız seçilen diller indirilir. Belgeler bilgisayarda işlenir. PaddleOCR ilk kullanımda seçilen dilin modelini indirir; cihaz paketi ayrıca gerekir. GPU paketi birkaç GB olabilir.\nMotor seçimi görünür resmi değiştirmez. Paddle'ın ikinci sayfa düzleştirmesi kapalıdır: OCR kutuları mevcut geometriye tam oturur. Tesseract GPU kullanmaz."));note->setWordWrap(true);form->addRow(note);
        m_log=new QPlainTextEdit;m_log->setReadOnly(true);m_log->setMaximumHeight(110);m_log->setMaximumBlockCount(500);form->addRow(m_log);
        auto saved=[&](QComboBox* box,char const* key){int i=box->findData(s.value(key));if(i>=0)box->setCurrentIndex(i);};
        saved(m_engine,"unified/ocr_engine");saved(m_device,"unified/ocr_device");saved(m_quality,"unified/tess_quality");saved(m_paddleLang,"unified/paddle_language");
        connect(m_quality,qOverload<int>(&QComboBox::currentIndexChanged),[this]{reload();});
        auto enabled=[this]{bool paddle=m_engine->currentData()=="paddle";m_device->setEnabled(paddle);m_paddleLang->setEnabled(paddle);m_codes->setEnabled(!paddle);m_quality->setEnabled(!paddle);m_models->setEnabled(!paddle);};
        connect(m_engine,qOverload<int>(&QComboBox::currentIndexChanged),enabled);enabled();
        for(auto box:{m_engine,m_device,m_quality,m_paddleLang})connect(box,qOverload<int>(&QComboBox::currentIndexChanged),[this]{options();});
        connect(m_codes,&QLineEdit::textChanged,[this]{
            options();QSignalBlocker block(m_models);auto codes=m_codes->text().split('+');
            for(int i=0;i<m_models->topLevelItemCount();++i){auto row=m_models->topLevelItem(i);row->setCheckState(0,codes.contains(row->data(0,Qt::UserRole).toString())?Qt::Checked:Qt::Unchecked);}
        });
        connect(&m_manager,&QProcess::readyReadStandardOutput,[this]{read();});
        connect(&m_manager,&QProcess::readyReadStandardError,[this]{m_log->appendPlainText(unified_i18n::text(m_manager.readAllStandardError()));});
        connect(&m_manager,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),[this](int code,QProcess::ExitStatus){read();reload();m_status->setText(code==0?unified_i18n::text("İşlem tamamlandı. Dil ve cihaz durumları güncellendi."):unified_i18n::text("İşlem tamamlanamadı. Ayrıntı aşağıda; mevcut dosyalar korundu."));});
        connect(&m_manager,&QProcess::errorOccurred,[this](QProcess::ProcessError){m_status->setText(m_manager.errorString());});reload();
    }
    bool busy()const{return m_manager.state()!=QProcess::NotRunning;}
    void setOptions(QJsonObject const& values) {
        auto set=[&](QComboBox* box,char const* key){int i=box->findData(values.value(key).toString());if(i>=0)box->setCurrentIndex(i);};
        set(m_engine,"ocr_engine");set(m_device,"ocr_device");set(m_quality,"tess_quality");set(m_paddleLang,"paddle_language");
        if(values.contains("language"))m_codes->setText(values.value("language").toString());
    }
    QJsonObject options()const {
        QJsonObject values{{"ocr_engine",m_engine->currentData().toString()},{"ocr_device",m_device->currentData().toString()},{"tess_quality",m_quality->currentData().toString()},{"paddle_language",m_paddleLang->currentData().toString()},{"language",m_codes->text().trimmed()}};
        QSettings s;for(auto i=values.begin();i!=values.end();++i)s.setValue("unified/"+(i.key()=="language"?"ocr_languages":i.key()),i.value().toString());s.sync();return values;
    }
    QString validation()const {
        auto opts=options();
        if(busy())return unified_i18n::text("OCR paketi işlemi devam ediyor. Tamamlanınca kitabı başlatabilirsiniz.");
        if(opts.value("ocr_engine")=="paddle") {
            QString device=opts.value("ocr_device").toString();
            if(!QFileInfo::exists(unified_runtime::resource(m_root,"paddle-"+device+"-ready.json")))return unified_i18n::text("PaddleOCR cihazı doğrulanmadı. Paketi kurun ve cihaz testini çalıştırın.");
        } else {
            auto codes=opts.value("language").toString().split('+',Qt::SkipEmptyParts);if(codes.isEmpty())return unified_i18n::text("En az bir OCR dili seçin.");
            for(auto code:codes)if(!QFileInfo::exists(unified_runtime::resource(m_root,"tessdata_"+m_quality->currentData().toString()+"/"+code+".traineddata")))return unified_i18n::text("Eksik OCR dili: ")+code+unified_i18n::text(". Önce seçilen dilleri indirin.");
        }
        return {};
    }
private:
    QString m_root;QComboBox *m_engine,*m_device,*m_quality,*m_paddleLang;QLineEdit* m_codes;QTreeWidget* m_models;QLabel* m_status;QPlainTextEdit* m_log;QProcess m_manager;QByteArray m_buffer;
    void reload(){
        QSignalBlocker block(m_models);m_models->clear();QFile f(unified_runtime::resource(m_root,"ocr-catalog.json"));
        if(!f.open(QIODevice::ReadOnly)){m_status->setText(unified_i18n::text("Dil kataloğunu güncelleyin."));return;}
        auto source=QJsonDocument::fromJson(f.readAll()).object().value("sources").toObject().value(m_quality->currentData().toString()).toObject();
        auto selected=m_codes->text().split('+');int installed=0;
        for(auto value:source.value("models").toArray()){auto item=value.toObject();QString code=item.value("code").toString();bool present=QFileInfo::exists(unified_runtime::resource(m_root,"tessdata_"+m_quality->currentData().toString()+"/"+code+".traineddata"));if(present)++installed;auto row=new QTreeWidgetItem(m_models,{unified_i18n::display(item.value("name").toString())+" · "+code,present?unified_i18n::text("Kurulu"):unified_i18n::text("İndirilmedi"),QString::number(item.value("bytes").toDouble()/1024/1024,'f',1)+" MB"});row->setData(0,Qt::UserRole,code);row->setCheckState(0,selected.contains(code)?Qt::Checked:Qt::Unchecked);}
        QString gpu=QFileInfo::exists(unified_runtime::resource(m_root,"paddle-gpu-ready.json"))?unified_i18n::text("GPU testi geçti"):unified_i18n::text("GPU doğrulanmadı");
        m_status->setText(unified_i18n::text("%1 dil / yazı sistemi; %2 kurulu. Paddle: %3.").arg(m_models->topLevelItemCount()).arg(installed).arg(gpu));
    }
    void manage(QStringList args){if(busy())return;options();m_buffer.clear();m_log->clear();m_status->setText(unified_i18n::text("İşlem sürüyor…"));auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");m_manager.setProcessEnvironment(env);m_manager.setWorkingDirectory(m_root);m_manager.start(unified_runtime::python(m_root),QStringList{"-u","-m","scantailor_ocr_unified.ocr_manager"}+args+QStringList{"--root",m_root});}
    void read(){m_buffer+=m_manager.readAllStandardOutput();int end;while((end=m_buffer.indexOf('\n'))>=0){QByteArray line=m_buffer.left(end);m_buffer.remove(0,end+1);if(line.startsWith("STOUMGMT ")){auto o=QJsonDocument::fromJson(line.mid(9)).object();QString message=unified_i18n::display(o.value("message").toString());if(o.contains("percent"))message+=" · "+QString::number(o.value("percent").toInt())+" %";m_status->setText(message);m_log->appendPlainText(message);}else m_log->appendPlainText(unified_i18n::text(line));}}
};
