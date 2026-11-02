// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDesktopServices>
#include <QUrl>
#include <QSettings>
#include <QDateTime>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <functional>

class UnifiedPdfBatchDialog:public QDialog {
public:
    std::function<void(QString const&)> stageStatus;
    explicit UnifiedPdfBatchDialog(QString root,QWidget* parent=nullptr):QDialog(parent),m_root(root){
        setObjectName("pdfBatchDialog");setWindowTitle(tr("Klasörle PDF, OCR ve sıkıştırma"));resize(1120,780);
        auto layout=new QVBoxLayout(this);
        m_current=new QPushButton(tr("Açık kitabın PDF'sini kullan"));m_current->setObjectName("pdfBatchCurrentBook");m_current->hide();layout->addWidget(m_current);
        connect(m_current,&QPushButton::clicked,this,[this]{useCurrentPdf();});
        auto inputs=new QHBoxLayout;layout->addLayout(inputs);
        action(inputs,"PDF ekle","pdfBatchAddFiles",[this]{addInputs(QFileDialog::getOpenFileNames(this,tr("PDF ekle"),{},"PDF (*.pdf)"));});
        action(inputs,"Klasör ve alt klasörleri ekle","pdfBatchAddFolder",[this]{auto path=QFileDialog::getExistingDirectory(this,tr("Klasör ve alt klasörleri ekle"));if(!path.isEmpty())addInputs({path});});
        action(inputs,"Listeyi temizle","pdfBatchClear",[this]{m_inputs.clear();m_sources->clear();m_batch.clear();m_table->clear();refreshButtons();});
        m_sources=new QPlainTextEdit;m_sources->setObjectName("pdfBatchInputs");m_sources->setReadOnly(true);m_sources->setMaximumHeight(75);layout->addWidget(m_sources);
        auto form=new QFormLayout;layout->addLayout(form);
        auto mirrorRow=new QHBoxLayout;m_mirror=new QLineEdit;m_mirror->setObjectName("pdfBatchMirror");m_mirror->setPlaceholderText(tr("Boşsa PDF'nin yanına - küçük eklenir"));mirrorRow->addWidget(m_mirror,1);
        action(mirrorRow,"Ayna klasörü seç","pdfBatchChooseMirror",[this]{auto p=QFileDialog::getExistingDirectory(this,tr("Ayna klasörü seç"));if(!p.isEmpty())m_mirror->setText(p);});
        action(mirrorRow,"Yanına kaydet","pdfBatchAdjacent",[this]{m_mirror->clear();});form->addRow(tr("Çıktı konumu"),mirrorRow);
        m_profile=new QComboBox;m_profile->setObjectName("pdfBatchProfile");
        m_profile->addItem(tr("Az sıkıştır · görüntüyü koru"),"lossless");m_profile->addItem(tr("Gerektiğinde gri koru"),"mono_text");m_profile->addItem(tr("Tam sıkıştır · siyah-beyaz"),"mono_all");m_profile->setCurrentIndex(1);form->addRow(tr("Sıkıştırma"),m_profile);
        m_modeNote=new QLabel;m_modeNote->setWordWrap(true);m_modeNote->setObjectName("pdfBatchModeNote");layout->addWidget(m_modeNote);describeMode();
        auto note=new QLabel(tr("OCR bulunmayan sayfalara önce OCR eklenir. Ayna klasöründe alt klasör yapısı korunur. Kaynaklar, siz yerine geçirmeyi seçene kadar değişmez."));note->setWordWrap(true);layout->addWidget(note);
        auto ocr=new QHBoxLayout;
        m_language=new QComboBox;m_language->setObjectName("pdfBatchLanguage");
        for(auto item:QList<QPair<QString,QString>>{{tr("Almanca"),"deu"},{tr("Türkçe"),"tur"},{tr("İngilizce"),"eng"},{tr("Fransızca"),"fra"}})m_language->addItem(item.first,item.second);
        m_engine=new QComboBox;m_engine->setObjectName("pdfBatchEngine");m_engine->addItem("Paddle · GPU","paddle");m_engine->addItem("Tesseract · CPU","tesseract");
        ocr->addWidget(m_language);ocr->addWidget(m_engine);form->addRow(tr("Eksik OCR için dil ve motor"),ocr);
        m_password=new QLineEdit;m_password->setObjectName("pdfBatchPassword");m_password->setEchoMode(QLineEdit::Password);form->addRow(tr("PDF şifresi · gerekiyorsa"),m_password);
        auto commands=new QHBoxLayout;layout->addLayout(commands);
        m_plan=action(commands,"PDF listesini hazırla","pdfBatchPlan",[this]{prepare();});
        m_run=action(commands,"▶ Seçili ayarlarla başlat / devam et","pdfBatchRun",[this]{start();});
        m_pause=action(commands,"Duraklat","pdfBatchPause",[this]{cancel();});m_pause->setEnabled(false);
        m_history=new QComboBox;m_history->setObjectName("pdfBatchHistory");m_history->addItem(tr("Önceki işlemler"));
        for(auto path:QSettings().value("unified/pdf_batch_recent").toStringList())m_history->addItem(QSettings().value("unified/pdf_batch_labels/"+QFileInfo(path).baseName(),QFileInfo(path).baseName()).toString(),path);
        commands->addWidget(m_history,1);
        connect(m_history,qOverload<int>(&QComboBox::activated),this,[this](int index){if(index>0){m_followCurrent=false;request({{"action","pdf-batch-inspect"},{"batch",m_history->itemData(index).toString()}},[this](auto const& result){showBatch(result);});}});
        m_table=new QTreeWidget;m_table->setObjectName("pdfBatchFiles");m_table->setRootIsDecorated(false);m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_table->setHeaderLabels({tr("PDF"),tr("Önce · MiB"),tr("Sonra · MiB"),tr("Küçülme"),tr("OCR sayfası"),tr("Durum")});m_table->header()->setSectionResizeMode(0,QHeaderView::Stretch);layout->addWidget(m_table,1);
        connect(m_table,&QTreeWidget::itemSelectionChanged,this,[this]{refreshButtons();});
        connect(m_table,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem*,int){openResult();});
        auto outputs=new QHBoxLayout;layout->addLayout(outputs);
        action(outputs,"Kaynağı aç","pdfBatchOpenSource",[this]{if(auto item=m_table->currentItem())open(item->data(0,Qt::UserRole).toJsonObject().value("source").toString());});
        action(outputs,"Sonucu aç","pdfBatchOpenResult",[this]{openResult();});
        m_replace=action(outputs,"Seçilenleri ana dosyanın yerine geçir","pdfBatchReplace",[this]{QJsonArray ids;for(auto item:m_table->selectedItems())ids.append(item->data(0,Qt::UserRole).toJsonObject().value("id"));request({{"action","pdf-batch-replace"},{"batch",m_batch},{"ids",ids}},[this](auto const& result){showBatch(result);});});
        m_status=new QLabel(tr("Dosya veya üst klasör seçin; önce birkaç PDF ile deneyebilirsiniz."));m_status->setWordWrap(true);m_status->setObjectName("pdfBatchStatus");layout->addWidget(m_status);
        m_progress=new QProgressBar;m_progress->hide();layout->addWidget(m_progress);
        connect(&m_process,&QProcess::started,this,[this]{m_process.write(QJsonDocument(m_request).toJson(QJsonDocument::Compact)+"\n");m_process.closeWriteChannel();});
        connect(&m_process,&QProcess::readyReadStandardOutput,this,[this]{readOutput();});
        connect(&m_process,&QProcess::readyReadStandardError,this,[this]{m_errors+=m_process.readAllStandardError();m_errors=m_errors.right(4000);});
        connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){readOutput();auto callback=std::move(m_callback);m_callback={};m_busy=false;m_progress->hide();refreshButtons();if(stageStatus)stageStatus(QString::number(m_elapsed.elapsed()/1000.,'f',1)+" s");
            if(code||status!=QProcess::NormalExit||!m_response.value("ok").toBool()){m_status->setText(m_response.value("error").toString(tr("İşlem tamamlanamadı."))+QString::fromUtf8(m_errors.right(1000)));return;}
            if(callback)callback(m_response.value("result").toObject());
        });
        connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){m_busy=false;refreshButtons();m_status->setText(m_process.errorString());}});
        connect(m_mirror,&QLineEdit::textChanged,this,[this]{invalidatePlan();});
        connect(m_profile,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{describeMode();invalidatePlan();});
        auto timer=new QTimer(this);timer->setInterval(1000);connect(timer,&QTimer::timeout,this,[this]{if(m_busy&&stageStatus)stageStatus(QString::number(m_elapsed.elapsed()/1000.,'f',1)+" s");});timer->start();
        refreshButtons();
    }
    ~UnifiedPdfBatchDialog(){disconnect(&m_process,nullptr,this,nullptr);if(m_process.state()!=QProcess::NotRunning){cancel();if(!m_process.waitForFinished(5000)){m_process.kill();m_process.waitForFinished(1000);}}}
    void addInputs(QStringList const& values){if(m_busy||values.isEmpty())return;m_followCurrent=false;for(auto const& path:values)if(!path.isEmpty()&&!m_inputs.contains(path))m_inputs.append(path);m_sources->setPlainText(m_inputs.join('\n'));invalidatePlan();}
    void setCurrentPdf(QString const& path,QString const& title={}){bool changed=path!=m_currentPdf;m_currentPdf=path;m_current->setText(tr("Açık kitabın PDF'sini kullan")+(title.isEmpty()?QString():" · "+title));m_current->setToolTip(path);m_current->setVisible(!path.isEmpty());if(changed&&m_followCurrent&&!m_busy)useCurrentPdf();}
    bool busy()const{return m_busy;}
    void start(){if(m_busy)return;if(m_batch.isEmpty())prepare(true);else run();}
