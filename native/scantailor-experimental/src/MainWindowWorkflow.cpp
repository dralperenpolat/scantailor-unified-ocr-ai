// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "UnifiedWorkflow.h"
#include "UnifiedWorkflowHelp.h"
#include "UnifiedProjectCache.h"
#include "UnifiedRuntime.h"
#include "UnifiedPanel.h"
#include "UnifiedStageReviewBar.h"
#include "StageSequence.h"
#include "ProjectPages.h"
#include "ProjectWriter.h"
#include "ProcessingTaskQueue.h"
#include "WorkerThreadPool.h"
#include "ImageFileInfo.h"
#include "ImageMetadataLoader.h"
#include "SmartFilenameOrdering.h"
#include "PageSequence.h"
#include "BasicImageView.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include "UnifiedQueuePreview.h"
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QDesktopServices>
#include <QUrl>
#include <QUuid>
#include <QTabWidget>
#include <QTextStream>

using unified_workflow::text;

namespace {
QStringList imagesIn(QString const& folder) {
    QStringList paths;QDir dir(folder);for(auto const& name:dir.entryList({"*.jpg","*.jpeg","*.png","*.tif","*.tiff","*.bmp"},QDir::Files))paths<<dir.absoluteFilePath(name);return paths;
}
QLabel* note(QString const& value,QVBoxLayout* layout){auto label=new QLabel(value);label->setWordWrap(true);label->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(label);return label;}
}
void MainWindow::initWorkflow(){
    m_workflowReady=true;
    dockWidgetFilters->setWindowTitle(text("Aşamalar · resimden PDF’ye","Stages · images to PDF"));
    dockWidgetFilters->setMinimumWidth(filterList->minimumWidth()+12);
    // Give the stage list its own full-height column. Native controls get a
    // separate scrollable dock, so larger fonts never hide stage names.
    auto optionsDock=new QDockWidget(text("Seçili aşamanın ayarları","Selected stage settings"),this);optionsDock->setObjectName("workflowOptionsDock");
    scrollArea->setParent(nullptr);optionsDock->setWidget(scrollArea);optionsDock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea,optionsDock);
    m_workflowDirty=new QLabel; m_workflowDirty->setWordWrap(true);m_workflowDirty->setObjectName("workflowDependencyStatus");
    auto stagesLayout=qobject_cast<QVBoxLayout*>(filterList->parentWidget()->layout());
    if(stagesLayout){stagesLayout->addWidget(m_workflowDirty);auto refresh=new QPushButton(text("Çıktıyı ve PDF'yi yenile","Refresh Output and PDF"));refresh->setObjectName("workflowRefreshPdf");stagesLayout->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,[this]{runWorkflowStep("pdf");});stagesLayout->addStretch();}
    connect(filterList,&StageListView::launchStep,this,&MainWindow::runWorkflowStep);
    // Reuse the existing settings widgets, not a second copy of their values.
    m_workflowTools=new QWidget;auto toolsLayout=new QVBoxLayout(m_workflowTools);
    m_workflowTitle=note({},toolsLayout);m_workflowTitle->setObjectName("workflowToolsTitle");
    auto actions=new QHBoxLayout;toolsLayout->addLayout(actions);
    m_workflowRun=new QPushButton;m_workflowRun->setObjectName("workflowRun");actions->addWidget(m_workflowRun);
    connect(m_workflowRun,&QPushButton::clicked,this,[this]{runWorkflowStep(m_workflowStep);});
    m_workflowStop=new QPushButton(text("Durdur","Stop"));m_workflowStop->setObjectName("workflowStop");actions->addWidget(m_workflowStop);m_workflowStop->setEnabled(false);
    connect(m_workflowStop,&QPushButton::clicked,this,&MainWindow::cancelWorkflowProcess);
    auto openResult=new QPushButton(text("Son PDF'yi aç","Open latest PDF"));actions->addWidget(openResult);connect(openResult,&QPushButton::clicked,this,[this]{if(!m_workflowPdf.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(m_workflowPdf));});
    auto folderRow=new QHBoxLayout;toolsLayout->addLayout(folderRow);folderRow->addWidget(new QLabel(text("PDF çıktı klasörü","PDF output folder")));
    auto pdfFolder=new QLineEdit;pdfFolder->setReadOnly(true);pdfFolder->setObjectName("workflowPdfOutputFolder");folderRow->addWidget(pdfFolder,1);
    auto output=new QPushButton(text("Gözat…","Browse…"));output->setObjectName("workflowPdfBrowse");folderRow->addWidget(output);connect(output,&QPushButton::clicked,this,[this]{m_unified->chooseWorkflowOutput();refreshWorkflowState();});
    auto cacheSettings=new QPushButton(text("Önbellek ayarları","Cache settings"));actions->addWidget(cacheSettings);connect(cacheSettings,&QPushButton::clicked,this,[this]{filterList->selectStep("storage");});
    auto nativeSettings=new QPushButton(text("Uygulama ayarları","Application settings"));actions->addWidget(nativeSettings);connect(nativeSettings,&QPushButton::clicked,this,&MainWindow::openSettingsDialog);
    m_workflowStatus=note({},toolsLayout);m_workflowStatus->setObjectName("workflowStatus");
    m_unified->setParent(m_workflowTools);toolsLayout->addWidget(m_unified,1);m_unified->show();
    m_unifiedDock->setWidget(new QWidget);m_unifiedDock->hide();m_unifiedDock->toggleViewAction()->setVisible(false);
    m_unifiedViewStack->addWidget(m_workflowTools);
    m_workflowInput=new QWidget;auto input=new QVBoxLayout(m_workflowInput);input->setAlignment(Qt::AlignTop);
    auto heading=note(text("0 · Girdi — kitabı buradan başlatın","0 · Input — start your book here"),input);auto font=heading->font();font.setPointSize(font.pointSize()+3);font.setBold(true);heading->setFont(font);
    note(text("Resim veya PDF → yön → bölme → geometri → isteğe bağlı AI → içerik → kenarlar → Çıktı → temizlik → PDF → OCR → önbellek temizliği. Kaynaklar korunur.","Images or PDF → orientation → split → geometry → optional AI → content → margins → Output → cleanup → PDF → OCR → cache cleanup. Sources are preserved."),input);
    auto inputButton=[&](QString title,QString name,auto fn){auto b=new QPushButton(title);b->setObjectName(name);b->setMinimumHeight(38);input->addWidget(b);connect(b,&QPushButton::clicked,this,fn);};
    inputButton(text("Resim seç","Select images"),"workflowInputImages",[this]{auto paths=QFileDialog::getOpenFileNames(this,text("Resimler","Images"),{},"Images (*.jpg *.jpeg *.png *.tif *.tiff *.bmp)");if(!paths.empty())importWorkflowImages(paths);});
    inputButton(text("Klasör seç","Select folder"),"workflowInputFolder",[this]{auto path=QFileDialog::getExistingDirectory(this,text("Kitap resimlerinin klasörü","Book image folder"));if(!path.isEmpty())importWorkflowImages(imagesIn(path));});
    inputButton(text("Üst klasör seç","Select parent folder"),"workflowInputParent",[this]{
        auto folder=QFileDialog::getExistingDirectory(this,text("Alt klasörler ayrı kitaplar","Subfolders are separate books"));if(folder.isEmpty())return;
        QDirIterator it(folder,QDir::Dirs|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);int count=0;
        while(it.hasNext()){auto path=it.next();bool skip=QFileInfo(path).isSymLink();for(auto part:QDir(folder).relativeFilePath(path).toLower().split('/'))if(part=="out"||part=="output"||part=="cache"||part.startsWith('.'))skip=true;
            if(!skip&&!imagesIn(path).isEmpty()){m_unified->addWorkflowFolder(path);++count;}}
        showWorkflowTools();m_unified->showToolsTab(0);m_workflowStatus->setText(text("Eklenen kitap klasörü: ","Book folders added: ")+QString::number(count));
    });
    inputButton(text("PDF seç ve düzenle","Select and edit PDF"),"workflowInputPdf",[this]{auto path=QFileDialog::getOpenFileName(this,text("İşlenecek PDF","PDF to edit"),{},"PDF (*.pdf)");if(!path.isEmpty()){m_unified->importPdf(path,true);}});
    inputButton(text("Kayıtlı projeyi aç","Open saved project"),"workflowInputProject",[this]{openProject();});
    inputButton(text("Çıktı klasörünü seç","Choose output folder"),"workflowInputOutput",[this]{m_unified->chooseWorkflowOutput();refreshWorkflowState();});
    inputButton(text("Başlangıç ayarları","Startup settings"),"workflowInputSetup",[this]{m_unified->openStartupSettings();refreshWorkflowState();});
    auto paths=note({},input);paths->setObjectName("workflowPaths");
    m_unifiedViewStack->addWidget(m_workflowInput);
    m_workflowCache=new UnifiedProjectCache;m_unifiedViewStack->addWidget(m_workflowCache);
    m_workflowCache->busy=[this]{return isBatchProcessingInProgress()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning)||(m_unified&&m_unified->busy());};
    m_workflowCache->cleanRequested=[this]{runWorkflowStep("storage");};
    m_workflowHelp=unifiedWorkflowHelp();m_unifiedViewStack->addWidget(m_workflowHelp);
    m_workflowCleanup=new QWidget;auto clean=new QVBoxLayout(m_workflowCleanup);
    note(text("9 · Parmak ve leke temizliği","9 · Fingers and stains"),clean);
    note(text("Kenar izlerini Çıktı görüntüsünde önerir. Önizleyip uygula; eski TIFF ve PDF korunur. Harfleri örten parmak eksik metni geri getiremez. OCR bu adımda çalışmaz.","Propose edge cleanup on Output images. Preview and apply; original TIFF and PDF are preserved. A finger covering letters cannot reveal the missing text. OCR does not run at this stage."),clean);
    m_cleanupScope=new QComboBox;m_cleanupScope->setObjectName("workflowCleanupScope");
    m_cleanupScope->addItem(text("Yalnız bu sayfa","This page"),"page");m_cleanupScope->addItem(text("Seçili sayfalar","Selected pages"),"selected");m_cleanupScope->addItem(text("Kitabın tamamı","Whole book"),"book");clean->addWidget(m_cleanupScope);
    auto cleanActions=new QHBoxLayout;clean->addLayout(cleanActions);
    for(auto id:{"cleanup","cleanup_apply","cleanup_undo"}){
        QString mode=id;auto b=new QPushButton(mode=="cleanup"?text("▶ Temizliği hazırla","▶ Prepare cleanup"):mode=="cleanup_apply"?text("Temizliği uygula","Apply cleanup"):text("Temizliği geri al","Undo cleanup"));
        b->setObjectName("workflow_"+mode);cleanActions->addWidget(b);connect(b,&QPushButton::clicked,this,[this,mode]{if(mode=="cleanup")runWorkflowStep(mode);else startWorkflowProcess(mode);});
    }
    auto manual=new QPushButton(text("Elle dolgu bölgesi çiz","Draw a manual fill zone"));manual->setObjectName("workflowCleanupManual");clean->addWidget(manual);
    connect(manual,&QPushButton::clicked,this,[this]{m_editFillRequested=true;filterList->selectFilter(5);reloadRequested();});
    m_cleanupStatus=note({},clean);m_cleanupStatus->setObjectName("workflowCleanupStatus");
    auto list=new QListWidget;list->setObjectName("workflowCleanupPages");list->setMaximumHeight(120);clean->addWidget(list);
    auto preview=new QSplitter;preview->setObjectName("workflowCleanupPreview");clean->addWidget(preview,1);
    connect(list,&QListWidget::currentRowChanged,this,[this,preview](int row){
        while(preview->count())delete preview->widget(0);
        auto pages=m_workflowReceipt.value("pages").toArray();if(row<0||row>=pages.size())return;auto page=pages[row].toObject();
        for(auto key:{"before","after"}){auto host=new QWidget;auto layout=new QVBoxLayout(host);layout->addWidget(new QLabel(QString(key)=="before"?text("Önce","Before"):text("Sonra · öneri","After · proposal")));QImage image(page.value(key).toString());if(!image.isNull())layout->addWidget(new BasicImageView(std::make_shared<NonAcceleratedOperations>(),image),1);preview->addWidget(host);}
        auto report=page.value("report").toObject();m_cleanupStatus->setText(text("Silme adayı / belirsiz bölge: ","Removal candidates / uncertain regions: ")+QString::number(report.value("removed").toArray().size())+" / "+QString::number(report.value("uncertain").toArray().size()));
    });
    m_unifiedViewStack->addWidget(m_workflowCleanup);
    connect(m_unified,&UnifiedPanel::importedImagesReady,this,[this](QString const& folder){importWorkflowImages(imagesIn(folder));});
    auto timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&MainWindow::refreshWorkflowState);timer->start(1000);
    filterList->selectStep("input");selectWorkflowRow(0);resize(qMax(width(),1280),qMax(height(),780));
    resizeDocks({dockWidgetFilters,optionsDock,dockWidgetThumbnails},{filterList->minimumWidth()+12,310,140},Qt::Horizontal);
    m_defaultPanelState=saveState();refreshWorkflowState();
}
void MainWindow::showWorkflowTools(){
    if(!m_workflowReady){m_unifiedDock->show();m_unifiedDock->raise();return;}
    filterList->selectStep("settings");selectWorkflowRow(unified_workflow::row("settings"));
}
bool MainWindow::selectWorkflowRow(int row){
    auto id=filterList->stepId(row);if(id.isEmpty())return false;
    if(m_unifiedViewStack->currentWidget()==m_queuePreview&&filterList->filterForRow(row)>=0){m_unified->editPreview(filterList->filterForRow(row));return false;}
    m_workflowStep=id;m_unified->setWorkflowContext(id);
    dockWidgetFilters->setWindowTitle(text("Aşamalar · resimden PDF’ye","Stages · images to PDF"));
    if(m_geometryReviewBar)m_geometryReviewBar->setVisible(id=="ai_geometry");
    if(m_contentReviewBar)m_contentReviewBar->setVisible(id=="ai_content");
    scrollArea->parentWidget()->setVisible(filterList->filterForRow(row)>=0);
    dockWidgetThumbnails->setVisible(filterList->filterForRow(row)>=0||id=="cleanup");
    if(filterList->filterForRow(row)>=0){returnToProjectView();return true;}
    if(id=="input")m_unifiedViewStack->setCurrentWidget(m_workflowInput);
    else if(id=="help")m_unifiedViewStack->setCurrentWidget(m_workflowHelp);
    else if(id=="cleanup")m_unifiedViewStack->setCurrentWidget(m_workflowCleanup);
    else if(id=="storage"){m_unifiedViewStack->setCurrentWidget(m_workflowCache);syncWorkflowCache(true);}
    else {
        m_unifiedViewStack->setCurrentWidget(m_workflowTools);
        auto step=unified_workflow::steps().at(row);m_workflowTitle->setText(step.title+"\n"+step.description);
        m_workflowRun->setVisible(id=="pdf"||id=="ocr");m_workflowRun->setText(id=="ocr"?text("▶ OCR ekle","▶ Add OCR"):text("▶ PDF oluştur","▶ Create PDF"));
        if(id=="pdf"||id=="ocr")m_unified->showToolsTab(id=="ocr"?3:1);
    }
    filterList->setBatchProcessingPossible(isProjectLoaded());refreshWorkflowState();return false;
}
void MainWindow::importWorkflowImages(QStringList files){
    if(files.isEmpty()){QMessageBox::information(this,text("Girdi","Input"),text("Bu klasörde desteklenen resim bulunamadı.","No supported images found in this folder."));return;}
    if(isBatchProcessingInProgress()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return;
    if(!closeProjectInteractive())return;
    std::sort(files.begin(),files.end(),SmartFilenameOrdering());files.removeDuplicates();std::vector<ImageFileInfo> loaded;QStringList failed;
    for(auto const& path:files){ImageFileInfo file(QFileInfo(path),{});auto result=ImageMetadataLoader::load(path,[&](ImageMetadata const& m){file.imageInfo().push_back(m);});if(result==ImageMetadataLoader::LOADED)loaded.push_back(file);else failed<<path;}
    if(!failed.isEmpty()){QMessageBox::warning(this,text("Okunamayan resimler","Unreadable images"),failed.join("\n"));return;}if(loaded.empty())return;
    QString base=m_unified->workflowOutput();if(base.isEmpty())base=QFileInfo(files.front()).absolutePath()+"/output";
    QString out=base+"/"+QFileInfo(files.front()).dir().dirName()+"-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)+"/images";if(!QDir().mkpath(out))return;
    switchToNewProject(IntrusivePtr<ProjectPages>(new ProjectPages(loaded,ProjectPages::AUTO_PAGES,Qt::LeftToRight)),out);
    m_workflowReview.clear();m_workflowPdf.clear();m_workflowPdfStamp.clear();m_workflowReceipt={};
    syncWorkflowCache();m_workflowCache->own(out,"native");
    auto imported=QFileInfo(files.front()).absoluteDir();imported.cdUp();imported.cdUp();
    if(QFileInfo::exists(imported.absoluteFilePath(".stou-import-session.json")))m_workflowCache->own(imported.absolutePath(),"sources");
    filterList->selectFilter(0);selectWorkflowRow(unified_workflow::row("orientation"));
}
namespace {
QByteArray canonicalSettings(QDomElement const& e){
    if(e.isNull())return {};
    auto tag=e.tagName();if(tag=="output-params"||tag=="outputParams"||tag=="dependencies"||tag=="contentSize"||tag=="content-size-px")return {};
    QByteArray out="<"+tag.toUtf8();QStringList names;auto attributes=e.attributes();
    for(int i=0;i<attributes.count();++i){auto name=attributes.item(i).nodeName();if(name!="id"&&name!="selected")names<<name;}
    names.sort();for(auto const& name:names)out+=" "+name.toUtf8()+"="+e.attribute(name).toUtf8().toPercentEncoding();
    out+=">";for(auto child=e.firstChild();!child.isNull();child=child.nextSibling())out+=child.isElement()?canonicalSettings(child.toElement()):child.nodeValue().trimmed().toUtf8();return out+"</"+tag.toUtf8()+">";
}
}
QJsonArray MainWindow::workflowPages()const{
    QJsonArray rows;if(!isProjectLoaded())return rows;
    ProjectWriter writer(m_ptrPages,m_selectedPage,m_outFileNameGen);auto doc=writer.toXml(m_ptrStages->filters(),QFileInfo(m_projectFile).absolutePath());
    std::map<PageId,int> pageIds;std::map<ImageId,int> imageIds;writer.enumPages([&](PageId const& id,int n){pageIds[id]=n;});writer.enumImages([&](ImageId const& id,int n){imageIds[id]=n;});
    struct FilterStamp {QByteArray common;QHash<int,QByteArray> page,image;};
    QVector<FilterStamp> filterStamps;auto filters=doc.documentElement().firstChildElement("filters");
    for(auto filter=filters.firstChildElement();!filter.isNull();filter=filter.nextSiblingElement()){
        FilterStamp stamp;stamp.common=canonicalSettings(filter.cloneNode(false).toElement());
        for(auto entry=filter.firstChildElement();!entry.isNull();entry=entry.nextSiblingElement()){
            auto bytes=canonicalSettings(entry);int id=entry.attribute("id").toInt();
            if(entry.tagName()=="page")stamp.page[id]+=bytes;else if(entry.tagName()=="image")stamp.image[id]+=bytes;else stamp.common+=bytes;
        }
        filterStamps.append(stamp);
    }
    auto pages=m_ptrPages->toPageSequence(PAGE_VIEW);
    for(size_t i=0;i<pages.numPages();++i){auto id=pages.pageAt(i).id();QFileInfo source(id.imageId().filePath());
        QString key=source.absoluteFilePath()+"#"+QString::number(id.imageId().page())+"#"+id.subPageAsString();
        QByteArray payload=key.toUtf8()+":"+QByteArray::number(source.size())+":"+QByteArray::number(source.lastModified().toMSecsSinceEpoch());
        for(auto const& stamp:filterStamps)payload+=stamp.common+stamp.image.value(imageIds[id.imageId()])+stamp.page.value(pageIds[id]);
        auto output=m_outFileNameGen.filePathFor(id);
        rows.append(QJsonObject{{"page",int(i+1)},{"key",key},{"source",output},{"gray",output+".ocr.tif"},{"input_stamp",QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex())}});
    }
    return rows;
}
QByteArray MainWindow::workflowFingerprint()const{
    auto options=m_unified->workflowOptions();for(auto key:{"ocr_engine","ocr_device","tess_quality","paddle_language","language","threads","root"})options.remove(key);
    QJsonArray stamps;for(auto value:workflowPages()){auto row=value.toObject();stamps.append(QJsonObject{{"key",row.value("key")},{"input_stamp",row.value("input_stamp")}});}
    return QCryptographicHash::hash(QJsonDocument(stamps).toJson(QJsonDocument::Compact)+QJsonDocument(options).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
}
void MainWindow::saveWorkflowState(){
    if(m_outFileNameGen.outDir().isEmpty())return;
    QSaveFile file(m_outFileNameGen.outDir()+"/.workflow-state.json");
    if(!file.open(QIODevice::WriteOnly))return;
    file.write(QJsonDocument(QJsonObject{{"pdf",m_workflowPdf},{"visual_pdf",m_workflowVisualPdf},{"visual_sha256",m_workflowVisualHash},{"stamp",QString::fromLatin1(m_workflowPdfStamp)},{"cleanup",m_workflowReview},{"pages",m_workflowLastPages}}).toJson());file.commit();
}
void MainWindow::refreshWorkflowState(){
    if(!m_workflowReady)return;
    if(auto paths=m_workflowInput->findChild<QLabel*>("workflowPaths"))paths->setText(text("PDF klasörü: ","PDF folder: ")+m_unified->workflowOutput()+"\n"+text("Açık projenin resim çıktısı: ","Open project's output images: ")+m_outFileNameGen.outDir());
    if(auto folder=m_workflowTools->findChild<QLineEdit*>("workflowPdfOutputFolder"))folder->setText(m_unified->workflowOutput());
    bool running=m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning;
    m_workflowStop->setEnabled(running);m_workflowRun->setEnabled(isProjectLoaded()&&!running&&!isBatchProcessingInProgress());
    if(running){filterList->setStepStatus(m_workflowProcess->property("stage").toString(),QString::number(m_workflowElapsed.elapsed()/1000)+" s");}
    QString key=m_outFileNameGen.outDir();
    if(key!=m_workflowProjectKey){
        m_workflowProjectKey=key;m_workflowPdf.clear();m_workflowVisualPdf.clear();m_workflowVisualHash.clear();m_workflowPdfStamp.clear();m_workflowReview.clear();m_workflowReceipt={};m_workflowLastPages={};
        QFile saved(key+"/.workflow-state.json");if(!key.isEmpty()&&saved.open(QIODevice::ReadOnly)){auto state=QJsonDocument::fromJson(saved.readAll()).object();m_workflowPdf=state.value("pdf").toString();m_workflowVisualPdf=state.value("visual_pdf").toString();m_workflowVisualHash=state.value("visual_sha256").toString();m_workflowPdfStamp=state.value("stamp").toString().toLatin1();m_workflowReview=state.value("cleanup").toString();m_workflowLastPages=state.value("pages").toArray();QFile review(m_workflowReview);if(review.open(QIODevice::ReadOnly))m_workflowReceipt=QJsonDocument::fromJson(review.readAll()).object();}
    }
    m_workflowDirty->setText(!isProjectLoaded()?text("0 · Girdi ile bir kitap açın.","Open a book at 0 · Input."):m_workflowPdf.isEmpty()?text("Çıktı → temizlik → PDF → OCR\nPDF henüz oluşturulmadı.","Output → cleanup → PDF → OCR\nPDF has not been created yet."):(!QFileInfo::exists(m_workflowPdf)||workflowFingerprint()!=m_workflowPdfStamp)?text("Ayarlar değişti. Çıktı/PDF yenilenmeli. Eski PDF korunuyor.","Settings changed. Refresh Output/PDF. The previous PDF is preserved."):text("PDF güncel kayıtlı ayarlarla oluşturuldu.","PDF was created with the current saved settings."));
}
void MainWindow::runWorkflowStep(QString const& id){
    if(!isProjectLoaded()||isBatchProcessingInProgress()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return;
    filterList->selectStep(id);
    if(id=="pdf"||id=="cleanup"||id=="ai_geometry"||id=="ai_content"){
        auto current=workflowPages();auto sequence=m_ptrPages->toPageSequence(PAGE_VIEW);QHash<QString,QJsonObject> previous;
        for(auto value:m_workflowLastPages){auto row=value.toObject();previous.insert(row.value("key").toString(),row);}
        auto options=m_unified->workflowOptions();QHash<QString,QString> masks;
        for(auto value:m_workflowReceipt.value("pages").toArray()){auto row=value.toObject();if(row.value("applied").toBool())masks.insert(row.value("key").toString(),row.value("mask_sha256").toString());}
        m_workflowPageTargets.clear();m_workflowPartial=true;
        auto selected=selectedPages();auto currentId=m_ptrThumbSequence->selectionLeader().id();
        QString scope=id=="cleanup"?m_cleanupScope->currentData().toString():id=="ai_geometry"?m_geometryReviewBar->scope():id=="ai_content"?m_contentReviewBar->scope():"book";
        for(int i=0;i<current.size();++i){auto row=current[i].toObject();auto page=sequence.pageAt(size_t(i)).id();
            auto old=previous.value(row.value("key").toString());auto oldSettings=old.value("pdf_settings").toObject();bool sameSettings=!oldSettings.isEmpty();
            for(auto it=oldSettings.begin();it!=oldSettings.end();++it)if(options.value(it.key())!=it.value())sameSettings=false;
            bool missing=!QFileInfo::exists(row.value("source").toString())||!QFileInfo::exists(row.value("gray").toString());
            bool include=id=="pdf"?(old.value("input_stamp")!=row.value("input_stamp")||(missing&&(!sameSettings||!QFileInfo::exists(m_workflowPdf)||old.value("cleanup_hash").toString()!=masks.value(row.value("key").toString())))):(scope=="book"||(scope=="page"?page==currentId:selected.count(page)));
            if(include)m_workflowPageTargets.insert(page);
        }
        if(m_workflowPageTargets.empty()){m_workflowPartial=false;startWorkflowProcess(id);return;}
        QString normal=id=="ai_geometry"?"geometry":id=="ai_content"?"content":"output";
        filterList->selectStep(normal);m_workflowPending=id;startBatchProcessing();return;
    }
    if(id=="storage"){QElapsedTimer elapsed;elapsed.start();syncWorkflowCache();m_workflowCache->clean();filterList->setStepStatus(id,QString::number(elapsed.elapsed()/1000)+" s");return;}
    if(id=="ocr"){startWorkflowProcess(id);return;}
    if(filterList->filterForRow(unified_workflow::row(id))>=0)startBatchProcessing();
}
void MainWindow::startWorkflowProcess(QString const& mode){
    if(mode=="ai_geometry"||mode=="ai_content"){stageReviewAction(mode=="ai_geometry",UnifiedStageReviewBar::Analyze);return;}
    if(!isProjectLoaded()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return;
    auto fail=[this](QString const& message){m_workflowStatus->setText(message);m_cleanupStatus->setText(message);QMainWindow::statusBar()->showMessage(message,15000);};
    if(mode=="ocr"){
        if(m_workflowPdf.isEmpty()||workflowFingerprint()!=m_workflowPdfStamp){fail(text("Önce güncel ayarlarla 10 · PDF oluştur aşamasını çalıştırın.","First run 10 · Create PDF with current settings."));return;}
        auto error=m_unified->workflowOcrValidation();if(!error.isEmpty()){fail(error);return;}
    }
    if(mode.startsWith("cleanup_")&&m_workflowReview.isEmpty()){fail(text("Önce temizliği hazırlayın.","Prepare cleanup first."));return;}
    auto sequence=m_ptrPages->toPageSequence(PAGE_VIEW);auto allRows=workflowPages();QJsonArray pages;auto selected=selectedPages();auto current=m_ptrThumbSequence->selectionLeader().id();
    for(size_t i=0;i<sequence.numPages();++i){auto page=sequence.pageAt(i).id();
        if(mode=="cleanup"&&m_cleanupScope->currentData()=="page"&&page!=current)continue;
        if(mode=="cleanup"&&m_cleanupScope->currentData()=="selected"&&!selected.count(page))continue;
        pages.append(allRows[int(i)]);
    }
    QString root=unified_runtime::root();QString folder=unified_runtime::data(root)+"/resources/reviews/workflow/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!QDir().mkpath(folder)){fail(text("İşlem klasörü oluşturulamadı.","Could not create work folder."));return;}
    syncWorkflowCache();m_workflowCache->own(folder,"work");
    QString ocrCache=m_outFileNameGen.outDir()+"/.ocr-text-cache";QDir().mkpath(ocrCache);m_workflowCache->own(ocrCache,"ocr");
    auto request=m_unified->workflowOptions();request.insert("ocr_cache",ocrCache);request.insert("mode",mode);request.insert("pages",pages);request.insert("work",folder);
    request.insert("cache_owner",m_outFileNameGen.outDir());request.insert("cancel_file",folder+"/cancel");
    request.insert("output_dir",m_unified->workflowOutput());request.insert("project_fingerprint",QString::fromLatin1(workflowFingerprint()));
    request.insert("cleanup_review",m_workflowReview);request.insert("previous_pdf",m_workflowPdf);request.insert("source_pdf",m_workflowVisualPdf);request.insert("source_pdf_sha256",m_workflowVisualHash);
    QString title=m_projectFile.isEmpty()?QFileInfo(m_outFileNameGen.outDir()).dir().dirName():QFileInfo(m_projectFile).completeBaseName();request.insert("title",title);
    QString requestPath=folder+"/request.json";QSaveFile file(requestPath);if(!file.open(QIODevice::WriteOnly)){fail(file.errorString());return;}file.write(QJsonDocument(request).toJson());if(!file.commit()){fail(file.errorString());return;}
    m_workflowResult=folder+"/result.json";
    if(m_workflowProcess)m_workflowProcess->deleteLater();
    auto process=new QProcess(this);m_workflowProcess=process;process->setObjectName("workflowProcess");
    QString stage=mode.startsWith("cleanup")?"cleanup":mode;process->setProperty("stage",stage);process->setProperty("fingerprint",workflowFingerprint());process->setProperty("project",m_outFileNameGen.outDir());process->setProperty("work",folder);
    bool hadOcr=false;for(auto value:m_workflowLastPages)hadOcr|=value.toObject().value("has_ocr").toBool();process->setProperty("refreshOcr",mode=="pdf"&&hadOcr);
    auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("PYTHONPATH",root+"/src");env.insert("OMP_NUM_THREADS",QString::number(request.value("threads").toInt(2)));process->setProcessEnvironment(env);process->setWorkingDirectory(root);
    m_workflowElapsed.start();m_workflowStatus->setText(text("İşlem sürüyor…","Processing…"));m_cleanupStatus->setText(m_workflowStatus->text());
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process]{
        auto buffer=process->property("buffer").toByteArray()+process->readAllStandardOutput();int end;
        while((end=buffer.indexOf('\n'))>=0){auto line=buffer.left(end);buffer.remove(0,end+1);if(line.startsWith("STOUWORK ")){auto event=QJsonDocument::fromJson(line.mid(9)).object();QString status=QString::number(event.value("page").toInt())+"/"+QString::number(event.value("total").toInt())+" · "+QString::number(m_workflowElapsed.elapsed()/1000)+" s";m_workflowStatus->setText(status);m_cleanupStatus->setText(status);}}
        process->setProperty("buffer",buffer);
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process,mode,stage](int code,QProcess::ExitStatus status){
        for(auto name:{"image-pdf.part.pdf","fitted-pdf.part.pdf"})QFile::remove(process->property("work").toString()+"/"+name);
        QFile result(m_workflowResult);QJsonObject data;if(result.open(QIODevice::ReadOnly))data=QJsonDocument::fromJson(result.readAll()).object();
        auto message=code==0&&status==QProcess::NormalExit&&data.value("ok").toBool()?text("Tamamlandı · ","Completed · ")+QString::number(m_workflowElapsed.elapsed()/1000)+" s":text("İşlem tamamlanmadı: ","Operation did not complete: ")+QString::fromUtf8(process->readAllStandardError()).right(900);
        m_workflowStatus->setText(message);m_cleanupStatus->setText(message);filterList->setStepStatus(stage,QString::number(m_workflowElapsed.elapsed()/1000)+" s");
        if(code==0&&data.value("ok").toBool()&&process->property("project").toString()==m_outFileNameGen.outDir()){
            if(mode=="cleanup"||mode.startsWith("cleanup_")){if(mode.startsWith("cleanup_"))m_workflowPdfStamp.clear();m_workflowReview=data.value("receipt").toString();m_workflowReceipt=data;
                auto list=m_workflowCleanup->findChild<QListWidget*>("workflowCleanupPages");list->clear();for(auto value:data.value("pages").toArray()){auto row=value.toObject();list->addItem(text("Sayfa ","Page ")+QString::number(row.value("page").toInt()));}if(list->count())list->setCurrentRow(0);
                m_cleanupStatus->setText(message+" · "+(data.value("applied").toBool()?text("PDF'ye dahil edilecek","Will be included in PDF"):text("Öneri; henüz uygulanmadı","Proposal; not applied")));
            }else if(mode=="pdf"||mode=="ocr"){m_workflowPdf=data.value("pdf").toString();m_workflowLastPages=data.value("records").toArray();m_workflowVisualPdf=m_workflowPdf;m_workflowVisualHash=data.value("sha256").toString();m_workflowPdfStamp=process->property("fingerprint").toByteArray();m_workflowStatus->setText(message+"\n"+text("Yenilenen / korunan sayfa: ","Updated / preserved pages: ")+QString::number(data.value("changed_pages").toArray().size())+" / "+QString::number(data.value("reused_pages").toArray().size())+"\n"+m_workflowPdf);}
        }
        saveWorkflowState();refreshWorkflowState();syncWorkflowCache();
        bool success=code==0&&status==QProcess::NormalExit&&data.value("ok").toBool()&&process->property("project").toString()==m_outFileNameGen.outDir();
        if(success&&(mode=="pdf"||mode=="ocr")){
            m_workflowCache->producedPdf(m_workflowPdf);
            QStringList stale;for(auto page:data.value("cleanup_needs_review").toArray())stale<<QString::number(page.toInt());
            if(!stale.isEmpty())m_workflowStatus->setText(m_workflowStatus->text()+"\n"+text("Yeni düzeltme nedeniyle bu sayfaların leke temizliği tekrar incelenmeli: ","Review stain cleanup again on these edited pages: ")+stale.join(", "));
            m_workflowCache->clean(mode);
        }
        if(success&&process->property("refreshOcr").toBool()&&!data.value("changed_pages").toArray().isEmpty())QTimer::singleShot(0,this,[this]{runWorkflowStep("ocr");});
    });
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error){if(error==QProcess::FailedToStart){m_workflowStatus->setText(process->errorString());m_cleanupStatus->setText(process->errorString());refreshWorkflowState();}});
    process->start(unified_runtime::python(root),{"-u","-m","scantailor_ocr_unified.workflow_stages",requestPath});refreshWorkflowState();
}
void MainWindow::syncWorkflowCache(bool scan){
    if(!m_workflowCache)return;
    m_workflowCache->context(m_outFileNameGen.outDir(),m_projectFile,workflowPages(),m_workflowPdf,m_workflowVisualHash);
    if(scan)m_workflowCache->scan();
}
void MainWindow::finishWorkflowProject(bool discard){
    if(!m_workflowCache)return;
    syncWorkflowCache();
    stopBatchProcessing(CLEAR_MAIN_AREA);m_ptrInteractiveQueue->cancelAndClear();m_ptrWorkerThreadPool->shutdown();
    // Release the old thumbnail reader before deleting its owned source copies.
    closeProjectWithoutSaving();
    m_workflowCache->clean(discard?"discard":"close");
    m_workflowCache->context({}, {}, {}, {}, {});
}
void MainWindow::cancelWorkflowProcess(){
    if(!m_workflowProcess||m_workflowProcess->state()==QProcess::NotRunning)return;
    QFile flag(m_workflowProcess->property("work").toString()+"/cancel");if(flag.open(QIODevice::WriteOnly))flag.write("cancel");
    QPointer<QProcess> process=m_workflowProcess;
    QTimer::singleShot(5000,this,[process]{if(process&&process->state()!=QProcess::NotRunning)process->kill();});
}
