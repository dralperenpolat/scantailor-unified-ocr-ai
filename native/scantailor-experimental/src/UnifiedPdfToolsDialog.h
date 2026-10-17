// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include <QWidget>
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
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <functional>

// Kept under the original name for callers; this is now an embedded tools tab.
class UnifiedPdfToolsDialog:public QWidget {
public:
    std::function<bool(QJsonObject const&)> importReady;
    explicit UnifiedPdfToolsDialog(QString root,QWidget* parent):QWidget(parent),m_root(std::move(root)) {
        setObjectName("unifiedPdfTools");
        auto form=new QFormLayout(this);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        auto intro=new QLabel(unified_i18n::text("PDF seç → sayfaları hazırla → kitap kuyruğunda düzelt ve yeniden OCR yap."));
        intro->setWordWrap(true);form->addRow(intro);
        m_mode=new QComboBox;m_mode->setObjectName("pdfToolsMode");
        m_mode->addItem(unified_i18n::text("Sayfaları ScanTailor’da düzeltmek için içe aktar"),"import");
        m_mode->addItem(unified_i18n::text("Görünümü koru · kayıpsız sıkıştır"),"compact");
        m_mode->addItem(unified_i18n::text("Dijital metni koru · yalnız metinsiz sayfalara OCR"),"smart");
        form->addRow(unified_i18n::text("İşlem"),m_mode);
        m_strategy=new QComboBox;m_strategy->setObjectName("pdfImportStrategy");
        m_strategy->addItem(unified_i18n::text("Otomatik · tarama resimlerini özgün çözünürlükte çıkar"),"auto");
        m_strategy->addItem(unified_i18n::text("Bütün sayfaları seçilen DPI ile görüntüye çevir"),"render");
        form->addRow(unified_i18n::text("PDF’den görüntü alma"),m_strategy);
        m_dpi=new QComboBox;m_dpi->setObjectName("pdfImportDpi");
        for(int value:{150,200,300,400,600})m_dpi->addItem(QString::number(value)+" DPI",value);
        m_dpi->setCurrentIndex(2);form->addRow(unified_i18n::text("Görüntüye çevrilen sayfalar"),m_dpi);
        m_split=new QComboBox;m_split->setObjectName("pdfImportSplit");
        m_split->addItem(unified_i18n::text("Tek sayfa · yeniden bölme"),"1");
        m_split->addItem(unified_i18n::text("Her PDF sayfasında iki kitap sayfası"),"2");
        m_split->addItem(unified_i18n::text("Otomatik"),"auto");
        form->addRow(unified_i18n::text("PDF’deki sayfa düzeni"),m_split);
        m_source=new QLineEdit;m_source->setObjectName("pdfToolsSource");
        auto sourceRow=new QHBoxLayout;sourceRow->addWidget(m_source);
        m_browse=new QPushButton(unified_i18n::text("PDF seç ve hazırla"));m_browse->setObjectName("pdfImportBrowse");sourceRow->addWidget(m_browse);
        form->addRow(unified_i18n::text("Kaynak PDF"),sourceRow);
        m_range=new QLineEdit;m_range->setObjectName("pdfImportPageRange");m_range->setPlaceholderText("1-5,8,12-20");form->addRow(unified_i18n::text("Sayfa aralığı · boşsa tümü"),m_range);
        m_language=new QComboBox;m_language->addItem(unified_i18n::text("Almanca"),"deu");m_language->addItem(unified_i18n::text("Türkçe"),"tur");m_language->addItem(unified_i18n::text("İngilizce"),"eng");form->addRow(unified_i18n::text("PDF OCR dili"),m_language);
        auto inspect=new QPushButton(unified_i18n::text("Kaynak PDF’yi önizle"));form->addRow(inspect);connect(inspect,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(m_source->text()));});
        m_destination=new QLineEdit(unified_runtime::data(m_root)+"/projects/PDFImports");m_destination->setObjectName("pdfToolsDestination");
        auto outRow=new QHBoxLayout;outRow->addWidget(m_destination);
        m_output=new QPushButton(unified_i18n::text("Klasör seç"));outRow->addWidget(m_output);
        form->addRow(unified_i18n::text("Yeni çıktı klasörü"),outRow);
        auto note=new QLabel(unified_i18n::text("Kaynak PDF korunur. Otomatik yöntemde uygun tarama resimleri yeniden ölçeklenmez; diğer sayfalar seçilen DPI ile hazırlanır. Eski OCR ayrıca saklanır. Hazırlanan kitap normal bölme, geometri, içerik ve OCR aşamalarına girer."));
        note->setWordWrap(true);form->addRow(note);
        m_status=new QLabel(unified_i18n::text("PDF seçerek başla."));m_status->setObjectName("pdfToolsStatus");
        m_status->setWordWrap(true);m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);form->addRow(m_status);
        m_progress=new QProgressBar;m_progress->setObjectName("pdfImportProgress");form->addRow(m_progress);
        auto buttons=new QHBoxLayout;form->addRow(buttons);
        m_start=new QPushButton(unified_i18n::text("Başlat / içe aktarmaya devam et"));m_start->setObjectName("pdfToolsStart");buttons->addWidget(m_start);
        m_stop=new QPushButton(unified_i18n::text("Durdur"));m_stop->setObjectName("pdfImportStop");buttons->addWidget(m_stop);
        m_add=new QPushButton(unified_i18n::text("Hazır kitabı kuyruğa ekle"));m_add->setObjectName("pdfImportAddReady");m_add->hide();form->addRow(m_add);
        connect(m_browse,&QPushButton::clicked,[this]{
            auto paths=QFileDialog::getOpenFileNames(this,unified_i18n::text("İşlenecek PDF’ler"),{},"PDF (*.pdf *.PDF)");
            if(!paths.isEmpty()){auto first=paths.takeFirst();m_sources=paths;selectSource(first,m_mode->currentData()=="import");}
        });
        connect(m_output,&QPushButton::clicked,[this]{auto path=QFileDialog::getExistingDirectory(this,unified_i18n::text("Yeni çıktı klasörü"),m_destination->text());if(!path.isEmpty())m_destination->setText(path);});
        connect(m_stop,&QPushButton::clicked,[this]{
            if(!running())return;
            m_cancelled=true;m_sources.clear();m_process.kill();
            m_status->setText(unified_i18n::text("Durduruldu. İçe aktarmada bitmiş sayfalar saklandı; Başlat ile devam edebilirsin."));
        });
        connect(m_mode,qOverload<int>(&QComboBox::currentIndexChanged),[this]{updateControls();});
        connect(m_start,&QPushButton::clicked,[this]{start();});
        connect(m_add,&QPushButton::clicked,[this]{addReady();});
        connect(&m_process,&QProcess::readyReadStandardOutput,[this]{receive();});
        connect(&m_process,&QProcess::readyReadStandardError,[this]{m_errors+=m_process.readAllStandardError();if(m_errors.size()>4000)m_errors=m_errors.right(4000);});
        connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),[this](int code,QProcess::ExitStatus){
            receive();updateControls();
            if(code&&!m_cancelled)m_status->setText(unified_i18n::text("İşlem tamamlanamadı: ")+m_lastError+QString::fromUtf8(m_errors.right(1000)));
            if(!m_completed)m_progress->setRange(0,1);
            if(!code&&m_completed&&!m_cancelled&&!m_sources.isEmpty())QTimer::singleShot(0,this,[this]{selectSource(m_sources.takeFirst(),true);});
        });
        connect(&m_process,&QProcess::errorOccurred,[this](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart){updateControls();m_progress->setRange(0,1);m_status->setText(m_process.errorString());}
        });
        updateControls();
    }
    ~UnifiedPdfToolsDialog(){if(running()){m_process.kill();m_process.waitForFinished(2000);}}
    bool running()const{return m_process.state()!=QProcess::NotRunning;}
    void stop(){m_stop->click();}
    void useOutputFolder(QString const& path){if(!running())m_destination->setText(path);}
    void setMode(QString const& mode){if(!running()){int i=m_mode->findData(mode);if(i>=0)m_mode->setCurrentIndex(i);}}
    bool selectSource(QString const& path,bool prepare=true){
        if(running()){m_status->setText(unified_i18n::text("Bir PDF zaten hazırlanıyor. Bitmesini bekleyin veya Durdur’u kullanın."));return false;}
        m_source->setText(QFileInfo(path).absoluteFilePath());
        if(prepare)start();
        return true;
    }
