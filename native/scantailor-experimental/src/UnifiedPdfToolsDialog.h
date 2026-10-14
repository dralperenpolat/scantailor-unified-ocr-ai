// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <functional>
class UnifiedPdfToolsDialog:public QDialog {
public:
    std::function<bool(QJsonObject const&)> importReady;
    explicit UnifiedPdfToolsDialog(QString root,QWidget* parent):QDialog(parent),m_root(std::move(root)){
        setObjectName("unifiedPdfTools");setWindowTitle(unified_i18n::text("Mevcut PDF’leri işle"));resize(690,440);auto form=new QFormLayout(this);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        m_source=new QLineEdit;m_source->setObjectName("pdfToolsSource");auto sourceRow=new QHBoxLayout;sourceRow->addWidget(m_source);auto browse=new QPushButton(unified_i18n::text("PDF seç"));sourceRow->addWidget(browse);form->addRow(unified_i18n::text("Kaynak PDF"),sourceRow);
        m_mode=new QComboBox;m_mode->setObjectName("pdfToolsMode");m_mode->addItem(unified_i18n::text("Sayfaları ScanTailor’da düzeltmek için içe aktar"),"import");m_mode->addItem(unified_i18n::text("Görünümü koru · kayıpsız sıkıştır"),"compact");form->addRow(unified_i18n::text("İşlem"),m_mode);
        m_dpi=new QComboBox;for(int value:{150,200,300,400,600})m_dpi->addItem(QString::number(value)+" DPI",value);m_dpi->setCurrentIndex(2);form->addRow(unified_i18n::text("Düzenleme kopyası"),m_dpi);
        m_destination=new QLineEdit(unified_runtime::data(m_root)+"/projects/PDFImports");m_destination->setObjectName("pdfToolsDestination");auto outRow=new QHBoxLayout;outRow->addWidget(m_destination);auto output=new QPushButton(unified_i18n::text("Klasör seç"));outRow->addWidget(output);form->addRow(unified_i18n::text("Yeni çıktı klasörü"),outRow);
        auto note=new QLabel(unified_i18n::text("Kaynak PDF korunur. İçe aktarma, metni ve konumlarını ayrıca saklar; sayfaları seçilen çözünürlükte PNG’ye çevirir. Geometri değişince OCR yeniden yapılır. Metin / vektör PDF’yi görüntüye çevirmek dosyayı büyütebilir; yalnız küçültmek için kayıpsız sıkıştırmayı seç. DPI artırmak kaynakta olmayan ayrıntıyı üretmez."));note->setWordWrap(true);form->addRow(note);
        m_status=new QLabel(unified_i18n::text("PDF seçerek başla."));m_status->setObjectName("pdfToolsStatus");m_status->setWordWrap(true);m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);form->addRow(m_status);m_progress=new QProgressBar;form->addRow(m_progress);
        auto buttons=new QHBoxLayout;form->addRow(buttons);m_start=new QPushButton(unified_i18n::text("Başlat / içe aktarmaya devam et"));m_start->setObjectName("pdfToolsStart");buttons->addWidget(m_start);auto stop=new QPushButton(unified_i18n::text("Durdur"));buttons->addWidget(stop);auto close=new QPushButton(unified_i18n::text("Kapat"));buttons->addWidget(close);
        connect(browse,&QPushButton::clicked,[this]{auto p=QFileDialog::getOpenFileName(this,unified_i18n::text("İşlenecek PDF"),{},"PDF (*.pdf)");if(!p.isEmpty())m_source->setText(p);});connect(output,&QPushButton::clicked,[this]{auto p=QFileDialog::getExistingDirectory(this,unified_i18n::text("Yeni çıktı klasörü"),m_destination->text());if(!p.isEmpty())m_destination->setText(p);});
        connect(close,&QPushButton::clicked,this,&QDialog::close);connect(stop,&QPushButton::clicked,[this]{m_cancelled=true;m_process.kill();m_status->setText(unified_i18n::text("Durduruldu. İçe aktarmada bitmiş sayfalar saklandı; Başlat ile devam edebilirsin."));});
        connect(m_mode,qOverload<int>(&QComboBox::currentIndexChanged),[this]{m_dpi->setEnabled(m_mode->currentData()=="import");});connect(m_start,&QPushButton::clicked,[this]{start();});
        connect(&m_process,&QProcess::readyReadStandardOutput,[this]{receive();});connect(&m_process,&QProcess::readyReadStandardError,[this]{m_errors+=m_process.readAllStandardError();if(m_errors.size()>4000)m_errors=m_errors.right(4000);});
        connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),[this](int code,QProcess::ExitStatus){receive();m_start->setEnabled(true);m_mode->setEnabled(true);if(code&&!m_cancelled)m_status->setText(unified_i18n::text("İşlem tamamlanamadı: ")+m_lastError+unified_i18n::text(m_errors.right(1000)));});
        connect(&m_process,&QProcess::errorOccurred,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){m_start->setEnabled(true);m_mode->setEnabled(true);m_status->setText(m_process.errorString());}});
    }
    ~UnifiedPdfToolsDialog(){if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(2000);}}
    void setMode(QString const& mode){if(m_process.state()==QProcess::NotRunning){int i=m_mode->findData(mode);if(i>=0)m_mode->setCurrentIndex(i);}}
