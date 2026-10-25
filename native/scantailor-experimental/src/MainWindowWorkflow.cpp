// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "UnifiedWorkflow.h"
#include "UnifiedQueueWorkflow.h"
#include "UnifiedWorkflowHelp.h"
#include "UnifiedProjectCache.h"
#include "UnifiedRuntime.h"
#include "UnifiedPanel.h"
#include "UnifiedStageReviewBar.h"
#include "UnifiedGeometryReview.h"
#include "UnifiedContentReview.h"
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
#include <QSignalBlocker>
#include <QCheckBox>

using unified_workflow::text;

namespace {
QStringList imagesIn(QString const& folder) {
    QStringList paths;QDir dir(folder);for(auto const& name:dir.entryList({"*.jpg","*.jpeg","*.png","*.tif","*.tiff","*.bmp"},QDir::Files))paths<<dir.absoluteFilePath(name);return paths;
}
QLabel* note(QString const& value,QVBoxLayout* layout){auto label=new QLabel(value);label->setWordWrap(true);label->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(label);return label;}
QString readingPdf(QString const& master){
    QFile receipt(QFileInfo(master).absolutePath()+"/"+QFileInfo(master).completeBaseName()+".workflow.json");
    if(receipt.open(QIODevice::ReadOnly)){
        auto path=QJsonDocument::fromJson(receipt.readAll()).object().value("reading_pdf").toString();
        if(QFileInfo::exists(path))return path;
    }
    return master;
}
}
void MainWindow::initWorkflow(){
    m_workflowReady=true;
    menuFile->setTitle(text("Dosya","File"));menuDebug->setTitle(text("Araçlar","Tools"));menuHelp->setTitle(text("Yardım","Help"));
    actionNewProject->setText(text("Yeni proje…","New Project…"));actionOpenProject->setText(text("Proje / çalışma aç…","Open Project / Run…"));
    actionSaveProject->setText(text("Projeyi kaydet","Save Project"));actionSaveProjectAs->setText(text("Projeyi farklı kaydet…","Save Project As…"));
    actionCloseProject->setText(text("Projeyi kapat","Close Project"));actionQuit->setText(text("Çıkış","Quit"));
    actionSettings->setText(text("Uygulama ayarları…","Application Settings…"));actionAbout->setText(text("Hakkında","About"));
    actionFixDpi->setText(text("DPI düzelt…","Fix DPI…"));actionRelinking->setText(text("Eksik dosyaları yeniden bağla…","Relink Missing Files…"));
    actionDebug->setText(text("Hata ayıklama modu","Debug Mode"));
    dockWidgetThumbnails->setWindowTitle(text("Sayfalar","Pages"));
    dockWidgetFilters->setWindowTitle(text("Aşamalar · resimden PDF’ye","Stages · images to PDF"));
    dockWidgetFilters->setMinimumWidth(filterList->minimumWidth()+12);
    // Give the stage list its own full-height column. Native controls get a
    // separate scrollable dock, so larger fonts never hide stage names.
    auto optionsDock=new QDockWidget(text("Seçili aşamanın ayarları","Selected stage settings"),this);optionsDock->setObjectName("workflowOptionsDock");
    scrollArea->setParent(nullptr);optionsDock->setWidget(scrollArea);optionsDock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea,optionsDock);
    m_workflowDirty=new QLabel; m_workflowDirty->setWordWrap(true);m_workflowDirty->setObjectName("workflowDependencyStatus");
    auto stagesLayout=qobject_cast<QVBoxLayout*>(filterList->parentWidget()->layout());
    if(stagesLayout){
        auto group=new QComboBox;group->setObjectName("workflowNavigationGroup");
        group->addItems({text("1 · Ekle","1 · Add"),text("2 · Düzelt","2 · Adjust"),text("3 · Kontrol et","3 · Review"),text("4 · PDF ve OCR","4 · PDF and OCR"),text("5 · Kaydet ve yönet","5 · Save and manage")});
        stagesLayout->insertWidget(0,group);
        auto all=new QCheckBox(text("Tüm aşamaları göster","Show all stages"));all->setObjectName("workflowShowAllStages");stagesLayout->insertWidget(1,all);
        all->setChecked(QSettings().value("unified/all_workflow_stages",false).toBool());filterList->showAllStages(all->isChecked());
        connect(all,&QCheckBox::toggled,this,[this](bool show){filterList->showAllStages(show);QSettings().setValue("unified/all_workflow_stages",show);});
        connect(group,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){const QStringList first={"input","orientation","cleanup","pdf","storage"};filterList->selectStep(first.value(index,"input"));});
        connect(filterList,&StageListView::navigationGroupChanged,this,[group](int index){QSignalBlocker block(group);group->setCurrentIndex(index);});
        connect(filterList->selectionModel(),&QItemSelectionModel::currentChanged,this,[this,group](QModelIndex const& current,QModelIndex const&){QSignalBlocker block(group);group->setCurrentIndex(StageListView::groupForStep(filterList->stepId(current.row())));});
    }
    if(stagesLayout){stagesLayout->addWidget(m_workflowDirty);auto refresh=new QPushButton(text("Çıktıyı ve PDF'yi yenile","Refresh Output and PDF"));refresh->setObjectName("workflowRefreshPdf");stagesLayout->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,[this]{runWorkflowStep("pdf");});stagesLayout->addStretch();}
    connect(filterList,&StageListView::launchStep,this,&MainWindow::runWorkflowStep);
    connect(filterList,&QTableView::clicked,this,[this](QModelIndex const& index){
        // A click on the already-selected row does not emit selectionChanged.
        if(m_unifiedViewStack->currentWidget()==m_queuePreview)selectWorkflowRow(index.row());
    });
    // Reuse the existing settings widgets, not a second copy of their values.
    m_workflowTools=new QWidget;auto toolsLayout=new QVBoxLayout(m_workflowTools);
    m_workflowTitle=note({},toolsLayout);m_workflowTitle->setObjectName("workflowToolsTitle");
    auto actions=new QHBoxLayout;toolsLayout->addLayout(actions);
    m_workflowRun=new QPushButton;m_workflowRun->setObjectName("workflowRun");actions->addWidget(m_workflowRun);
    connect(m_workflowRun,&QPushButton::clicked,this,[this]{runWorkflowStep(m_workflowStep);});
    m_workflowStop=new QPushButton(text("Durdur","Stop"));m_workflowStop->setObjectName("workflowStop");actions->addWidget(m_workflowStop);m_workflowStop->setEnabled(false);
    connect(m_workflowStop,&QPushButton::clicked,this,&MainWindow::cancelWorkflowProcess);
    auto openResult=new QPushButton(text("Son PDF'yi aç","Open latest PDF"));openResult->setObjectName("workflowOpenPdf");actions->addWidget(openResult);connect(openResult,&QPushButton::clicked,this,[this]{auto path=readingPdf(m_workflowPdf);if(QFileInfo::exists(path))QDesktopServices::openUrl(QUrl::fromLocalFile(path));});
    auto folderRow=new QHBoxLayout;toolsLayout->addLayout(folderRow);folderRow->addWidget(new QLabel(text("PDF çıktı klasörü","PDF output folder")));
    auto pdfFolder=new QLineEdit;pdfFolder->setReadOnly(true);pdfFolder->setObjectName("workflowPdfOutputFolder");folderRow->addWidget(pdfFolder,1);
    auto output=new QPushButton(text("Gözat…","Browse…"));output->setObjectName("workflowPdfBrowse");folderRow->addWidget(output);connect(output,&QPushButton::clicked,this,[this]{m_unified->chooseWorkflowOutput();refreshWorkflowState();});
    auto cacheSettings=new QPushButton(text("Önbellek ayarları","Cache settings"));actions->addWidget(cacheSettings);connect(cacheSettings,&QPushButton::clicked,this,[this]{filterList->selectStep("storage");});
    auto nativeSettings=new QPushButton(text("Uygulama ayarları","Application settings"));actions->addWidget(nativeSettings);connect(nativeSettings,&QPushButton::clicked,this,&MainWindow::openSettingsDialog);
    m_workflowStatus=note({},toolsLayout);m_workflowStatus->setObjectName("workflowStatus");
    m_unified->setParent(m_workflowTools);toolsLayout->addWidget(m_unified,1);m_unified->show();
    connect(m_unified->findChild<QPushButton*>("workflowOcrRun"),&QPushButton::clicked,this,[this]{runWorkflowStep("ocr");});
    m_unifiedDock->setWidget(new QWidget);m_unifiedDock->hide();m_unifiedDock->toggleViewAction()->setVisible(false);
    m_unifiedViewStack->addWidget(m_workflowTools);
    m_workflowInput=new QWidget;auto input=new QVBoxLayout(m_workflowInput);input->setAlignment(Qt::AlignTop);
    auto heading=note(text("0 · Girdi — kitabı buradan başlatın","0 · Input — start your book here"),input);auto font=heading->font();font.setPointSize(font.pointSize()+3);font.setBold(true);heading->setFont(font);
    note(text("Resim veya PDF → yön → bölme → geometri → isteğe bağlı AI → içerik → kenarlar → Çıktı → temizlik → PDF → OCR → önbellek temizliği. Kaynaklar korunur.","Images or PDF → orientation → split → geometry → optional AI → content → margins → Output → cleanup → PDF → OCR → cache cleanup. Sources are preserved."),input);
    auto ai=new QCheckBox(text("Yapay zekâ desteği istiyorum · AI geometri ve AI içerik aşamalarını göster","Use AI assistance · show AI geometry and AI content stages"));ai->setObjectName("workflowAiEnabled");input->addWidget(ai);
    ai->setChecked(QSettings().value("unified/ai_enabled",false).toBool());filterList->setAiEnabled(ai->isChecked());
    connect(ai,&QCheckBox::toggled,this,[this](bool enabled){QSettings().setValue("unified/ai_enabled",enabled);filterList->setAiEnabled(enabled);});
    note(text("AI kapalıyken standart düzeltmeler ve OCR çalışır. AI aşamasındaki ▶ uygun önerileri uygular; Hazırla düğmesi yalnız önizleme üretir.","Standard corrections and OCR work with AI disabled. ▶ in an AI stage applies valid proposals; Prepare only creates previews."),input);
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
    inputButton(text("Kayıtlı proje / çalışma aç","Open saved project / run"),"workflowInputProject",[this]{openProject();});
    inputButton(text("Kitap kuyruğunu göster","Show book queue"),"workflowInputQueue",[this]{showWorkflowTools();m_unified->showToolsTab(0);});
    inputButton(text("Çıktı klasörünü seç","Choose output folder"),"workflowInputOutput",[this]{m_unified->chooseWorkflowOutput();refreshWorkflowState();});
    inputButton(text("Başlangıç ayarları","Startup settings"),"workflowInputSetup",[this]{m_unified->openStartupSettings();refreshWorkflowState();});
    auto paths=note({},input);paths->setObjectName("workflowPaths");
    m_unifiedViewStack->addWidget(m_workflowInput);
    m_workflowCache=new UnifiedProjectCache;m_unifiedViewStack->addWidget(m_workflowCache);
    m_workflowCache->busy=[this]{return isBatchProcessingInProgress()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning)||(m_unified&&m_unified->busy());};
    m_workflowCache->cleanRequested=[this]{runWorkflowStep("storage");};
    m_workflowCache->finishRequested=[this]{finalizeWorkflowProject();};
    connect(m_workflowCache->findChild<QPushButton*>("workflowFinishOpen"),&QPushButton::clicked,this,[this]{findChild<QPushButton*>("workflowOpenPdf")->click();});
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
    auto summary=note({},clean);summary->setObjectName("workflowCleanupSummary");
    auto reviewFilter=new QComboBox;reviewFilter->setObjectName("workflowCleanupFilter");
    reviewFilter->addItems({text("Tüm sayfalar","All pages"),text("Silme önerisi olanlar","Pages with removal proposals"),text("Kontrol gerekenler","Pages needing review")});clean->addWidget(reviewFilter);
    auto list=new QListWidget;list->setObjectName("workflowCleanupPages");list->setMaximumHeight(120);clean->addWidget(list);
    auto preview=new QSplitter;preview->setObjectName("workflowCleanupPreview");clean->addWidget(preview,1);
    connect(reviewFilter,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshCleanupPages();});
    connect(list,&QListWidget::currentRowChanged,this,[this,list](int row){
        if(row<0||!isProjectLoaded())return;auto sequence=m_ptrPages->toPageSequence(PAGE_VIEW);
        auto index=list->item(row)->data(Qt::UserRole).toInt();if(index<0||size_t(index)>=sequence.numPages())return;
        auto page=sequence.pageAt(size_t(index)).id();m_ptrThumbSequence->setSelection(page);showCleanupPage(page);
    });
    m_unifiedViewStack->addWidget(m_workflowCleanup);
    m_unified->importImagesForEditing=[this](QString const& folder){return importWorkflowImages(imagesIn(folder));};
    m_unified->editorProcessing=[this]{return isBatchProcessingInProgress()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning);};
    auto timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&MainWindow::refreshWorkflowState);timer->start(1000);
    filterList->selectStep("input");selectWorkflowRow(0);resize(qMax(width(),1280),qMax(height(),780));
    if(m_unified->monitoringExternalWorker()){showWorkflowTools();m_unified->showToolsTab(0);}
    resizeDocks({dockWidgetFilters,optionsDock,dockWidgetThumbnails},{filterList->minimumWidth()+12,310,140},Qt::Horizontal);
    m_defaultPanelState=saveState();refreshWorkflowState();
}
void MainWindow::refreshCleanupPages(){
    if(!m_workflowCleanup)return;
    auto list=m_workflowCleanup->findChild<QListWidget*>("workflowCleanupPages");
    auto filter=m_workflowCleanup->findChild<QComboBox*>("workflowCleanupFilter");
    QSignalBlocker block(list);list->clear();
    QHash<QString,QJsonObject> reviews;int proposed=0,uncertain=0,applied=0;
    for(auto value:m_workflowReceipt.value("pages").toArray()){
        auto row=value.toObject();reviews.insert(row.value("key").toString(),row);
        auto report=row.value("report").toObject();proposed+=!report.value("removed").toArray().isEmpty();uncertain+=!report.value("uncertain").toArray().isEmpty();applied+=row.value("applied").toBool();
    }
    auto pages=workflowPages();
    for(int i=0;i<pages.size();++i){
        auto page=pages[i].toObject();auto row=reviews.value(page.value("key").toString());auto report=row.value("report").toObject();
        int removed=report.value("removed").toArray().size(),review=report.value("uncertain").toArray().size();
        bool stale=!row.isEmpty()&&row.value("input_stamp")!=page.value("input_stamp");
        QString state=row.isEmpty()?text("Henüz incelenmedi","Not analyzed"):stale?text("Ayar değişti · yeniden hazırla","Settings changed · prepare again"):
            text("Silme adayı: %1 · kontrol: %2","Removal candidates: %1 · review: %2").arg(removed).arg(review)+(row.value("applied").toBool()?text(" · uygulandı"," · applied"):text(" · öneri"," · proposal"));
        auto item=new QListWidgetItem(text("Sayfa %1 · ","Page %1 · ").arg(i+1)+state,list);item->setData(Qt::UserRole,i);item->setToolTip(page.value("source").toString());
        item->setHidden((filter->currentIndex()==1&&!removed)||(filter->currentIndex()==2&&!review&&!stale));
    }
    m_workflowCleanup->findChild<QLabel*>("workflowCleanupSummary")->setText(text("%1 sayfa · %2 sayfada silme önerisi · %3 sayfada kontrol · %4 sayfaya uygulandı","%1 pages · %2 with removal proposals · %3 need review · %4 applied").arg(pages.size()).arg(proposed).arg(uncertain).arg(applied));
    showCleanupPage(m_ptrThumbSequence->selectionLeader().id());
}
void MainWindow::showCleanupPage(PageId const& selected){
    if(!m_workflowCleanup)return;
    auto preview=m_workflowCleanup->findChild<QSplitter*>("workflowCleanupPreview");
    while(preview->count())delete preview->widget(0);
    if(!isProjectLoaded()||selected.isNull()){m_cleanupStatus->setText(text("Önce bir kitap açın.","Open a book first."));return;}
    auto sequence=m_ptrPages->toPageSequence(PAGE_VIEW);int index=-1;
    for(size_t i=0;i<sequence.numPages();++i)if(sequence.pageAt(i).id()==selected){index=int(i);break;}
    auto pages=workflowPages();if(index<0||index>=pages.size())return;auto current=pages[index].toObject();QJsonObject review;
    for(auto value:m_workflowReceipt.value("pages").toArray())if(value.toObject().value("key")==current.value("key")){review=value.toObject();break;}
    auto list=m_workflowCleanup->findChild<QListWidget*>("workflowCleanupPages");
    {QSignalBlocker block(list);for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toInt()==index){list->setCurrentRow(i);list->scrollToItem(list->item(i));break;}}
    bool ready=!review.isEmpty()&&review.value("input_stamp")==current.value("input_stamp")&&QFileInfo::exists(review.value("before").toString())&&QFileInfo::exists(review.value("after").toString());
    auto report=review.value("report").toObject();
    m_cleanupStatus->setText(text("Seçili sayfa: %1 · ","Selected page: %1 · ").arg(index+1)+(ready?text("Silme adayı: %1 · belirsiz: %2 · %3","Removal candidates: %1 · uncertain: %2 · %3").arg(report.value("removed").toArray().size()).arg(report.value("uncertain").toArray().size()).arg(review.value("applied").toBool()?text("uygulandı","applied"):text("henüz uygulanmadı","not applied")):text("Temizliği hazırla ile bu sayfanın önerisini oluşturun.","Use Prepare cleanup to analyze this page.")));
    for(auto side:{"before","after"}){
        auto host=new QWidget;auto layout=new QVBoxLayout(host);layout->addWidget(new QLabel(text("Sayfa %1 · ","Page %1 · ").arg(index+1)+(QString(side)=="before"?text("Önce","Before"):text("Sonra · temizlik önerisi","After · cleanup proposal"))));
        QString path=ready?review.value(side).toString():QString(side)=="before"?current.value("source").toString():QString();
        if(!ready&&QString(side)=="before"&&!QFileInfo::exists(path))path=selected.imageId().filePath();
        QImage image(path);if(!image.isNull())layout->addWidget(new BasicImageView(std::make_shared<NonAcceleratedOperations>(),image),1);
        else {auto note=new QLabel(text("Bu sayfada önizleme hazır değil. Temizliği hazırla düğmesini kullanın.","No preview for this page yet. Use Prepare cleanup."));note->setWordWrap(true);layout->addWidget(note);}
        preview->addWidget(host);
    }
}
void MainWindow::showWorkflowTools(){
    if(!m_workflowReady){m_unifiedDock->show();m_unifiedDock->raise();return;}
    filterList->selectStep("settings");selectWorkflowRow(unified_workflow::row("settings"));
}
bool MainWindow::selectWorkflowRow(int row){
    auto id=filterList->stepId(row);if(id.isEmpty())return false;
    if(isBatchProcessingInProgress()){
        // Menu shortcuts can select a row even when the stage list is disabled.
        // Do not let that clear the active batch or replace its editor context.
        QSignalBlocker selection(filterList->selectionModel());filterList->selectStep(m_workflowStep);
        QMainWindow::statusBar()->showMessage(text(
            "İşlem devam ediyor. Aşama değiştirmek için tamamlanmasını bekleyin veya Durdur'u kullanın.",
            "Processing continues. Wait for completion or use Stop before changing stages."),8000);
        return false;
    }
    if(m_unifiedViewStack->currentWidget()==m_queuePreview&&id!="input"&&id!="settings"&&id!="help"){
        m_unified->editPreview(qMax(0,filterList->filterForRow(row)),id);return false;
    }
    filterList->clearQueueProgress();
    m_workflowStep=id;m_unified->setWorkflowContext(id);
    dockWidgetFilters->setWindowTitle(text("Aşamalar · resimden PDF’ye","Stages · images to PDF"));
    if(m_geometryReviewBar)m_geometryReviewBar->setVisible(id=="ai_geometry");
    if(m_contentReviewBar)m_contentReviewBar->setVisible(id=="ai_content");
    scrollArea->parentWidget()->setVisible(filterList->filterForRow(row)>=0);
    dockWidgetThumbnails->setVisible(filterList->filterForRow(row)>=0||id=="cleanup");
    if(filterList->filterForRow(row)>=0){returnToProjectView();return true;}
    if(id=="input")m_unifiedViewStack->setCurrentWidget(m_workflowInput);
    else if(id=="help")m_unifiedViewStack->setCurrentWidget(m_workflowHelp);
    else if(id=="cleanup"){m_unifiedViewStack->setCurrentWidget(m_workflowCleanup);refreshCleanupPages();}
    else if(id=="storage"){m_unifiedViewStack->setCurrentWidget(m_workflowCache);syncWorkflowCache(true);}
    else {
        m_unifiedViewStack->setCurrentWidget(m_workflowTools);
        auto step=unified_workflow::steps().at(row);m_workflowTitle->setText((m_editBinding.isEmpty()?QString():m_editBinding.value("title").toString()+" · ")+step.title+"\n"+step.description);
        m_workflowRun->setVisible(id=="pdf"||id=="ocr");m_workflowRun->setText(id=="ocr"?text("▶ Seçili ayarlarla OCR’ı başlat","▶ Start OCR with selected settings"):text("▶ PDF oluştur","▶ Create PDF"));
        if(id=="pdf"||id=="ocr")m_unified->showToolsTab(id=="ocr"?3:1);
    }
    filterList->setBatchProcessingPossible(isProjectLoaded());refreshWorkflowState();return false;
}
bool MainWindow::importWorkflowImages(QStringList files){
    if(files.isEmpty()){QMessageBox::information(this,text("Girdi","Input"),text("Bu klasörde desteklenen resim bulunamadı.","No supported images found in this folder."));return false;}
    if(isBatchProcessingInProgress()||m_unified->busy()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return false;
    std::sort(files.begin(),files.end(),SmartFilenameOrdering());files.removeDuplicates();std::vector<ImageFileInfo> loaded;QStringList failed;
    for(auto const& path:files){ImageFileInfo file(QFileInfo(path),{});auto result=ImageMetadataLoader::load(path,[&](ImageMetadata const& m){file.imageInfo().push_back(m);});if(result==ImageMetadataLoader::LOADED)loaded.push_back(file);else failed<<path;}
    if(!failed.isEmpty()){QMessageBox::warning(this,text("Okunamayan resimler","Unreadable images"),failed.join("\n"));return false;}if(loaded.empty())return false;
    if(!closeProjectInteractive())return false;
    QString base=m_unified->workflowOutput();if(base.isEmpty())base=QFileInfo(files.front()).absolutePath()+"/output";
    QString out=base+"/"+QFileInfo(files.front()).dir().dirName()+"-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)+"/images";if(!QDir().mkpath(out))return false;
    switchToNewProject(IntrusivePtr<ProjectPages>(new ProjectPages(loaded,ProjectPages::AUTO_PAGES,Qt::LeftToRight)),out);
    m_workflowReview.clear();m_workflowPdf.clear();m_workflowPdfStamp.clear();m_workflowReceipt={};
    syncWorkflowCache();m_workflowCache->own(out,"native");
    auto imported=QFileInfo(files.front()).absoluteDir();imported.cdUp();imported.cdUp();
    if(QFileInfo::exists(imported.absoluteFilePath(".stou-import-session.json")))m_workflowCache->own(imported.absolutePath(),"sources");
    filterList->selectFilter(0);selectWorkflowRow(unified_workflow::row("orientation"));
    return true;
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
    auto options=m_unified->workflowOptions();for(auto key:{"ocr_engine","ocr_device","tess_quality","paddle_language","paddle_version","language","threads","root"})options.remove(key);
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
    bool aiEnabled=QSettings().value("unified/ai_enabled",false).toBool();
    auto aiChoice=m_workflowInput->findChild<QCheckBox*>("workflowAiEnabled");
    if(aiChoice->isChecked()!=aiEnabled){QSignalBlocker block(aiChoice);aiChoice->setChecked(aiEnabled);filterList->setAiEnabled(aiEnabled);}
    aiChoice->setEnabled(!m_aiGeometryRunning&&!m_aiContentRunning&&!isBatchProcessingInProgress());
    if(auto paths=m_workflowInput->findChild<QLabel*>("workflowPaths"))paths->setText(text("PDF klasörü: ","PDF folder: ")+m_unified->workflowOutput()+"\n"+text("Çalışma kaydı: ","Saved run: ")+m_unified->queuePath()+"\n"+text("Açık projenin resim çıktısı: ","Open project's output images: ")+m_outFileNameGen.outDir());
    if(auto folder=m_workflowTools->findChild<QLineEdit*>("workflowPdfOutputFolder"))folder->setText(m_unified->workflowOutput());
    bool running=m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning;
    bool available=isProjectLoaded()&&!running&&!isBatchProcessingInProgress()&&!m_unified->busy()&&!m_aiGeometryRunning&&!m_aiContentRunning&&!m_workflowCache->cleaning();
    m_workflowStop->setEnabled(running);m_workflowRun->setEnabled(available);
    if(!m_editBinding.isEmpty()&&(m_workflowStep=="pdf"||m_workflowStep=="ocr")){
        m_workflowRun->setText(text("▶ Kaydet ve bu kitabın PDF’sini güncelle","▶ Save and update this book's PDF"));
        m_workflowRun->setEnabled(isProjectLoaded()&&!running&&!isBatchProcessingInProgress()&&!m_aiGeometryRunning&&!m_aiContentRunning&&!m_workflowCache->cleaning());
    }
    filterList->setBatchProcessingPossible(available);
    for(auto name:{"workflowRefreshPdf","workflow_cleanup","workflowCleanupManual","workflowOcrRun","workflowFinish"})if(auto button=findChild<QPushButton*>(name))button->setEnabled(available);
    for(auto name:{"workflowInputImages","workflowInputFolder","workflowInputPdf","workflowInputProject","workflowInputSetup","workflowInputOutput","workflowPdfBrowse"})if(auto button=findChild<QPushButton*>(name))button->setEnabled(!running&&!isBatchProcessingInProgress()&&!m_unified->busy());
    if(running){filterList->setStepStatus(m_workflowProcess->property("stage").toString(),unified_workflow::duration(m_workflowElapsed.elapsed()));}
    QString key=m_outFileNameGen.outDir();
    if(key!=m_workflowProjectKey){
        m_workflowProjectKey=key;m_workflowPdf.clear();m_workflowVisualPdf.clear();m_workflowVisualHash.clear();m_workflowPdfStamp.clear();m_workflowReview.clear();m_workflowReceipt={};m_workflowLastPages={};
        QFile saved(key+"/.workflow-state.json");if(!key.isEmpty()&&saved.open(QIODevice::ReadOnly)){auto state=QJsonDocument::fromJson(saved.readAll()).object();m_workflowPdf=state.value("pdf").toString();m_workflowVisualPdf=state.value("visual_pdf").toString();m_workflowVisualHash=state.value("visual_sha256").toString();m_workflowPdfStamp=state.value("stamp").toString().toLatin1();m_workflowReview=state.value("cleanup").toString();m_workflowLastPages=state.value("pages").toArray();QFile review(m_workflowReview);if(review.open(QIODevice::ReadOnly))m_workflowReceipt=QJsonDocument::fromJson(review.readAll()).object();}
    }
    findChild<QPushButton*>("workflowOpenPdf")->setEnabled(QFileInfo::exists(m_workflowPdf));
    findChild<QPushButton*>("workflowFinishOpen")->setEnabled(QFileInfo::exists(m_workflowPdf));
    findChild<QPushButton*>("workflow_cleanup_apply")->setEnabled(available&&!m_workflowReview.isEmpty());
    findChild<QPushButton*>("workflow_cleanup_undo")->setEnabled(available&&!m_workflowReview.isEmpty()&&m_workflowReceipt.value("applied").toBool());
    if(m_unified->pdfImportRunning())m_workflowDirty->setText(text("PDF içe aktarımı devam ediyor: ","PDF import continues: ")+m_unified->pdfImportStatus()+"\n"+text("İlerlemeye dönmek için PDF içe aktar düğmesini kullanın.","Use Import PDF to return to its progress."));
    else if(running)m_workflowDirty->setText(text("İşlem arka planda devam ediyor: ","Processing continues in the background: ")+m_workflowStatus->text());
    else m_workflowDirty->setText(!isProjectLoaded()?text("0 · Girdi ile bir kitap açın.","Open a book at 0 · Input."):m_workflowPdf.isEmpty()?text("Çıktı → temizlik → PDF → OCR\nPDF henüz oluşturulmadı.","Output → cleanup → PDF → OCR\nPDF has not been created yet."):(!QFileInfo::exists(m_workflowPdf)||workflowFingerprint()!=m_workflowPdfStamp)?text("Ayarlar değişti. Çıktı/PDF yenilenmeli. Eski PDF korunuyor.","Settings changed. Refresh Output/PDF. The previous PDF is preserved."):text("PDF güncel kayıtlı ayarlarla oluşturuldu.","PDF was created with the current saved settings."));
    bool queueView=m_unifiedViewStack->currentWidget()==m_queuePreview;
    auto all=findChild<QCheckBox*>("workflowShowAllStages");
    {QSignalBlocker block(all);all->setEnabled(!queueView);all->setChecked(queueView||QSettings().value("unified/all_workflow_stages",false).toBool());}
    if(queueView){
        auto job=m_unified->previewJob();auto status=job.value("status").toString();
        filterList->showQueueProgress(unifiedQueueWorkflowStatuses(job),unifiedQueueWorkflowStep(status));
        dockWidgetFilters->setWindowTitle(text("Aşamalar · ","Stages · ")+job.value("title").toString());
        auto row=unified_workflow::row(unifiedQueueWorkflowStep(status));
        auto stage=row>=0?unified_workflow::steps().at(row).title:unified_i18n::display(status);
        m_workflowDirty->setText(job.value("title").toString()+"\n"+stage+"\n"+unified_i18n::display(job.value("detail").toString()));
    }else filterList->clearQueueProgress();
}
void MainWindow::runWorkflowStep(QString const& id){
    if(id.startsWith("ai_")&&!QSettings().value("unified/ai_enabled",false).toBool())return;
    if((id=="pdf"||id=="ocr")&&!m_editBinding.isEmpty()){
        if(!isProjectLoaded()||m_aiGeometryRunning||m_aiContentRunning||m_workflowCache->cleaning()||isBatchProcessingInProgress()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return;
        auto error=m_unified->workflowOcrValidation();
        if(!error.isEmpty()&&m_unified->ocrTiming()!="none"){m_workflowStatus->setText(error);return;}
        publishQueueEdit();return;
    }
    if(!isProjectLoaded()||m_aiGeometryRunning||m_aiContentRunning||m_workflowCache->cleaning()||m_unified->busy()||isBatchProcessingInProgress()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return;
    if(id=="ocr"){
        auto error=m_unified->workflowOcrValidation();
        if(!error.isEmpty()){m_workflowStatus->setText(error);QMainWindow::statusBar()->showMessage(error,15000);return;}
        if(m_workflowPdf.isEmpty()||!QFileInfo::exists(m_workflowPdf)||workflowFingerprint()!=m_workflowPdfStamp){m_workflowOcrPending=true;runWorkflowStep("pdf");return;}
    }
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
        if(id=="ai_geometry"){for(auto const& sample:unified_geometry::collect(*m_ptrPages,*m_ptrStages))m_workflowPageTargets.erase(sample.page.id());}
        if(id=="ai_content"){for(auto const& sample:unified_content::collect(*m_ptrPages,*m_ptrStages))m_workflowPageTargets.erase(sample.page.id());}
        if(m_workflowPageTargets.empty()){m_workflowPartial=false;startWorkflowProcess(id);return;}
        QString normal=id=="ai_geometry"?"geometry":id=="ai_content"?"content":"output";
        filterList->selectStep(normal);m_workflowPending=id;startBatchProcessing();return;
    }
    if(id=="storage"){QElapsedTimer elapsed;elapsed.start();syncWorkflowCache();m_workflowCache->clean();filterList->setStepStatus(id,unified_workflow::duration(elapsed.elapsed()));return;}
    if(id=="ocr"){startWorkflowProcess(id);return;}
    if(filterList->filterForRow(unified_workflow::row(id))>=0)startBatchProcessing();
}
void MainWindow::startWorkflowProcess(QString const& mode){
    if(mode=="ai_geometry"||mode=="ai_content"){stageReviewAction(mode=="ai_geometry",m_aiRequeueTargets.empty()?UnifiedStageReviewBar::Optimize:UnifiedStageReviewBar::Analyze);return;}
    if(!isProjectLoaded()||m_unified->busy()||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return;
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
    request.insert("cache_owner",m_outFileNameGen.outDir());request.insert("cancel_file",folder+"/cancel");request.insert("saved_project",m_projectFile);
    request.insert("output_dir",m_unified->workflowOutput());request.insert("project_fingerprint",QString::fromLatin1(workflowFingerprint()));
    request.insert("cleanup_review",m_workflowReview);request.insert("previous_pdf",m_workflowPdf);request.insert("source_pdf",m_workflowVisualPdf);request.insert("source_pdf_sha256",m_workflowVisualHash);
    QString title=m_projectFile.isEmpty()?QFileInfo(m_outFileNameGen.outDir()).dir().dirName():QFileInfo(m_projectFile).completeBaseName();request.insert("title",title);
    QString requestPath=folder+"/request.json";QSaveFile file(requestPath);if(!file.open(QIODevice::WriteOnly)){fail(file.errorString());return;}file.write(QJsonDocument(request).toJson());if(!file.commit()){fail(file.errorString());return;}
    m_workflowResult=folder+"/result.json";
    if(m_workflowProcess)m_workflowProcess->deleteLater();
    auto process=new QProcess(this);m_workflowProcess=process;process->setObjectName("workflowProcess");
    QString stage=mode=="finalize"?"storage":mode.startsWith("cleanup")?"cleanup":mode;process->setProperty("stage",stage);process->setProperty("finishing",mode=="finalize");process->setProperty("fingerprint",workflowFingerprint());process->setProperty("project",m_outFileNameGen.outDir());process->setProperty("work",folder);
    bool hadOcr=false;for(auto value:m_workflowLastPages)hadOcr|=value.toObject().value("has_ocr").toBool();process->setProperty("refreshOcr",mode=="pdf"&&(hadOcr||m_workflowOcrPending));if(mode=="pdf")m_workflowOcrPending=false;
    auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("PYTHONPATH",root+"/src");env.insert("OMP_NUM_THREADS",QString::number(request.value("threads").toInt(2)));process->setProcessEnvironment(env);process->setWorkingDirectory(root);
    m_workflowElapsed.start();m_workflowStatus->setText(text("İşlem sürüyor…","Processing…"));m_cleanupStatus->setText(m_workflowStatus->text());
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process]{
        auto buffer=process->property("buffer").toByteArray()+process->readAllStandardOutput();int end;
        while((end=buffer.indexOf('\n'))>=0){auto line=buffer.left(end);buffer.remove(0,end+1);if(line.startsWith("STOUWORK ")){auto event=QJsonDocument::fromJson(line.mid(9)).object();QString phase=event.value("phase").toString();
            if(phase=="layout")phase=text("Yerleşim ölçülüyor","Measuring layout");else if(phase=="render")phase=text("PDF sayfaları hazırlanıyor","Rendering PDF pages");else if(phase=="verify")phase=text("OCR’lı PDF doğrulanıyor","Verifying searchable PDF");else if(phase=="cleanup")phase=text("Dosyalar temizleniyor","Removing work files");else phase=text("Sayfalar işleniyor","Processing pages");
            QString status=phase+" · "+QString::number(event.value("page").toInt())+"/"+QString::number(event.value("total").toInt())+" · "+unified_workflow::duration(m_workflowElapsed.elapsed());m_workflowStatus->setText(status);m_cleanupStatus->setText(status);if(process->property("finishing").toBool())m_workflowCache->showProgress(event,status);}}
        process->setProperty("buffer",buffer);
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process,mode,stage](int code,QProcess::ExitStatus status){
        for(auto name:{"image-pdf.part.pdf","fitted-pdf.part.pdf"})QFile::remove(process->property("work").toString()+"/"+name);
        QFile result(m_workflowResult);QJsonObject data;if(result.open(QIODevice::ReadOnly))data=QJsonDocument::fromJson(result.readAll()).object();
        result.close();
        if(mode=="finalize"){
            bool success=code==0&&status==QProcess::NormalExit&&data.value("ok").toBool();
            auto detail=success?text("Tamamlandı · yalnız OCR’lı PDF bırakıldı. Proje kapatıldı.","Completed · only the searchable PDF remains. Project closed."):
                text("Temizlik tamamlanmadı; OCR’lı PDF korundu. ","Cleanup did not complete; searchable PDF is preserved. ")+QString::fromUtf8(process->readAllStandardError()).right(700);
            if(success){
                if(!data.value("retained").toArray().isEmpty())detail=text("Proje kapatıldı; OCR’lı PDF ve sahipliği doğrulanamayan %1 dosya korundu.","Project closed; searchable PDF and %1 files with unverified ownership retained.").arg(data.value("retained").toArray().size());
                auto pdf=data.value("pdf").toString();closeProjectWithoutSaving();m_workflowCache->context({}, {}, {}, {}, {});refreshWorkflowState();m_workflowPdf=pdf;
                auto folder=process->property("work").toString();for(auto name:{"request.json","result.json",".stou-cache-owner.json","cancel"})QFile::remove(folder+"/"+name);QDir().rmdir(folder);
                filterList->selectStep("storage");detail+="\n"+text("Silindi: %1 · %2 dosya","Removed: %1 · %2 files").arg(UnifiedProjectCache::sizeText(qint64(data.value("bytes").toDouble()))).arg(data.value("files").toInt())+"\n"+pdf;
            }else if(!data.value("skipped").toArray().isEmpty())detail+="\n"+text("Kaldırılamayan dosya: ","Files not removed: ")+QString::number(data.value("skipped").toArray().size());
            m_workflowCache->showProgress({{"page",1},{"total",1}},detail);m_workflowStatus->setText(detail);refreshWorkflowState();return;
        }
        auto message=code==0&&status==QProcess::NormalExit&&data.value("ok").toBool()?text("Tamamlandı · ","Completed · ")+unified_workflow::duration(m_workflowElapsed.elapsed()):text("İşlem tamamlanmadı: ","Operation did not complete: ")+QString::fromUtf8(process->readAllStandardError()).right(900);
        m_workflowStatus->setText(message);m_cleanupStatus->setText(message);filterList->setStepStatus(stage,unified_workflow::duration(m_workflowElapsed.elapsed()));
        if(code==0&&data.value("ok").toBool()&&process->property("project").toString()==m_outFileNameGen.outDir()){
            if(mode=="cleanup"||mode.startsWith("cleanup_")){if(mode.startsWith("cleanup_"))m_workflowPdfStamp.clear();m_workflowReview=data.value("receipt").toString();m_workflowReceipt=data;
                refreshCleanupPages();
                m_cleanupStatus->setText(message+" · "+(data.value("applied").toBool()?text("PDF'ye dahil edilecek","Will be included in PDF"):text("Öneri; henüz uygulanmadı","Proposal; not applied")));
            }else if(mode=="pdf"||mode=="ocr"){m_workflowPdf=data.value("pdf").toString();m_workflowLastPages=data.value("records").toArray();m_workflowVisualPdf=m_workflowPdf;m_workflowVisualHash=data.value("sha256").toString();m_workflowPdfStamp=process->property("fingerprint").toByteArray();m_workflowStatus->setText(message+"\n"+text("Yenilenen / korunan sayfa: ","Updated / preserved pages: ")+QString::number(data.value("changed_pages").toArray().size())+" / "+QString::number(data.value("reused_pages").toArray().size())+"\n"+m_workflowPdf);}
        }
        saveWorkflowState();refreshWorkflowState();syncWorkflowCache();
        bool success=code==0&&status==QProcess::NormalExit&&data.value("ok").toBool()&&process->property("project").toString()==m_outFileNameGen.outDir();
        if(success&&(mode=="pdf"||mode=="ocr")){
            m_workflowCache->producedPdf(m_workflowPdf);
            auto reading=readingPdf(m_workflowPdf);m_workflowCache->producedPdf(reading);
            if(reading!=m_workflowPdf){
                auto compressed=data.value("compressed").toObject();
                m_workflowStatus->setText(m_workflowStatus->text()+"\n"+text("Okuma kopyası: ","Reading copy: ")+reading+"\n"+unified_i18n::display(compressed.value("note").toString()));
            }
            QStringList stale;for(auto page:data.value("cleanup_needs_review").toArray())stale<<QString::number(page.toInt());
            if(!stale.isEmpty())m_workflowStatus->setText(m_workflowStatus->text()+"\n"+text("Yeni düzeltme nedeniyle bu sayfaların leke temizliği tekrar incelenmeli: ","Review stain cleanup again on these edited pages: ")+stale.join(", "));
            m_workflowCache->clean(mode);
        }
        if(success&&process->property("refreshOcr").toBool())QTimer::singleShot(0,this,[this]{runWorkflowStep("ocr");});
    });
    connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error){if(error==QProcess::FailedToStart){m_workflowStatus->setText(process->errorString());m_cleanupStatus->setText(process->errorString());refreshWorkflowState();}});
    process->start(unified_runtime::python(root),{"-u","-m","scantailor_ocr_unified.workflow_stages",requestPath});refreshWorkflowState();
}
void MainWindow::finalizeWorkflowProject(){
    if(!isProjectLoaded()||m_unified->busy()||isBatchProcessingInProgress()||m_aiGeometryRunning||m_aiContentRunning||(m_workflowProcess&&m_workflowProcess->state()!=QProcess::NotRunning))return;
    bool complete=!m_workflowLastPages.isEmpty();for(auto value:m_workflowLastPages)complete&=value.toObject().value("has_ocr").toBool();
    if(!complete||!QFileInfo::exists(m_workflowPdf)||workflowFingerprint()!=m_workflowPdfStamp){m_workflowCache->showProgress({{"page",0},{"total",1}},text("Önce güncel ayarlarla OCR’ı tamamlayın. Hiçbir dosya silinmedi.","Complete OCR with current settings first. No files were removed."));return;}
    m_ptrInteractiveQueue->cancelAndClear();m_ptrWorkerThreadPool->shutdown();
    startWorkflowProcess("finalize");
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