private:
    QString m_root,m_lastError,m_runSplit;QStringList m_sources;
    QLineEdit *m_source,*m_destination,*m_range;
    QComboBox *m_mode,*m_dpi,*m_strategy,*m_split,*m_language;
    QPushButton *m_start,*m_stop,*m_browse,*m_output,*m_add;
    QLabel* m_status;QProgressBar* m_progress;
    QProcess m_process;QByteArray m_buffer,m_errors;QJsonObject m_pending;
    bool m_cancelled=false,m_completed=false;
    void updateControls(){
        const bool idle=!running(),importing=m_mode->currentData()=="import";
        for(QWidget* widget:QList<QWidget*>{m_start,m_mode,m_source,m_destination,m_browse,m_output,m_range,m_language})widget->setEnabled(idle);
        for(auto combo:{m_dpi,m_strategy,m_split})combo->setEnabled(idle&&importing);
        m_stop->setEnabled(!idle);m_add->setEnabled(idle);
        m_browse->setText(unified_i18n::text(importing?"PDF seç ve hazırla":"PDF seç"));
    }
    void addReady(){
        if(m_pending.isEmpty())return;
        const QString summary=unified_i18n::text("%1 sayfa hazır · %2 özgün resim · %3 sayfa görüntüsü.")
            .arg(m_pending.value("pages").toInt()).arg(m_pending.value("extracted").toInt()).arg(m_pending.value("rendered").toInt());
        if(importReady&&importReady(m_pending)){
            m_pending={};m_add->hide();
            m_status->setText(summary+"\n"+unified_i18n::text("Kitap kuyruğa eklendi. İşler sekmesinden Projeleri hazırla ile devam edin."));
        }else{
            m_add->show();
            m_status->setText(summary+"\n"+unified_i18n::text("Kuyruğa henüz eklenemedi. Kuyruk boşta olduğunda Hazır kitabı kuyruğa ekle düğmesini kullanın.")+"\n"+m_pending.value("folder").toString());
        }
    }
    void start(){
        if(running())return;
        const QFileInfo input(m_source->text());
        if(!input.isFile()||input.suffix().compare("pdf",Qt::CaseInsensitive)!=0){m_status->setText(unified_i18n::text("Kaynak PDF bulunamadı."));return;}
        if(m_destination->text().trimmed().isEmpty()){m_status->setText(unified_i18n::text("Yeni çıktı klasörü seçin."));return;}
        QString source=input.absoluteFilePath(),mode=m_mode->currentData().toString(),out=m_destination->text();
        if(mode=="compact")out=QDir(out).filePath(input.completeBaseName()+" - Kucuk_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+".pdf");
        if(mode=="smart")out=QDir(out).filePath(input.completeBaseName()+" - OCR_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+".pdf");
        auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("PYTHONIOENCODING","utf-8");env.insert("PYTHONPATH",m_root+"/src");
        m_process.setProcessEnvironment(env);m_process.setWorkingDirectory(m_root);
        m_buffer.clear();m_errors.clear();m_lastError.clear();m_pending={};m_add->hide();m_cancelled=false;m_completed=false;
        m_runSplit=m_split->currentData().toString();m_progress->setRange(0,0);m_status->setText(unified_i18n::text("PDF okunuyor…"));
        m_process.start(unified_runtime::python(m_root),{"-u","-m","scantailor_ocr_unified.pdf_tools",mode,source,out,
            "--dpi",QString::number(m_dpi->currentData().toInt()),"--strategy",m_strategy->currentData().toString(),"--pages",m_range->text(),"--language",m_language->currentData().toString()});
        updateControls();
    }
    void receive(){
        m_buffer+=m_process.readAllStandardOutput();int end;
        while((end=m_buffer.indexOf('\n'))>=0){
            auto line=m_buffer.left(end);m_buffer.remove(0,end+1);if(!line.startsWith("STOUPDF "))continue;
            auto value=QJsonDocument::fromJson(line.mid(8)).object();
            if(value.contains("total")){
                m_progress->setRange(0,value.value("total").toInt());m_progress->setValue(value.value("page").toInt());
                if(!m_cancelled)m_status->setText(unified_i18n::display(value.value("stage").toString())+QString(" · %1 / %2").arg(value.value("page").toInt()).arg(value.value("total").toInt()));
            }
            if(value.contains("error")){m_lastError=unified_i18n::display(value.value("error").toString());m_status->setText(m_lastError);}
            if(value.value("ok").toBool()&&!m_cancelled){
                m_completed=true;
                if(value.value("mode")=="import"){
                    value.insert("split",m_runSplit);m_pending=value;addReady();
                }else if(value.value("mode")=="smart")m_status->setText(unified_i18n::text("PDF hazır: ")+value.value("pdf").toString());
                else m_status->setText(unified_i18n::text("Doğrulandı: %1 → %2 MB. Metin ve 150 DPI kontrol görüntüleri aynı.\n%3")
                    .arg(value.value("before_bytes").toDouble()/1048576.,0,'f',2).arg(value.value("after_bytes").toDouble()/1048576.,0,'f',2).arg(value.value("pdf").toString()));
            }
        }
    }
};