private:
    QString m_root,m_batch,m_currentPdf;QStringList m_inputs;bool m_busy=false,m_followCurrent=true;
    QPlainTextEdit* m_sources;QLineEdit *m_mirror,*m_password;QComboBox *m_profile,*m_language,*m_engine,*m_history;
    QTreeWidget* m_table;QPushButton *m_plan,*m_run,*m_pause,*m_replace,*m_current;QLabel *m_status,*m_modeNote;QProgressBar* m_progress;
    QProcess m_process;QByteArray m_buffer,m_errors;QJsonObject m_request,m_response;
    QElapsedTimer m_elapsed;
    std::unique_ptr<QTemporaryDir> m_temporary;std::function<void(QJsonObject const&)> m_callback;
    static QString tr(char const* text){return unified_i18n::text(text);}
    QPushButton* action(QBoxLayout* layout,char const* title,char const* name,std::function<void()> callback){auto button=new QPushButton(tr(title));button->setObjectName(name);layout->addWidget(button);connect(button,&QPushButton::clicked,this,std::move(callback));return button;}
    static void open(QString const& path){if(!path.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(path));}
    void invalidatePlan(){m_batch.clear();m_table->clear();m_status->setText(tr("Ayarlar hazır. Başlat düğmesiyle seçili ayarlar uygulanır."));refreshButtons();}
    void useCurrentPdf(){if(m_busy)return;m_inputs=m_currentPdf.isEmpty()?QStringList():QStringList{m_currentPdf};m_sources->setPlainText(m_inputs.join('\n'));m_followCurrent=true;invalidatePlan();}
    void describeMode(){auto mode=m_profile->currentData().toString();m_modeNote->setText(mode=="lossless"?tr("Görüntü pikselleri ve OCR korunur; kayıpsız olarak mümkün olan sıkıştırma uygulanır."):mode=="mono_text"?tr("Metin sayfaları siyah-beyaza çevrilir; soluk yazı, resim ve gerekli gri alanlar korunmaya çalışılır. Örnek sayfaları kontrol edin."):tr("Antigravity yöntemi: çözünürlük korunur, gri ve renkler 180 eşiğiyle siyah-beyaza çevrilir. Görüntü değişir; soluk yazıları ve resimleri kontrol edin."));}
    void openResult(){if(auto item=m_table->currentItem()){auto row=item->data(0,Qt::UserRole).toJsonObject();open(row.value(row.value("replaced").toBool()?"source":"destination").toString());}}
    void refreshButtons(){
        for(auto button:findChildren<QPushButton*>())button->setEnabled(!m_busy);
        m_plan->setEnabled(!m_busy&&!m_inputs.isEmpty());m_run->setEnabled(!m_busy&&!m_inputs.isEmpty());m_pause->setEnabled(m_busy&&m_request.value("action")!="pdf-batch-replace");
        bool replace=!m_table->selectedItems().isEmpty();for(auto item:m_table->selectedItems())if(item->data(0,Qt::UserRole).toJsonObject().value("status")!="complete")replace=false;
        m_replace->setEnabled(!m_busy&&replace&&!m_batch.isEmpty());
        for(auto widget:QList<QWidget*>{m_mirror,m_profile,m_language,m_engine,m_password,m_history})widget->setEnabled(!m_busy);
    }
    void prepare(bool startAfter=false){if(m_inputs.isEmpty()){m_status->setText(tr("Önce bir PDF veya klasör ekleyin."));return;}QJsonArray inputs;for(auto const& path:m_inputs)inputs.append(path);request({{"action","pdf-batch-create"},{"inputs",inputs},{"mirror_root",m_mirror->text().trimmed()},{"profile",m_profile->currentData().toString()}},[this,startAfter](auto const& result){showBatch(result);remember(result);if(startAfter)run();});}
    void run(){
        QString language=m_language->currentData().toString();QString paddle=language=="deu"?"de":language=="tur"?"tr":language=="fra"?"fr":"en";
        request({{"action","pdf-batch-run"},{"batch",m_batch},{"language",language},{"paddle_language",paddle},{"ocr_engine",m_engine->currentData().toString()},{"ocr_device",m_engine->currentData()=="paddle"?"gpu":"cpu"},{"password",m_password->text()}},[this](auto const& result){showBatch(result);});
    }
    void request(QJsonObject value,std::function<void(QJsonObject const&)> callback){
        if(m_busy)return;m_temporary.reset(new QTemporaryDir);if(!m_temporary->isValid()){m_status->setText(tr("Geçici çalışma klasörü oluşturulamadı."));return;}
        value.insert("root",m_root);value.insert("cancel_file",m_temporary->filePath("cancel"));m_request=value;m_response={};m_callback=std::move(callback);m_buffer.clear();m_errors.clear();m_busy=true;m_elapsed.start();refreshButtons();m_progress->setRange(0,0);m_progress->show();
        auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONPATH",m_root+"/src");env.insert("PYTHONUTF8","1");m_process.setProcessEnvironment(env);m_process.setWorkingDirectory(m_root);
        m_status->setText(tr("İşleniyor…"));m_process.start(unified_runtime::python(m_root),{"-u","-m","scantailor_ocr_unified.workspace_service"});
    }
    void cancel(){if(!m_temporary)return;QFile file(m_temporary->filePath("cancel"));file.open(QIODevice::WriteOnly);file.write("cancel");file.close();}
    void readOutput(){
        m_buffer+=m_process.readAllStandardOutput();int end;
        while((end=m_buffer.indexOf('\n'))>=0){auto line=m_buffer.left(end).trimmed();m_buffer.remove(0,end+1);
            if(line.startsWith("STOUWORK "))m_response=QJsonDocument::fromJson(line.mid(9)).object();
            else if(line.startsWith("STOUWORKPROGRESS ")){auto event=QJsonDocument::fromJson(line.mid(17)).object();
                m_status->setText(event.value("source").toString()+"\n"+unified_i18n::display(event.value("stage").toString())+QString(" · %1/%2").arg(event.value("page").toInt()).arg(event.value("total").toInt()));
                auto total=event.value("total").toInt();m_progress->setRange(0,total);if(total)m_progress->setValue(event.value("page").toInt());
                if(event.contains("job"))for(int i=0;i<m_table->topLevelItemCount();++i){auto item=m_table->topLevelItem(i);auto row=item->data(0,Qt::UserRole).toJsonObject();if(row.value("id")==event.value("job")){auto change=event.value("result").toObject();for(auto it=change.begin();it!=change.end();++it)row.insert(it.key(),it.value());if(event.contains("status"))row.insert("status",event.value("status"));if(event.contains("error"))row.insert("error",event.value("error"));updateRow(item,row);item->setText(5,event.value("stage").toString());break;}}
            }
        }
    }
    QString stateName(QString const& state){if(state=="complete")return tr("Hazır");if(state=="replaced")return tr("Ana dosyanın yerine geçirildi");if(state=="error")return tr("Hata");if(state=="running")return tr("İşleniyor…");return tr("Bekliyor");}
    void updateRow(QTreeWidgetItem* item,QJsonObject const& row){item->setData(0,Qt::UserRole,row);item->setText(0,row.value("relative").toString());item->setToolTip(0,row.value("source").toString());item->setToolTip(5,row.value("error").toString());
        item->setText(1,QString::number(row.value("before_bytes").toDouble()/1048576.,'f',2));item->setText(2,row.contains("after_bytes")?QString::number(row.value("after_bytes").toDouble()/1048576.,'f',2):QString());item->setText(3,row.contains("saved_percent")?QString::number(row.value("saved_percent").toDouble(),'f',1)+"%":QString());item->setText(4,row.contains("ocr_pages")?QString::number(row.value("ocr_pages").toInt()):QString());item->setText(5,stateName(row.value("status").toString()));}
    void showBatch(QJsonObject const& result){QSignalBlocker mirrorBlock(m_mirror),profileBlock(m_profile);m_batch=result.value("batch").toString();auto settings=result.value("settings").toObject();auto ocr=settings.value("ocr_settings").toObject();if(!ocr.isEmpty()){int language=m_language->findData(ocr.value("language").toString()),engine=m_engine->findData(ocr.value("ocr_engine").toString());if(language>=0)m_language->setCurrentIndex(language);if(engine>=0)m_engine->setCurrentIndex(engine);}m_inputs.clear();for(auto value:settings.value("inputs").toArray())m_inputs.append(value.toString());m_sources->setPlainText(m_inputs.join('\n'));m_mirror->setText(settings.value("output_root").toString());m_profile->setCurrentIndex(qMax(0,m_profile->findData(settings.value("profile").toString())));describeMode();m_table->clear();int done=0,errors=0;
        for(auto value:result.value("jobs").toArray()){auto row=value.toObject();auto item=new QTreeWidgetItem(m_table);updateRow(item,row);if(row.value("status")=="complete"||row.value("status")=="replaced")++done;if(row.value("status")=="error")++errors;}
        m_status->setText(tr("%1 PDF · %2 tamamlandı · %3 hata").arg(m_table->topLevelItemCount()).arg(done).arg(errors));refreshButtons();}
    void remember(QJsonObject const& result){auto recent=QSettings().value("unified/pdf_batch_recent").toStringList();recent.removeAll(m_batch);recent.prepend(m_batch);while(recent.size()>50)recent.removeLast();QSettings().setValue("unified/pdf_batch_recent",recent);QString label=QDateTime::currentDateTime().toString("dd.MM.yyyy HH:mm")+QString(" · %1 PDF · ").arg(result.value("jobs").toArray().size())+m_profile->currentText();QSettings().setValue("unified/pdf_batch_labels/"+QFileInfo(m_batch).baseName(),label);m_history->insertItem(1,label,m_batch);}
};