private:
    QString m_root,m_lastError;QLineEdit *m_source,*m_destination;QComboBox *m_mode,*m_dpi;QPushButton* m_start;QLabel* m_status;QProgressBar* m_progress;QProcess m_process;QByteArray m_buffer,m_errors;bool m_cancelled=false;
    void start(){if(m_process.state()!=QProcess::NotRunning)return;QString source=QFileInfo(m_source->text()).absoluteFilePath();if(!QFileInfo::exists(source)){m_status->setText(unified_i18n::text("Kaynak PDF bulunamadı."));return;}QString mode=m_mode->currentData().toString(),out=m_destination->text();if(mode=="compact")out=QDir(out).filePath(QFileInfo(source).completeBaseName()+" - Kucuk_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+".pdf");
        auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("PYTHONIOENCODING","utf-8");m_process.setProcessEnvironment(env);m_process.setWorkingDirectory(m_root);m_buffer.clear();m_errors.clear();m_lastError.clear();m_cancelled=false;m_start->setEnabled(false);m_mode->setEnabled(false);m_progress->setRange(0,0);m_status->setText(unified_i18n::text("PDF okunuyor…"));m_process.start(unified_runtime::python(m_root),{"-u",m_root+"/src/scantailor_ocr_unified/pdf_tools.py",mode,source,out,"--dpi",QString::number(m_dpi->currentData().toInt())});
    }
    void receive(){m_buffer+=m_process.readAllStandardOutput();int end;while((end=m_buffer.indexOf('\n'))>=0){auto line=m_buffer.left(end);m_buffer.remove(0,end+1);if(!line.startsWith("STOUPDF "))continue;auto v=QJsonDocument::fromJson(line.mid(8)).object();if(v.contains("total")){m_progress->setRange(0,v.value("total").toInt());m_progress->setValue(v.value("page").toInt());m_status->setText(unified_i18n::display(v.value("stage").toString())+QString(" · %1 / %2").arg(v.value("page").toInt()).arg(v.value("total").toInt()));}if(v.contains("error")){m_lastError=unified_i18n::display(v.value("error").toString());m_status->setText(m_lastError);}if(v.value("ok").toBool()){if(v.value("mode")=="import"){bool added=importReady&&importReady(v);m_status->setText((added?unified_i18n::text("Kitap kuyruğa eklendi. "):unified_i18n::text("Sayfalar hazır. Kuyruk çalıştığı için otomatik eklenmedi; Fotoğraf klasöründen kitap ekle ile ekleyebilirsin. "))+v.value("folder").toString());}else m_status->setText(unified_i18n::text("Doğrulandı: %1 → %2 MB. Metin ve 150 DPI kontrol görüntüleri aynı.\n%3").arg(v.value("before_bytes").toDouble()/1048576.,0,'f',2).arg(v.value("after_bytes").toDouble()/1048576.,0,'f',2).arg(v.value("pdf").toString()));}}}
};
