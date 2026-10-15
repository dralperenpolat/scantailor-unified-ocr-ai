/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C) 2015  Joseph Artsimovich <joseph.artsimovich@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "UnifiedI18n.h"
#include <QActionGroup>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "UnifiedRuntime.h"
#include "MainWindow.h"
#include "UnifiedPanel.h"
#include "UnifiedStageTools.h"
#include "UnifiedStageReviewBridge.h"
#include "stages/deskew/Settings.h"
#include "stages/deskew/OptionsWidget.h"
#include "stages/select_content/OptionsWidget.h"
#include <QShortcut>
#include "UnifiedPageAudit.h"
#include "UnifiedContentReviewDialog.h"
#include "UnifiedAiReviewDialog.h"
#include "UnifiedGeometryReviewDialog.h"
#include "stages/page_layout/OptionsWidget.h"
#include "stages/select_content/Settings.h"
#include "UnifiedWheelGuard.h"
#include "UnifiedQueuePreview.h"
#include <QJsonDocument>
#include <QSaveFile>
#include <QTabWidget>
#include "UnifiedQueueThumbnails.h"
#include <QStackedWidget>
#include "ImageViewBase.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include "NewOpenProjectPanel.h"
#include "RecentProjects.h"
#include "WorkerThreadPool.h"
#include "ProjectPages.h"
#include "PageSequence.h"
#include "PageSelectionAccessor.h"
#include "PageSelectionProvider.h"
#include "StageSequence.h"
#include "ThumbnailSequence.h"
#include "PageOrderOption.h"
#include "PageOrderProvider.h"
#include "ProcessingTaskQueue.h"
#include "FileNameDisambiguator.h"
#include "OutputFileNameGenerator.h"
#include "ImageInfo.h"
#include "PageInfo.h"
#include "ImageId.h"
#include "Utils.h"
#include "FilterOptionsWidget.h"
#include "ErrorWidget.h"
#include "AutoRemovingFile.h"
#include "DebugImagesImpl.h"
#include "DebugImageView.h"
#include "TabbedDebugImages.h"
#include "BasicImageView.h"
#include "ProjectWriter.h"
#include "ProjectReader.h"
#include "PaletteReader.h"
#include "ThumbnailPixmapCache.h"
#include "ThumbnailFactory.h"
#include "ContentBoxPropagator.h"
#include "PageOrientationPropagator.h"
#include "ProjectCreationContext.h"
#include "ProjectOpeningContext.h"
#include "SkinnedButton.h"
#include "SystemLoadWidget.h"
#include "ProcessingIndicationWidget.h"
#include "ImageMetadataLoader.h"
#include "SmartFilenameOrdering.h"
#include "OrthogonalRotation.h"
#include "LoadFilesStatusDialog.h"
#include "SettingsDialog.h"
#include "AbstractRelinker.h"
#include "RelinkingDialog.h"
#include "OutOfMemoryHandler.h"
#include "OutOfMemoryDialog.h"
#include "stages/fix_orientation/Filter.h"
#include "stages/fix_orientation/Task.h"
#include "stages/fix_orientation/CacheDrivenTask.h"
#include "stages/page_split/Filter.h"
#include "stages/page_split/Task.h"
#include "stages/page_split/CacheDrivenTask.h"
#include "stages/deskew/Filter.h"
#include "stages/deskew/Task.h"
#include "stages/deskew/CacheDrivenTask.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/Task.h"
#include "stages/select_content/CacheDrivenTask.h"
#include "stages/page_layout/Filter.h"
#include "stages/page_layout/Task.h"
#include "stages/page_layout/CacheDrivenTask.h"
#include "stages/output/Filter.h"
#include "stages/output/Task.h"
#include "stages/output/CacheDrivenTask.h"
#include "acceleration/DefaultAccelerationProvider.h"
#include "LoadFileTask.h"
#include "CompositeCacheDrivenTask.h"
#include "ScopedIncDec.h"
#include "ui_AboutDialog.h"
#include "ui_RemovePagesDialog.h"
#include "ui_BatchProcessingLowerPanel.h"
#include "config.h"
#include "version.h"

class MainWindow::PageSelectionProviderImpl : public PageSelectionProvider
{
public:
    PageSelectionProviderImpl(MainWindow* wnd) : m_ptrWnd(wnd) {}

    virtual PageSequence allPages() const
    {
        return m_ptrWnd ? m_ptrWnd->allPages() : PageSequence();
    }

    virtual std::set<PageId> selectedPages() const
    {
        return m_ptrWnd ? m_ptrWnd->selectedPages() : std::set<PageId>();
    }

    std::vector<PageRange> selectedRanges() const
    {
        return m_ptrWnd ? m_ptrWnd->selectedRanges() : std::vector<PageRange>();
    }
private:
    QPointer<MainWindow> m_ptrWnd;
};


MainWindow::MainWindow()
    :	m_ptrPages(new ProjectPages),
      m_ptrStages(new StageSequence(m_ptrPages, newPageSelectionAccessor())),
      m_ptrWorkerThreadPool(new WorkerThreadPool),
      m_ptrInteractiveQueue(new ProcessingTaskQueue),
      m_pAccelerationProvider(new DefaultAccelerationProvider(this)),
      m_ptrOutOfMemoryDialog(new OutOfMemoryDialog),
      m_curFilter(0),
      m_ignoreSelectionChanges(0),
      m_ignorePageOrderingChanges(0),
      m_debug(false),
      m_closing(false)
{
    m_maxLogicalThumbSize = QSize(250, 160);
    m_ptrThumbSequence.reset(new ThumbnailSequence(m_maxLogicalThumbSize));

    setupUi(this);
    horizontalLayout->removeWidget(imageViewFrame);
    m_unifiedViewStack = new QStackedWidget(centralwidget);
    horizontalLayout->addWidget(m_unifiedViewStack);
    m_editorHost=new QWidget;auto editorLayout=new QVBoxLayout(m_editorHost);editorLayout->setContentsMargins(0,0,0,0);
    m_editBar=new QWidget;auto editLayout=new QVBoxLayout(m_editBar);editLayout->setContentsMargins(4,2,4,2);editLayout->setSpacing(2);
    m_editLabel=new QLabel;m_editLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);editLayout->addWidget(m_editLabel);
    auto actions=new QHBoxLayout;editLayout->addLayout(actions);
    auto publishEdit=new QPushButton(unified_i18n::text("Kaydet ve PDF'yi güncelle"));publishEdit->setObjectName("publishQueueEdit");actions->addWidget(publishEdit);
    connect(publishEdit,&QPushButton::clicked,this,&MainWindow::publishQueueEdit);
    auto showPdf=new QPushButton(unified_i18n::text("PDF'ye dön"));actions->addWidget(showPdf);actions->addStretch();
    connect(showPdf,&QPushButton::clicked,this,[this]{m_unified->previewBook(m_editBinding.value("job_id").toString(),m_editBinding.value("page").toInt());});
    m_keepEditMargins=new QCheckBox(unified_i18n::text("PDF'de projenin kenarlarını koru"));m_keepEditMargins->setChecked(true);editLayout->addWidget(m_keepEditMargins);
    m_keepEditMargins->setToolTip(unified_i18n::text("Açık: düzenlediğiniz sayfanın tamamı kenarlarıyla A4'e sığar. Kapalı: PDF / OCR ayarlarındaki otomatik içerik doldurma uygulanır."));
    connect(m_keepEditMargins,&QCheckBox::toggled,[this](bool checked){if(!m_editBinding.isEmpty())m_editBinding.insert("preserve_margins",checked);});
    editorLayout->addWidget(m_editBar);editorLayout->addWidget(imageViewFrame,1);m_editBar->hide();
    m_unifiedViewStack->addWidget(m_editorHost);
    m_queuePreview = new UnifiedQueuePreview;
    m_unifiedViewStack->addWidget(m_queuePreview);
    m_stagePreviewHost=new QWidget;m_stagePreviewLayout=new QVBoxLayout(m_stagePreviewHost);
    m_stagePreviewNote=new QLabel;m_stagePreviewNote->setWordWrap(true);m_stagePreviewLayout->addWidget(m_stagePreviewNote);
    auto backFromReview=new QPushButton(unified_i18n::text("Sayfa düzenleyicisine dön"));m_stagePreviewLayout->addWidget(backFromReview);connect(backFromReview,&QPushButton::clicked,this,&MainWindow::returnToProjectView);
    m_unifiedViewStack->addWidget(m_stagePreviewHost);
    m_queuePreview->returnToEditor = [this]{returnToProjectView();};
    m_queuePreview->editRequested = [this](int stage){m_unified->editPreview(stage);};
    m_queueThumbStack = new QStackedWidget;
    m_queueThumbStack->addWidget(dockWidgetThumbnails->widget());
    m_queueThumbnails = new UnifiedQueueThumbnails;
    m_queueThumbStack->addWidget(m_queueThumbnails);
    dockWidgetThumbnails->setWidget(m_queueThumbStack);
    m_queueThumbnails->selectionChanged = [this](QString const& path){m_queuePreview->setCurrentPath(path);};
    m_queuePreview->currentPathChanged = [this](QString const& path){m_queueThumbnails->setCurrentPath(path);if(m_unified)m_unified->setPreviewPath(path);};
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    dockWidgetFilters->setFeatures(dockWidgetFilters->features()|QDockWidget::DockWidgetClosable);
    dockWidgetFilters->setMinimumWidth(300);
    dockWidgetThumbnails->setFeatures(dockWidgetThumbnails->features()|QDockWidget::DockWidgetClosable);
    dockWidgetThumbnails->setMinimumWidth(120);
    dockWidgetThumbnails->setMaximumWidth(QWIDGETSIZE_MAX);
    sortOptions->setVisible(false);
    m_unified = new UnifiedPanel(this);
    m_unifiedDock = new QDockWidget(unified_i18n::text("OCR / Çoklu kitap / Kaynak kullanımı"), this);
    m_unifiedDock->setObjectName("unifiedJobsDock");
    m_unifiedDock->setWidget(m_unified);
    addDockWidget(Qt::BottomDockWidgetArea, m_unifiedDock);
    m_unifiedDock->hide();
    m_unifiedDock->installEventFilter(this);
    connect(m_unifiedDock,&QDockWidget::visibilityChanged,[this](bool shown){if(shown)resizeDocks({m_unifiedDock},{QSettings().value("unified/panel_height",370).toInt()},Qt::Vertical);});
    connect(m_unified,&UnifiedPanel::panelHeightRequested,[this](int height){if(m_unifiedDock->isFloating())m_unifiedDock->resize(m_unifiedDock->width(),height);else resizeDocks({m_unifiedDock},{height},Qt::Vertical);});
    auto languageMenu=menuBar()->addMenu(QStringLiteral("Dil / Language"));
    languageMenu->setObjectName("unifiedLanguageMenu");
    auto languageGroup=new QActionGroup(this);languageGroup->setExclusive(true);
    auto languageNote=languageMenu->addAction(unified_i18n::text("Dil değişikliği program yeniden açılınca uygulanır."));
    languageNote->setEnabled(false);languageMenu->addSeparator();
    const auto activeLanguage=qApp->property("unifiedUiLanguage").toString();
    for(auto const& code:{QStringLiteral("tr"),QStringLiteral("en")}) {
        auto action=languageMenu->addAction(code=="tr"?QStringLiteral("Türkçe"):QStringLiteral("English"));
        action->setObjectName("unifiedLanguage_"+code);action->setCheckable(true);
        action->setData(code);action->setChecked(code==activeLanguage);languageGroup->addAction(action);
        connect(action,&QAction::triggered,this,[this,code,activeLanguage,languageNote]{
            QSettings settings;settings.setValue("unified/ui_language",code);settings.sync();
            if(settings.status()!=QSettings::NoError){QMainWindow::statusBar()->showMessage(unified_i18n::text("Dil seçimi kaydedilemedi."),15000);return;}
            const auto note=unified_i18n::text(code==activeLanguage?"Seçili dil zaten kullanılıyor.":"Dil seçimi kaydedildi. Programı yeniden açınca uygulanacak; çalışan kitap kuyruğu devam ediyor.");
            languageNote->setText(note);QMainWindow::statusBar()->showMessage(note,20000);
        });
    }
    auto unifiedMenu = menuBar()->addMenu(unified_i18n::text("OCR / Kitaplar"));
    setAcceptDrops(true);
    auto importPdfAction=unifiedMenu->addAction(unified_i18n::text("PDF’den kitap ekle…"));
    importPdfAction->setObjectName("unifiedImportPdfAction");menuFile->addAction(importPdfAction);
    connect(importPdfAction,&QAction::triggered,this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,unified_i18n::text("İşlenecek PDF"),{},"PDF (*.pdf *.PDF)");
        if(!path.isEmpty())m_unified->importPdf(path);
    });
    connect(m_unified,&UnifiedPanel::toolsRequested,this,[this]{m_unifiedDock->show();m_unifiedDock->raise();});
    unifiedMenu->addAction(unified_i18n::text("Kitap düzenine göre içerik denetimi…"),this,&MainWindow::reviewBookContent);
    auto aiAction=unifiedMenu->addAction(unified_i18n::text("Yapay zekâ ile içerik ve sayfa düzeni…"),this,&MainWindow::reviewAiContent);
    auto geometryAction=unifiedMenu->addAction(unified_i18n::text("Eğriliği karşılaştır · AI dahil…"),this,&MainWindow::reviewGeometry);geometryAction->setShortcut(QKeySequence("Ctrl+Shift+G"));
    aiAction->setObjectName("unifiedAiReviewAction");aiAction->setShortcut(QKeySequence("Ctrl+Shift+A"));
    for(auto pair:{std::make_pair("Ctrl+Alt+A",UnifiedStageReviewBar::Analyze),std::make_pair("Ctrl+Alt+P",UnifiedStageReviewBar::Preview),std::make_pair("Ctrl+Alt+Return",UnifiedStageReviewBar::Apply),std::make_pair("Ctrl+Alt+Backspace",UnifiedStageReviewBar::Undo)}){
        auto shortcut=new QShortcut(QKeySequence(pair.first),this);connect(shortcut,&QShortcut::activated,[this,a=pair.second]{auto bar=m_curFilter==2?m_geometryReviewBar:m_curFilter==3?m_contentReviewBar:QPointer<UnifiedStageReviewBar>{};if(bar&&!isBatchProcessingInProgress())bar->trigger(a);});
    }
    auto reviewTimer=new QTimer(this);connect(reviewTimer,&QTimer::timeout,this,&MainWindow::refreshStageReviews);reviewTimer->start(400);
    auto viewMenu=new QMenu(unified_i18n::text("Görünüm"),this);menuBar()->insertMenu(unifiedMenu->menuAction(),viewMenu);
    dockWidgetFilters->toggleViewAction()->setText(unified_i18n::text("Aşama ve ayar paneli"));
    dockWidgetThumbnails->toggleViewAction()->setText(unified_i18n::text("Sayfa küçük resimleri"));
    viewMenu->addAction(dockWidgetFilters->toggleViewAction());viewMenu->addAction(dockWidgetThumbnails->toggleViewAction());
    viewMenu->addAction(m_unifiedDock->toggleViewAction());
    connect(viewMenu->addAction(unified_i18n::text("Açık proje görüntüsüne dön")),&QAction::triggered,this,&MainWindow::returnToProjectView);
    unifiedMenu->addAction(m_unifiedDock->toggleViewAction());
    for(int tab:{0,1}){auto b=new QPushButton(unified_i18n::text(tab==0?"Kitaplar ▴":"PDF / OCR ▴"));b->setToolTip(unified_i18n::text("Alt paneli bu bölümde aç. Aynı düğmeye tekrar basınca gizle."));QMainWindow::statusBar()->addPermanentWidget(b);connect(b,&QPushButton::clicked,[this,tab]{auto tabs=m_unified->findChild<QTabWidget*>("unifiedToolTabs");if(m_unifiedDock->isVisible()&&tabs&&tabs->currentIndex()==tab)m_unifiedDock->hide();else{m_unified->showToolsTab(tab);m_unifiedDock->show();m_unifiedDock->raise();}});}
    auto pdfImportButton=new QPushButton(unified_i18n::text("PDF içe aktar ▴"));pdfImportButton->setObjectName("unifiedPdfImportShortcut");
    QMainWindow::statusBar()->addPermanentWidget(pdfImportButton);
    connect(pdfImportButton,&QPushButton::clicked,this,[this]{m_unified->openPdfTools();});
    auto audit=new UnifiedPageAudit(this);
    auto auditDock=new QDockWidget(unified_i18n::text("Sayfa ayarları / uygulama kapsamı"),this);
    auditDock->setObjectName("unifiedPageAuditDock");auditDock->setWidget(audit);
    addDockWidget(Qt::BottomDockWidgetArea,auditDock);tabifyDockWidget(m_unifiedDock,auditDock);auditDock->hide();
    unifiedMenu->addAction(auditDock->toggleViewAction());
    viewMenu->addAction(auditDock->toggleViewAction());viewMenu->addSeparator();
    connect(viewMenu->addAction(unified_i18n::text("Panel yerleşimini sıfırla")),&QAction::triggered,[this]{
        restoreState(m_defaultPanelState);dockWidgetFilters->show();dockWidgetThumbnails->show();
        QMainWindow::statusBar()->showMessage(unified_i18n::text("Panel yerleşimi sıfırlandı. Kitabın işleme ayarları korunuyor."),6000);
    });
    auto auditTimer=new QTimer(this);auditTimer->setInterval(1200);
    connect(auditTimer,&QTimer::timeout,[this,audit,auditDock]{
        if(!auditDock->isVisible()||!isProjectLoaded()||isBatchProcessingInProgress())return;
        ProjectWriter writer(m_ptrPages,m_selectedPage,m_outFileNameGen);
        audit->update(writer.toXml(m_ptrStages->filters(),QFileInfo(m_projectFile).absolutePath()));
    });auditTimer->start();
    audit->navigate=[this](int index,int stage){
        if(!isProjectLoaded()||isBatchProcessingInProgress())return;
        auto pages=m_ptrPages->toPageSequence(PAGE_VIEW);if(index<0||size_t(index)>=pages.numPages())return;
        filterList->selectRow(stage);m_ptrThumbSequence->setSelection(pages.pageAt(index).id());
    };
    auto fitAction=unifiedMenu->addAction(unified_i18n::text("Sayfayı ekrana sığdır · yalnız görünüm"));
    fitAction->setShortcut(QKeySequence("Ctrl+0"));fitAction->setToolTip(unified_i18n::text("Tek seferlik yakınlaştırma komutu. PDF boyutu ve içerik alanı değişmez; tekerlekle yeniden yakınlaşabilirsiniz."));
    viewMenu->addAction(fitAction);
    connect(fitAction,&QAction::triggered,[this]{for(auto view:findChildren<ImageViewBase*>())if(view->isVisible())view->setZoomLevel(1.);});
    connect(unifiedMenu->addAction(unified_i18n::text("Bu kitabı OCR kuyruğuna ekle")), &QAction::triggered, this, &MainWindow::captureUnifiedProject);
    connect(unifiedMenu->addAction(unified_i18n::text("Parmak temizle · Fill Zones")), &QAction::triggered, [this] {
        if (!isProjectLoaded() || isBatchProcessingInProgress()) return;
        filterList->selectRow(m_ptrStages->outputFilterIdx());
        QMessageBox::information(this, unified_i18n::text("Parmak temizleme"), unified_i18n::text("Output aşamasında sağdaki Fill Zones / Parmak temizle sekmesini açın. Parmak çevresinde çokgen çizip başlangıç noktasına dönün; dolgu rengini beyaz seçin. Metin üzerini örten parmak, eksik harfler yeniden çekilmeden düzeltilemez."));
    });
    connect(m_unified, &UnifiedPanel::captureRequested, this, &MainWindow::captureUnifiedProject);
    connect(m_unified, &UnifiedPanel::editRequested, this, &MainWindow::openQueuePage);
    connect(m_unified, &UnifiedPanel::openRequested, this, [this](QString const& path){
        // Explicit editing is the only queue action which replaces the editor.
        // Keep its normal save/cancel flow for unsaved manual adjustments.
        if(isBatchProcessingInProgress())return;
        if(QFileInfo(path).absoluteFilePath()==QFileInfo(m_projectFile).absoluteFilePath()){returnToProjectView();return;}
        if(!closeProjectInteractive())return;
        returnToProjectView();openProject(path);
    });
    connect(m_unified,&UnifiedPanel::selectionPreviewRequested,this,[this](QString const& title,QJsonArray const& images,QString const& path){
        m_queuePreview->showSelection(title,images,path);
        m_unifiedViewStack->setCurrentWidget(m_queuePreview);
        m_queueThumbnails->setImages(images,m_queuePreview->currentPath());
        m_queueThumbStack->setCurrentWidget(m_queueThumbnails);
        // Editing controls still belong to the preserved open project.
        dockWidgetFilters->widget()->setEnabled(false);
        dockWidgetFilters->setWindowTitle(unified_i18n::text("Aşamalar · üstte Bu sayfayı düzenle'yi seçin"));
        dockWidgetThumbnails->setWindowTitle(unified_i18n::text("Küçük resimler · ")+title);
    });
    connect(m_unified,&UnifiedPanel::selectionPreviewUpdated,this,[this](QString const& title,QJsonArray const& images,QString const& path){
        if(m_unifiedViewStack->currentWidget()!=m_queuePreview)return;
        m_queuePreview->showSelection(title,images,path);
        m_queueThumbnails->setImages(images,m_queuePreview->currentPath());
    });
    connect(m_unified, &UnifiedPanel::settingsRequested, this, &MainWindow::openSettingsDialog);
    connect(m_unified, &UnifiedPanel::accelerationChanged, m_pAccelerationProvider, &DefaultAccelerationProvider::processUpdatedConfiguration);

    createBatchProcessingWidget();
    m_ptrProcessingIndicationWidget.reset(new ProcessingIndicationWidget);

    filterList->setStages(m_ptrStages);
    filterList->selectRow(0);

    setupThumbView(); // Expects m_ptrThumbSequence to be initialized.

    m_ptrTabbedDebugImages.reset(new TabbedDebugImages);

    m_debug = actionDebug->isChecked();
    m_pImageFrameLayout = new QStackedLayout(imageViewFrame);
    m_pOptionsFrameLayout = new QStackedLayout(filterOptions);

    addAction(actionFirstPage);
    addAction(actionLastPage);
    addAction(actionNextPage);
    addAction(actionPrevPage);
    addAction(actionPrevPageQ);
    addAction(actionNextPageW);

    // Should be enough to save a project.
    OutOfMemoryHandler::instance().allocateEmergencyMemory(3*1024*1024);

    connect(actionFirstPage, SIGNAL(triggered(bool)), SLOT(goFirstPage()));
    connect(actionLastPage, SIGNAL(triggered(bool)), SLOT(goLastPage()));
    connect(actionPrevPage, SIGNAL(triggered(bool)), SLOT(goPrevPage()));
    connect(actionNextPage, SIGNAL(triggered(bool)), SLOT(goNextPage()));
    connect(actionPrevPageQ, SIGNAL(triggered(bool)), this, SLOT(goPrevPage()));
    connect(actionNextPageW, SIGNAL(triggered(bool)), this, SLOT(goNextPage()));
    connect(actionAbout, SIGNAL(triggered(bool)), this, SLOT(showAboutDialog()));
    connect(
        &OutOfMemoryHandler::instance(),
        SIGNAL(outOfMemory()), SLOT(handleOutOfMemorySituation())
    );

    connect(
        filterList->selectionModel(),
        SIGNAL(selectionChanged(QItemSelection const&, QItemSelection const&)),
        this, SLOT(filterSelectionChanged(QItemSelection const&))
    );
    connect(
        filterList, SIGNAL(launchBatchProcessing()),
        this, SLOT(startBatchProcessing())
    );

    connect(
        m_ptrWorkerThreadPool.get(),
        SIGNAL(taskResult(BackgroundTaskPtr const&, FilterResultPtr const&)),
        this, SLOT(filterResult(BackgroundTaskPtr const&, FilterResultPtr const&))
    );

    connect(
        m_ptrThumbSequence.get(),
        SIGNAL(newSelectionLeader(PageInfo const&, QRectF const&, ThumbnailSequence::SelectionFlags)),
        this, SLOT(currentPageChanged(PageInfo const&, QRectF const&, ThumbnailSequence::SelectionFlags))
    );
    connect(
        m_ptrThumbSequence.get(),
        SIGNAL(pageContextMenuRequested(PageInfo const&, QPoint const&, bool)),
        this, SLOT(pageContextMenuRequested(PageInfo const&, QPoint const&, bool))
    );
    connect(
        m_ptrThumbSequence.get(),
        SIGNAL(pastLastPageContextMenuRequested(QPoint const&)),
        SLOT(pastLastPageContextMenuRequested(QPoint const&))
    );

    connect(
        thumbView->verticalScrollBar(), SIGNAL(sliderMoved(int)),
        this, SLOT(thumbViewScrolled())
    );
    connect(
        thumbView->verticalScrollBar(), SIGNAL(valueChanged(int)),
        this, SLOT(thumbViewScrolled())
    );
    connect(
        focusButton, SIGNAL(clicked(bool)),
        this, SLOT(thumbViewFocusToggled(bool))
    );
    connect(
        sortOptions, SIGNAL(currentIndexChanged(int)),
        this, SLOT(pageOrderingChanged(int))
    );

    connect(actionRelinking, SIGNAL(triggered(bool)), SLOT(showRelinkingDialog()));
    connect(actionDebug, SIGNAL(toggled(bool)), SLOT(debugToggled(bool)));

    connect(
        actionSettings, SIGNAL(triggered(bool)),
        this, SLOT(openSettingsDialog())
    );

    connect(
        actionNewProject, SIGNAL(triggered(bool)),
        this, SLOT(newProject())
    );
    connect(
        actionOpenProject, SIGNAL(triggered(bool)),
        this, SLOT(openProject())
    );
    connect(
        actionSaveProject, SIGNAL(triggered(bool)),
        this, SLOT(saveProjectTriggered())
    );
    connect(
        actionSaveProjectAs, SIGNAL(triggered(bool)),
        this, SLOT(saveProjectAsTriggered())
    );
    connect(
        actionCloseProject, SIGNAL(triggered(bool)),
        this, SLOT(closeProject())
    );
    connect(
        actionQuit, SIGNAL(triggered(bool)),
        this, SLOT(close())
    );

    updateProjectActions();
    updateWindowTitle();
    updateMainArea();

    m_defaultPanelState=saveState();
    QSettings settings;
    QVariant const geom(
        settings.value("mainWindow/geometry")
    );
    if (!restoreGeometry(geom.toByteArray()))
    {
        resize(1014, 689); // A sensible value.
    }
    QVariant const state(
        settings.value("mainWindow/windowState")
    );
    restoreState(state.toByteArray());

    filterOptions->installEventFilter(this);
    thumbView->installEventFilter(this);

    setDockNestingEnabled(true);

    m_systemPalette = QApplication::palette();

    QString stylesheetFilePath = settings.value("settings/stylesheet", "").toString();
    if (!stylesheetFilePath.isEmpty())
        stylesheetChanged(stylesheetFilePath);
    setStyleSheet(styleSheet()+QStringLiteral("\nQMainWindow::separator { width: 8px; height: 8px; background: palette(midlight); } QMainWindow::separator:hover { background: palette(highlight); }"));
}


MainWindow::~MainWindow()
{
    // Member-owned thumbnail scenes are destroyed before QObject's children.
    // Their scrollbars can emit while ThumbnailSequence is already tearing down.
    disconnect(thumbView->verticalScrollBar(),nullptr,this,nullptr);
    disconnect(thumbView->horizontalScrollBar(),nullptr,this,nullptr);
    disconnect(m_ptrThumbSequence.get(),nullptr,this,nullptr);
    m_ptrInteractiveQueue->cancelAndClear();
    if (m_ptrBatchQueue.get())
    {
        m_ptrBatchQueue->cancelAndClear();
    }
    m_ptrWorkerThreadPool->shutdown();

    removeWidgetsFromLayout(m_pImageFrameLayout);
    removeWidgetsFromLayout(m_pOptionsFrameLayout);
    m_ptrTabbedDebugImages->clear();

    m_pAccelerationProvider->releaseResources();
}

PageSequence
MainWindow::allPages() const
{
    return m_ptrThumbSequence->toPageSequence();
}

std::set<PageId>
MainWindow::selectedPages() const
{
    return m_ptrThumbSequence->selectedItems();
}

std::vector<PageRange>
MainWindow::selectedRanges() const
{
    return m_ptrThumbSequence->selectedRanges();
}

void
MainWindow::switchToNewProject(
    IntrusivePtr<ProjectPages> const& pages,
    QString const& out_dir, QString const& project_file_path,
    ProjectReader const* project_reader)
{
    returnToProjectView();
    stopBatchProcessing(CLEAR_MAIN_AREA);
    m_ptrInteractiveQueue->cancelAndClear();

    if (!out_dir.isEmpty())
    {
        Utils::maybeCreateCacheDir(out_dir);
    }

    m_ptrPages = pages;
    m_projectFile = project_file_path;
    m_editBinding={};QFile binding(project_file_path+".unified-edit.json");
    if(binding.open(QIODevice::ReadOnly)){auto bytes=binding.readAll();binding.close();m_editBinding=QJsonDocument::fromJson(bytes).object();}
    m_editBar->setVisible(!m_editBinding.isEmpty());
    m_keepEditMargins->setChecked(m_editBinding.value("preserve_margins").toBool(true));
    m_editLabel->setText(m_editBinding.value("title").toString()+unified_i18n::text(" · düzenleme kopyası"));

    if (project_reader)
    {
        m_selectedPage = project_reader->selectedPage();
    }

    IntrusivePtr<FileNameDisambiguator> disambiguator;
    if (project_reader)
    {
        disambiguator = project_reader->namingDisambiguator();
    }
    else
    {
        disambiguator.reset(new FileNameDisambiguator);
    }

    m_outFileNameGen = OutputFileNameGenerator(
                           disambiguator, out_dir, pages->layoutDirection()
                       );
    // These two need to go in this order.
    updateDisambiguationRecords(pages->toPageSequence(IMAGE_VIEW));

    // A review belongs to a project; closing its window only hides it.
    for(auto name:{"unifiedAiReview","unifiedGeometryReview","unifiedBookReview"})if(auto dialog=findChild<QDialog*>(name))delete dialog;
    // Recreate the stages and load their state.
    m_ptrStages.reset(new StageSequence(pages, newPageSelectionAccessor()));
    m_ptrStages->selectContentFilter()->onBookReview(this,[this]{reviewBookContent();});
    m_ptrStages->selectContentFilter()->onAiReview(this,[this]{reviewAiContent();});
    m_ptrStages->deskewFilter()->onGeometryReview(this,[this]{reviewGeometry();});
    bindUnifiedOutputTools(m_unified,m_ptrStages->outputFilter()->optionsWidget());
    bindUnifiedLayoutTools(m_unified,m_ptrStages->pageLayoutFilter()->optionsWidget());
    bindStageReviews();
    if (project_reader)
    {
        project_reader->readFilterSettings(m_ptrStages->filters());
    }

    // Connect the filter list model to the view and select
    // the first item.
    {
        ScopedIncDec<int> guard(m_ignoreSelectionChanges);
        filterList->setStages(m_ptrStages);
        filterList->selectRow(0);
        m_curFilter = 0;

        // Setting a data model also implicitly sets a new
        // selection model, so we have to reconnect to it.
        connect(
            filterList->selectionModel(),
            SIGNAL(selectionChanged(QItemSelection const&, QItemSelection const&)),
            this, SLOT(filterSelectionChanged(QItemSelection const&))
        );
    }

    updateSortOptions();

    m_ptrContentBoxPropagator.reset(
        new ContentBoxPropagator(
            m_ptrStages->pageLayoutFilter(),
            createCompositeCacheDrivenTask(
                m_ptrStages->selectContentFilterIdx()
            )
        )
    );

    m_ptrPageOrientationPropagator.reset(
        new PageOrientationPropagator(
            m_ptrStages->pageSplitFilter(),
            createCompositeCacheDrivenTask(
                m_ptrStages->fixOrientationFilterIdx()
            )
        )
    );

    // Thumbnails are stored relative to the output directory,
    // so recreate the thumbnail cache.
    if (out_dir.isEmpty())
    {
        m_ptrThumbnailCache.reset();
    }
    else
    {
        m_ptrThumbnailCache = Utils::createThumbnailCache(
                                  m_outFileNameGen.outDir(), m_pAccelerationProvider->getOperations()
                              );
    }
    resetThumbSequence(currentPageOrderProvider());

    removeFilterOptionsWidget();
    updateProjectActions();
    updateWindowTitle();
    updateMainArea();

    if (!QDir(out_dir).exists())
    {
        showRelinkingDialog();
    }
}

void
MainWindow::showNewOpenProjectPanel()
{
    std::unique_ptr<QWidget> outer_widget(new QWidget);
    QGridLayout* layout = new QGridLayout(outer_widget.get());
    outer_widget->setLayout(layout);

    NewOpenProjectPanel* nop = new NewOpenProjectPanel(outer_widget.get());

    // We use asynchronous connections because otherwise we
    // would be deleting a widget from its event handler, which
    // Qt doesn't like.
    connect(
        nop, SIGNAL(newProject()),
        this, SLOT(newProject()),
        Qt::QueuedConnection
    );
    connect(
        nop, SIGNAL(openProject()),
        this, SLOT(openProject()),
        Qt::QueuedConnection
    );
    connect(
        nop, SIGNAL(openRecentProject(QString const&)),
        this, SLOT(openProject(QString const&)),
        Qt::QueuedConnection
    );

    layout->addWidget(nop, 1, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(2, 1);
    layout->setRowStretch(0, 1);
    layout->setRowStretch(2, 1);
    setImageWidget(outer_widget.release(), TRANSFER_OWNERSHIP);

    filterList->setBatchProcessingPossible(false);
}

void
MainWindow::createBatchProcessingWidget()
{
    m_ptrBatchProcessingWidget.reset(new QWidget);
    QGridLayout* layout = new QGridLayout(m_ptrBatchProcessingWidget.get());
    m_ptrBatchProcessingWidget->setLayout(layout);

    SkinnedButton* stop_btn = new SkinnedButton(
        ":/icons/stop-big.png",
        ":/icons/stop-big-hovered.png",
        ":/icons/stop-big-pressed.png",
        m_ptrBatchProcessingWidget.get()
    );
    stop_btn->setStatusTip(tr("Stop batch processing"));

    class LowerPanel : public QWidget
    {
    public:
        LowerPanel(QWidget* parent = 0) : QWidget(parent)
        {
            ui.setupUi(this);
        }

        Ui::BatchProcessingLowerPanel ui;
    };
    LowerPanel* lower_panel = new LowerPanel(m_ptrBatchProcessingWidget.get());
    m_checkBeepWhenFinished = [lower_panel]()
    {
        return lower_panel->ui.beepWhenFinished->isChecked();
    };

    int row = 0; // Row 0 is reserved.
    layout->addWidget(stop_btn, ++row, 1, Qt::AlignCenter);
    layout->addWidget(lower_panel, ++row, 0, 1, 3, Qt::AlignHCenter|Qt::AlignTop);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(2, 1);
    layout->setRowStretch(0, 1);
    layout->setRowStretch(row, 1);
    // Native controls above are retained; add live stage, count and pause.
    m_liveLabel = new QLabel(unified_i18n::text("İşlenen sayfa burada görünecek."));
    m_liveProgress = new QProgressBar;
    m_pauseButton = new QPushButton(unified_i18n::text("Duraklat"));
    auto live = new QWidget; m_liveLayout = new QVBoxLayout(live);
    layout->addWidget(m_liveLabel, 0, 0, 1, 3);
    layout->addWidget(live, 1, 0, 1, 3);
    layout->addWidget(m_liveProgress, 2, 0, 1, 3);
    layout->addWidget(stop_btn, 3, 0);
    layout->addWidget(m_pauseButton, 3, 1);
    layout->addWidget(lower_panel, 3, 2);
    layout->setRowStretch(0,0);layout->setRowStretch(1,1);layout->setRowStretch(2,0);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::toggleUnifiedPause);

    connect(stop_btn, SIGNAL(clicked()), SLOT(stopBatchProcessing()));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if(obj==m_unifiedDock && ev->type()==QEvent::Resize && m_unified && m_unifiedDock->isVisible()){
        int height=m_unifiedDock->height();m_unified->setPanelHeight(height);QSettings().setValue("unified/panel_height",height);
    }
    if (obj == filterOptions && ev->type() == QEvent::Resize)
    {
        scrollArea->setMinimumWidth(qMin(220,filterOptions->minimumSizeHint().width()));
    }

    if (obj == thumbView && ev->type() == QEvent::Resize)
    {
        // Allow the user to resize the thumbnail dock on narrow / scaled screens.
        // Its viewport keeps native scrolling and thumbnail ordering intact.
    }

    return false;
}

void
MainWindow::setupThumbView()
{
    int const sb = thumbView->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    int inner_width = thumbView->maximumViewportSize().width() - sb;
    if (thumbView->style()->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents, 0, thumbView))
    {
        inner_width -= thumbView->frameWidth() * 2;
    }
    int const delta_x = thumbView->size().width() - inner_width;
    thumbView->setFixedWidth((int)ceil(m_maxLogicalThumbSize.width() + delta_x));

    thumbView->setStyleSheet("background-color: transparent");
    m_ptrThumbSequence->attachView(thumbView);
}

void
MainWindow::closeEvent(QCloseEvent* const event)
{
    if (m_unified && m_unified->busy() && !m_unified->monitoringExternalWorker()) {
        m_unified->stop(); event->ignore();
        QTimer::singleShot(500, this, [this] { close(); });
        return;
    }
    if (m_closing)
    {
        event->accept();
    }
    else
    {
        event->ignore();
        startTimer(0);
    }
}

void
MainWindow::timerEvent(QTimerEvent* const event)
{
    // We only use the timer event for delayed closing of the window.
    killTimer(event->timerId());

    if (closeProjectInteractive())
    {
        m_closing = true;
        QSettings settings;
        settings.setValue("mainWindow/geometry", saveGeometry());
        settings.setValue("mainWindow/windowState", saveState());
        close();
    }
}

MainWindow::SavePromptResult
MainWindow::promptProjectSave()
{
    QMessageBox::StandardButton const res = QMessageBox::question(
            this, tr("Save Project"), tr("Save this project?"),
            QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,
            QMessageBox::Save
                                            );

    switch (res)
    {
    case QMessageBox::Save:
        return SAVE;
    case QMessageBox::Discard:
        return DONT_SAVE;
    default:
        return CANCEL;
    }
}

bool
MainWindow::compareFiles(QString const& fpath1, QString const& fpath2)
{
    QFile file1(fpath1);
    QFile file2(fpath2);

    if (!file1.open(QIODevice::ReadOnly))
    {
        return false;
    }
    if (!file2.open(QIODevice::ReadOnly))
    {
        return false;
    }

    if (!file1.isSequential() && !file2.isSequential())
    {
        if (file1.size() != file2.size())
        {
            return false;
        }
    }

    int const chunk_size = 4096;
    for (;;)
    {
        QByteArray const chunk1(file1.read(chunk_size));
        QByteArray const chunk2(file2.read(chunk_size));
        if (chunk1.size() != chunk2.size())
        {
            return false;
        }
        else if (chunk1.size() == 0)
        {
            return true;
        }
    }
}

IntrusivePtr<PageOrderProvider const>
MainWindow::currentPageOrderProvider() const
{
    int const idx = sortOptions->currentIndex();
    if (idx < 0)
    {
        return IntrusivePtr<PageOrderProvider const>();
    }

    IntrusivePtr<AbstractFilter> const filter(m_ptrStages->filterAt(m_curFilter));
    return filter->pageOrderOptions()[idx].provider();
}

void
MainWindow::updateSortOptions()
{
    ScopedIncDec<int> const guard(m_ignorePageOrderingChanges);

    IntrusivePtr<AbstractFilter> const filter(m_ptrStages->filterAt(m_curFilter));

    sortOptions->clear();

    BOOST_FOREACH(PageOrderOption const& opt, filter->pageOrderOptions())
    {
        sortOptions->addItem(opt.name());
    }

    sortOptions->setVisible(sortOptions->count() > 0);

    if (sortOptions->count() > 0)
    {
        sortOptions->setCurrentIndex(filter->selectedPageOrder());
    }
}

void
MainWindow::resetThumbSequence(
    IntrusivePtr<PageOrderProvider const> const& page_order_provider)
{
    if (m_ptrThumbnailCache.get())
    {
        IntrusivePtr<CompositeCacheDrivenTask> const task(
            createCompositeCacheDrivenTask(m_curFilter)
        );

        m_ptrThumbSequence->setThumbnailFactory(
            IntrusivePtr<ThumbnailFactory>(
                new ThumbnailFactory(
                    m_ptrThumbnailCache,
                    m_maxLogicalThumbSize, task
                )
            )
        );
    }

    m_ptrThumbSequence->reset(
        m_ptrPages->toPageSequence(getCurrentView()),
        ThumbnailSequence::RESET_SELECTION, page_order_provider
    );

    if (!m_ptrThumbnailCache.get())
    {
        // Empty project.
        assert(m_ptrPages->numImages() == 0);
        m_ptrThumbSequence->setThumbnailFactory(
            IntrusivePtr<ThumbnailFactory>()
        );
    }

    PageId const page(m_selectedPage.get(getCurrentView()));
    if (m_ptrThumbSequence->setSelection(page))
    {
        // OK
    }
    else if (m_ptrThumbSequence->setSelection(PageId(page.imageId(), PageId::LEFT_PAGE)))
    {
        // OK
    }
    else if (m_ptrThumbSequence->setSelection(PageId(page.imageId(), PageId::RIGHT_PAGE)))
    {
        // OK
    }
    else if (m_ptrThumbSequence->setSelection(PageId(page.imageId(), PageId::SINGLE_PAGE)))
    {
        // OK
    }
    else
    {
        // Last resort.
        m_ptrThumbSequence->setSelection(m_ptrThumbSequence->firstPage().id());
    }
}

void
MainWindow::setOptionsWidget(FilterOptionsWidget* widget, Ownership const ownership)
{
    if (isBatchProcessingInProgress())
    {
        if (ownership == TRANSFER_OWNERSHIP)
        {
            delete widget;
        }
        return;
    }

    if (m_ptrOptionsWidget != widget)
    {
        removeWidgetsFromLayout(m_pOptionsFrameLayout);
    }

    // Delete the old widget we were owning, if any.
    m_optionsWidgetCleanup.clear();

    if (ownership == TRANSFER_OWNERSHIP)
    {
        m_optionsWidgetCleanup.add(widget);
    }

    if (m_ptrOptionsWidget == widget)
    {
        return;
    }

    if (m_ptrOptionsWidget)
    {
        disconnect(
            m_ptrOptionsWidget, SIGNAL(reloadRequested()),
            this, SLOT(reloadRequested())
        );
        disconnect(
            m_ptrOptionsWidget, SIGNAL(invalidateThumbnail(PageId const&)),
            this, SLOT(invalidateThumbnail(PageId const&))
        );
        disconnect(
            m_ptrOptionsWidget, SIGNAL(invalidateThumbnail(PageInfo const&)),
            this, SLOT(invalidateThumbnail(PageInfo const&))
        );
        disconnect(
            m_ptrOptionsWidget, SIGNAL(invalidateAllThumbnails()),
            this, SLOT(invalidateAllThumbnails())
        );
        disconnect(
            m_ptrOptionsWidget, SIGNAL(goToPage(PageId const&)),
            this, SLOT(goToPage(PageId const&))
        );
    }

    m_pOptionsFrameLayout->addWidget(widget);
    installUnifiedWheelGuard(scrollArea,widget);
    m_ptrOptionsWidget = widget;

    // We use an asynchronous connection here, because the slot
    // will probably delete the options panel, which could be
    // responsible for the emission of this signal.  Qt doesn't
    // like when we delete an object while it's emitting a singal.
    connect(
        widget, SIGNAL(reloadRequested()),
        this, SLOT(reloadRequested()), Qt::QueuedConnection
    );
    connect(
        widget, SIGNAL(invalidateThumbnail(PageId const&)),
        this, SLOT(invalidateThumbnail(PageId const&))
    );
    connect(
        widget, SIGNAL(invalidateThumbnail(PageInfo const&)),
        this, SLOT(invalidateThumbnail(PageInfo const&))
    );
    connect(
        widget, SIGNAL(invalidateAllThumbnails()),
        this, SLOT(invalidateAllThumbnails())
    );
    connect(
        widget, SIGNAL(goToPage(PageId const&)),
        this, SLOT(goToPage(PageId const&))
    );
}

void
MainWindow::setImageWidget(
    QWidget* widget, Ownership const ownership,
    DebugImagesImpl* debug_images)
{
    if (isBatchProcessingInProgress() && widget != m_ptrBatchProcessingWidget.get())
    {
        while (auto item = m_liveLayout->takeAt(0)) { delete item->widget(); delete item; }
        if (ownership == TRANSFER_OWNERSHIP) {
            // Navigate the live result without editing a page under processing.
            if(auto view=qobject_cast<ImageViewBase*>(widget))view->setReadOnlyNavigation(true);
            for(auto view:widget->findChildren<ImageViewBase*>())view->setReadOnlyNavigation(true);
            m_liveLayout->addWidget(widget);
        }
        return;
    }

    removeImageWidget();

    if (ownership == TRANSFER_OWNERSHIP)
    {
        m_imageWidgetCleanup.add(widget);
    }

    if (!debug_images || debug_images->empty())
    {
        m_pImageFrameLayout->addWidget(widget);
    }
    else
    {
        m_ptrTabbedDebugImages->addTab(widget, "Main");
        QString label;
        IntrusivePtr<DebugViewFactory> factory;
        while ((factory = debug_images->retrieveNext(&label)))
        {
            QWidget* widget = new DebugImageView(factory);
            m_imageWidgetCleanup.add(widget);
            m_ptrTabbedDebugImages->addTab(widget, label);
        }
        m_pImageFrameLayout->addWidget(m_ptrTabbedDebugImages.get());
    }
}

void
MainWindow::removeImageWidget()
{
    removeWidgetsFromLayout(m_pImageFrameLayout);

    m_ptrTabbedDebugImages->clear();

    // Delete the old widget we were owning, if any.
    m_imageWidgetCleanup.clear();
}

void
MainWindow::invalidateThumbnail(PageId const& page_id)
{
    m_ptrThumbSequence->invalidateThumbnail(page_id);
}

void
MainWindow::invalidateThumbnail(PageInfo const& page_info)
{
    m_ptrThumbSequence->invalidateThumbnail(page_info);
}

void
MainWindow::invalidateAllThumbnails()
{
    m_ptrThumbSequence->invalidateAllThumbnails();
}

IntrusivePtr<AbstractCommand0<void> >
MainWindow::relinkingDialogRequester()
{
    class Requester : public AbstractCommand0<void>
    {
    public:
        Requester(MainWindow* wnd) : m_ptrWnd(wnd) {}

        virtual void operator()()
        {
            if (MainWindow* wnd = m_ptrWnd)
            {
                wnd->showRelinkingDialog();
            }
        }
    private:
        QPointer<MainWindow> m_ptrWnd;
    };

    return IntrusivePtr<AbstractCommand0<void> >(new Requester(this));
}

void
MainWindow::showRelinkingDialog()
{
    if (!isProjectLoaded())
    {
        return;
    }

    RelinkingDialog* dialog = new RelinkingDialog(m_projectFile, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);

    m_ptrPages->listRelinkablePaths(dialog->pathCollector());
    dialog->pathCollector()(RelinkablePath(m_outFileNameGen.outDir(), RelinkablePath::Dir));

    connect(dialog, &QDialog::accepted, [this, dialog]()
    {
        this->performRelinking(dialog->relinker());
    });

    dialog->show();
}

void
MainWindow::performRelinking(IntrusivePtr<AbstractRelinker> const& relinker)
{
    assert(relinker.get());

    if (!isProjectLoaded())
    {
        return;
    }

    m_ptrPages->performRelinking(*relinker);
    m_ptrStages->performRelinking(*relinker);
    m_outFileNameGen.performRelinking(*relinker);

    Utils::maybeCreateCacheDir(m_outFileNameGen.outDir());

    m_ptrThumbnailCache->setThumbDir(Utils::outputDirToThumbDir(m_outFileNameGen.outDir()));
    resetThumbSequence(currentPageOrderProvider());
    m_selectedPage.set(m_ptrThumbSequence->selectionLeader().id(), getCurrentView());

    reloadRequested();
}

void
MainWindow::goFirstPage()
{
    if (isBatchProcessingInProgress() || !isProjectLoaded())
    {
        return;
    }

    PageInfo const first_page(m_ptrThumbSequence->firstPage());
    if (!first_page.isNull())
    {
        goToPage(first_page.id());
    }
}

void
MainWindow::goLastPage()
{
    if (isBatchProcessingInProgress() || !isProjectLoaded())
    {
        return;
    }

    PageInfo const last_page(m_ptrThumbSequence->lastPage());
    if (!last_page.isNull())
    {
        goToPage(last_page.id());
    }
}

void
MainWindow::goNextPage()
{
    if (isBatchProcessingInProgress() || !isProjectLoaded())
    {
        return;
    }

    PageInfo const next_page(
        m_ptrThumbSequence->nextPage(m_ptrThumbSequence->selectionLeader().id())
    );
    if (!next_page.isNull())
    {
        goToPage(next_page.id());
    }
}

void
MainWindow::goPrevPage()
{
    if (isBatchProcessingInProgress() || !isProjectLoaded())
    {
        return;
    }

    PageInfo const prev_page(
        m_ptrThumbSequence->prevPage(m_ptrThumbSequence->selectionLeader().id())
    );
    if (!prev_page.isNull())
    {
        goToPage(prev_page.id());
    }
}

void
MainWindow::goToPage(PageId const& page_id)
{
    focusButton->setChecked(true);

    m_ptrThumbSequence->setSelection(page_id);

    // If the page was already selected, it will be reloaded.
    // That's by design.
    updateMainArea();
}

void
MainWindow::currentPageChanged(
    PageInfo const& page_info, QRectF const& thumb_rect,
    ThumbnailSequence::SelectionFlags const flags)
{
    m_selectedPage.set(page_info.id(), getCurrentView());

    if ((flags & ThumbnailSequence::SELECTED_BY_USER) || focusButton->isChecked())
    {
        if (!(flags & ThumbnailSequence::AVOID_SCROLLING_TO))
        {
            thumbView->ensureVisible(thumb_rect, 0, 0);
        }
    }

    if (flags & ThumbnailSequence::SELECTED_BY_USER)
    {
        if (isBatchProcessingInProgress())
        {
            stopBatchProcessing();
        }
        else if (!(flags & ThumbnailSequence::REDUNDANT_SELECTION))
        {
            // Start loading / processing the newly selected page.
            updateMainArea();
        }
    }
}

void
MainWindow::pageContextMenuRequested(
    PageInfo const& page_info_, QPoint const& screen_pos, bool selected)
{
    if (isBatchProcessingInProgress())
    {
        return;
    }

    // Make a copy to prevent it from being invalidated.
    PageInfo const page_info(page_info_);

    if (!selected)
    {
        goToPage(page_info.id());
    }

    QMenu menu;

    QAction* ins_before = menu.addAction(
                              QIcon(":/icons/insert-before-16.png"), tr("Insert before ...")
                          );
    QAction* ins_after = menu.addAction(
                             QIcon(":/icons/insert-after-16.png"), tr("Insert after ...")
                         );

    menu.addSeparator();

    QAction* remove = menu.addAction(
                          QIcon(":/icons/user-trash.png"), tr("Remove from project ...")
                      );

    QAction* action = menu.exec(screen_pos);
    if (action == ins_before)
    {
        showInsertFileDialog(BEFORE, page_info.imageId());
    }
    else if (action == ins_after)
    {
        showInsertFileDialog(AFTER, page_info.imageId());
    }
    else if (action == remove)
    {
        showRemovePagesDialog(m_ptrThumbSequence->selectedItems());
    }
}

void
MainWindow::pastLastPageContextMenuRequested(QPoint const& screen_pos)
{
    if (!isProjectLoaded())
    {
        return;
    }

    QMenu menu;
    menu.addAction(QIcon(":/icons/insert-here-16.png"), tr("Insert here ..."));

    if (menu.exec(screen_pos))
    {
        showInsertFileDialog(BEFORE, ImageId());
    }
}

void
MainWindow::thumbViewFocusToggled(bool const checked)
{
    QRectF const rect(m_ptrThumbSequence->selectionLeaderSceneRect());
    if (rect.isNull())
    {
        // No selected items.
        return;
    }

    if (checked)
    {
        thumbView->ensureVisible(rect, 0, 0);
    }
}

void
MainWindow::thumbViewScrolled()
{
    QRectF const rect(m_ptrThumbSequence->selectionLeaderSceneRect());
    if (rect.isNull())
    {
        // No items selected.
        return;
    }

    QRectF const viewport_rect(thumbView->viewport()->rect());
    QRectF const viewport_item_rect(
        thumbView->viewportTransform().mapRect(rect)
    );

    double const intersection_threshold = 0.5;
    if (viewport_item_rect.top() >= viewport_rect.top() &&
            viewport_item_rect.top() + viewport_item_rect.height()
            * intersection_threshold <= viewport_rect.bottom())
    {
        // Item is visible.
    }
    else if (viewport_item_rect.bottom() <= viewport_rect.bottom() &&
             viewport_item_rect.bottom() - viewport_item_rect.height()
             * intersection_threshold >= viewport_rect.top())
    {
        // Item is visible.
    }
    else
    {
        focusButton->setChecked(false);
    }
}

void
MainWindow::filterSelectionChanged(QItemSelection const& selected)
{
    if (m_ignoreSelectionChanges)
    {
        return;
    }

    if (selected.empty())
    {
        return;
    }

    m_ptrInteractiveQueue->cancelAndClear();
    if (m_ptrBatchQueue.get())
    {
        // Should not happen, but just in case.
        m_ptrBatchQueue->cancelAndClear();
    }

    bool const was_below_fix_orientation = isBelowFixOrientation(m_curFilter);
    bool const was_below_select_content = isBelowSelectContent(m_curFilter);
    m_curFilter = selected.front().top();
    bool const now_below_fix_orientation = isBelowFixOrientation(m_curFilter);
    bool const now_below_select_content = isBelowSelectContent(m_curFilter);

    m_ptrStages->filterAt(m_curFilter)->selected();

    updateSortOptions();

    // Propagate context boxes down the stage list, if necessary.
    if (!was_below_select_content && now_below_select_content)
    {
        // IMPORTANT: this needs to go before resetting thumbnails,
        // because it may affect them.
        if (m_ptrContentBoxPropagator.get())
        {
            m_ptrContentBoxPropagator->propagate(*m_ptrPages);
        } // Otherwise probably no project is loaded.
    }

    // Propagate page orientations (that might have changed) to the "Split Pages" stage.
    if (!was_below_fix_orientation && now_below_fix_orientation)
    {
        // IMPORTANT: this needs to go before resetting thumbnails,
        // because it may affect them.
        if (m_ptrPageOrientationPropagator.get())
        {
            m_ptrPageOrientationPropagator->propagate(*m_ptrPages);
        } // Otherwise probably no project is loaded.
    }

    focusButton->setChecked(true); // Should go before resetThumbSequence().
    resetThumbSequence(currentPageOrderProvider());

    updateMainArea();
}

void
MainWindow::pageOrderingChanged(int idx)
{
    if (m_ignorePageOrderingChanges)
    {
        return;
    }

    focusButton->setChecked(true); // Keep the current page in view.

    m_ptrStages->filterAt(m_curFilter)->selectPageOrder(idx);

    m_ptrThumbSequence->reset(
        m_ptrPages->toPageSequence(getCurrentView()),
        ThumbnailSequence::KEEP_SELECTION,
        currentPageOrderProvider()
    );
}

void
MainWindow::reloadRequested()
{
    // Start loading / processing the current page.
    updateMainArea();
}

void
MainWindow::startBatchProcessing()
{
    if (isBatchProcessingInProgress() || !isProjectLoaded())
    {
        return;
    }

    m_ptrInteractiveQueue->cancelAndClear();
    m_batchDone=0;m_batchTotal=0;m_batchPaused=false;m_batchElapsed.start();
    m_pauseButton->setText(unified_i18n::text("Duraklat"));

    auto const enqueuePage = [this](PageInfo const& page)
    {
        ++m_batchTotal;
        m_ptrBatchQueue->addProcessingTask(
            page, createCompositeTask(page, m_curFilter, /*batch=*/true, m_debug)
        );
    };

    m_ptrBatchQueue.reset(new ProcessingTaskQueue);

    // We want to start and finish on the same page.
    PageInfo startFinishPage(m_ptrThumbSequence->selectionLeader());
    m_ptrBatchQueue->selectPageWhenDone(startFinishPage);

    // Process pages starting from the current one till the end of the list.
    PageInfo page(startFinishPage);
    for (; !page.isNull(); page = m_ptrThumbSequence->nextPage(page.id()))
    {
        enqueuePage(page);
    }

    // Wrap-around and start from the beginning, unless we've only got one page in the list.
    page = m_ptrThumbSequence->firstPage();
    if (page.id() != m_ptrThumbSequence->lastPage().id())
    {
        for (; page.id() != startFinishPage.id(); page = m_ptrThumbSequence->nextPage(page.id()))
        {
            enqueuePage(page);
        }
    }

    focusButton->setChecked(true);

    removeFilterOptionsWidget();
    filterList->setBatchProcessingInProgress(true);
    filterList->setEnabled(false);

    BackgroundTaskPtr task(m_ptrBatchQueue->takeForProcessing());
    if (!task)
    {
        stopBatchProcessing();
    }
    else
    {
        do
        {
            m_ptrWorkerThreadPool->submitTask(task);
            if (!m_ptrWorkerThreadPool->hasSpareCapacity())
            {
                break;
            }
        }
        while ((task = m_ptrBatchQueue->takeForProcessing()));
    }

    page = m_ptrBatchQueue->selectedPage();
    if (!page.isNull())
    {
        m_ptrThumbSequence->setSelection(page.id());
    }

    m_liveProgress->setRange(0,m_batchTotal);m_liveProgress->setValue(0);
    m_liveLabel->setText(unified_i18n::text("%1 · 0/%2 sayfa").arg(m_ptrStages->filterAt(m_curFilter)->getName()).arg(m_batchTotal));
    // Display the batch processing screen.
    updateMainArea();
}

void
MainWindow::stopBatchProcessing(MainAreaAction main_area)
{
    if (!isBatchProcessingInProgress())
    {
        return;
    }

    PageInfo const page(m_ptrBatchQueue->selectedPage());
    if (!page.isNull())
    {
        m_ptrThumbSequence->setSelection(page.id());
    }

    // Note that it's important to call selectedPage() before cancelAndClear(),
    // as otherwise we would get the page set by selectPageWhenDone().

    m_ptrBatchQueue->cancelAndClear();
    m_ptrBatchQueue.reset();

    filterList->setBatchProcessingInProgress(false);
    filterList->setEnabled(true);

    switch (main_area)
    {
    case UPDATE_MAIN_AREA:
        updateMainArea();
        break;
    case CLEAR_MAIN_AREA:
        removeImageWidget();
        break;
    }
}

void
MainWindow::filterResult(BackgroundTaskPtr const& task, FilterResultPtr const& result)
{
    // Cancelled or not, we must mark it as finished.
    m_ptrInteractiveQueue->processingFinished(task);
    if (m_ptrBatchQueue.get())
    {
        m_ptrBatchQueue->processingFinished(task);
    }

    if (task->isCancelled())
    {
        return;
    }

    if (!isBatchProcessingInProgress())
    {
        if (!result->filter())
        {
            // Error loading file.  No special action is necessary.
        }
        else if (result->filter() != m_ptrStages->filterAt(m_curFilter))
        {
            // Error from one of the previous filters.
            int const idx = m_ptrStages->findFilter(result->filter());
            assert(idx >= 0);
            m_curFilter = idx;

            ScopedIncDec<int> selection_guard(m_ignoreSelectionChanges);
            filterList->selectRow(idx);
        }
    }

    if (isBatchProcessingInProgress() && task->type() == BackgroundTask::BATCH) {
        ++m_batchDone; m_liveProgress->setValue(m_batchDone);
        auto fileTask = dynamic_cast<LoadFileTask*>(task.get());
        QString label = fileTask ? QFileInfo(fileTask->pageId().imageId().filePath()).fileName() : QString();
        m_liveLabel->setText(unified_i18n::text("%1 · %2/%3 · %4 · %5 sn").arg(m_ptrStages->filterAt(m_curFilter)->getName()).arg(m_batchDone).arg(m_batchTotal).arg(label).arg(m_batchElapsed.elapsed()/1000));
    }
    // This needs to be done even if batch processing is taking place,
    // for instance because thumbnail invalidation is done from here.
    result->updateUI(this);
    if(m_editFillRequested&&m_curFilter==m_ptrStages->outputFilterIdx()){
        for(auto tabs:imageViewFrame->findChildren<QTabWidget*>())for(int i=0;i<tabs->count();++i)
            if(tabs->tabText(i).contains("Fill Zones")){tabs->setCurrentIndex(i);m_editFillRequested=false;}
    }

    if (isBatchProcessingInProgress())
    {
        if (m_ptrBatchQueue->allProcessed())
        {
            stopBatchProcessing();

            QApplication::alert(this); // Flash the taskbar entry.
            if (m_checkBeepWhenFinished())
            {
#if defined(Q_OS_UNIX)
                int result = std::system("beep");
#else
                QApplication::beep();
#endif
            }

            return;
        }

        if (m_batchPaused) return;
        do
        {
            BackgroundTaskPtr const task(m_ptrBatchQueue->takeForProcessing());
            if (!task)
            {
                break;
            }
            m_ptrWorkerThreadPool->submitTask(task);
        }
        while (m_ptrWorkerThreadPool->hasSpareCapacity());

        PageInfo const page(m_ptrBatchQueue->selectedPage());
        if (!page.isNull())
        {
            m_ptrThumbSequence->setSelection(page.id());
        }
    }
}

void
MainWindow::debugToggled(bool const enabled)
{
    m_debug = enabled;
}

void
MainWindow::saveProjectTriggered()
{
    if (m_projectFile.isEmpty())
    {
        saveProjectAsTriggered();
        return;
    }

    if (saveProjectWithFeedback(m_projectFile))
    {
        updateWindowTitle();
    }
}

void
MainWindow::saveProjectAsTriggered()
{
    // XXX: this function is duplicated in OutOfMemoryDialog.

    QString project_dir;
    if (!m_projectFile.isEmpty())
    {
        project_dir = QFileInfo(m_projectFile).absolutePath();
    }
    else
    {
        QSettings settings;
        project_dir = settings.value("project/lastDir").toString();
    }

    QString project_file(
        QFileDialog::getSaveFileName(
            this, QString(), project_dir,
            tr("Scan Tailor Projects")+" (*.ScanTailor)"
        )
    );
    if (project_file.isEmpty())
    {
        return;
    }

    if (!project_file.endsWith(".ScanTailor", Qt::CaseInsensitive))
    {
        project_file += ".ScanTailor";
    }

    if (saveProjectWithFeedback(project_file))
    {
        m_projectFile = project_file;
        updateWindowTitle();

        QSettings settings;
        settings.setValue(
            "project/lastDir",
            QFileInfo(m_projectFile).absolutePath()
        );

        RecentProjects rp;
        rp.read();
        rp.setMostRecent(m_projectFile);
        rp.write();
    }
}

void
MainWindow::newProject()
{
    if (!closeProjectInteractive())
    {
        return;
    }

    // It will delete itself when it's done.
    ProjectCreationContext* context = new ProjectCreationContext(this);
    connect(
        context, SIGNAL(done(ProjectCreationContext*)),
        this, SLOT(newProjectCreated(ProjectCreationContext*))
    );
}

void
MainWindow::newProjectCreated(ProjectCreationContext* context)
{
    IntrusivePtr<ProjectPages> pages(
        new ProjectPages(
            context->files(), ProjectPages::AUTO_PAGES,
            context->layoutDirection()
        )
    );
    switchToNewProject(pages, context->outDir());
}

void
MainWindow::openProject()
{
    QSettings settings;
    QString const project_dir(settings.value("project/lastDir").toString());

    QString const project_file(
        QFileDialog::getOpenFileName(
            this, tr("Open Project"), project_dir,
            tr("Scan Tailor Projects")+" / PDF (*.ScanTailor *.pdf *.PDF);;"+tr("Scan Tailor Projects")+" (*.ScanTailor);;PDF (*.pdf *.PDF)"
        )
    );
    if (project_file.isEmpty())
    {
        // Cancelled by user.
        return;
    }

    // Importing a PDF adds a book; it must not close an unsaved editor project.
    if(QFileInfo(project_file).suffix().compare("pdf",Qt::CaseInsensitive)!=0&&!closeProjectInteractive())return;
    openProject(project_file);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    const auto urls=event->mimeData()->urls();
    if(urls.size()==1&&urls.front().isLocalFile()&&QFileInfo(urls.front().toLocalFile()).suffix().compare("pdf",Qt::CaseInsensitive)==0)
        event->acceptProposedAction();
    else QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls=event->mimeData()->urls();
    if(urls.size()==1&&urls.front().isLocalFile()&&QFileInfo(urls.front().toLocalFile()).suffix().compare("pdf",Qt::CaseInsensitive)==0){
        m_unified->importPdf(urls.front().toLocalFile());event->acceptProposedAction();
    }else QMainWindow::dropEvent(event);
}

void
MainWindow::openProject(QString const& project_file)
{
    if(QFileInfo(project_file).suffix().compare("pdf",Qt::CaseInsensitive)==0){m_unified->importPdf(project_file);return;}
    QFile file(project_file);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(
            this, tr("Error"),
            tr("Unable to open the project file.")
        );
        return;
    }

    QDomDocument doc;
    if (!doc.setContent(&file))
    {
        QMessageBox::warning(
            this, tr("Error"),
            tr("The project file is broken.")
        );
        return;
    }

    file.close();

    ProjectOpeningContext* context = new ProjectOpeningContext(this, project_file, doc);
    connect(context, SIGNAL(done(ProjectOpeningContext*)), SLOT(projectOpened(ProjectOpeningContext*)));
    context->proceed();
}

void
MainWindow::projectOpened(ProjectOpeningContext* context)
{
    RecentProjects rp;
    rp.read();
    rp.setMostRecent(context->projectFile());
    rp.write();

    QSettings settings;
    settings.setValue(
        "project/lastDir",
        QFileInfo(context->projectFile()).absolutePath()
    );

    switchToNewProject(
        context->projectReader()->pages(),
        context->projectReader()->outputDirectory(),
        context->projectFile(), context->projectReader()
    );
    if(QFileInfo(m_pendingQueueEdit.value("edit_project").toString()).absoluteFilePath()==QFileInfo(m_projectFile).absoluteFilePath()){
        auto target=m_pendingQueueEdit;m_pendingQueueEdit={};selectQueueEditPage(target,target.value("stage").toInt(3));
    }
}

void
MainWindow::closeProject()
{
    closeProjectInteractive();
}

void
MainWindow::openSettingsDialog()
{
    SettingsDialog* dialog = new SettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);

    connect(dialog, &SettingsDialog::settingsUpdated, [this]
    {
        m_pAccelerationProvider->processUpdatedConfiguration();
        if (ThumbnailPixmapCache* thumb_cache = m_ptrThumbnailCache.get())
        {
            thumb_cache->setAccelOps(m_pAccelerationProvider->getOperations());
        }
    });

    connect(
        dialog,SIGNAL(stylesheetChanged(const QString)),
        this, SLOT(stylesheetChanged(const QString)));

    dialog->show();
}

void
MainWindow::showAboutDialog()
{
    Ui::AboutDialog ui;
    QDialog* dialog = new QDialog(this);
    ui.setupUi(dialog);
    ui.version->setText(unified_i18n::text(STFAMILY "\n" VERSION));

    QResource license(":/license.html");
    ui.licenseViewer->setHtml(unified_i18n::text((char const*)license.data(), license.size()));

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->show();
}

/**
 * This function is called asynchronously, always from the main thread.
 */
void
MainWindow::handleOutOfMemorySituation()
{
    deleteLater();

    m_ptrOutOfMemoryDialog->setParams(
        m_projectFile, m_ptrStages, m_ptrPages, m_selectedPage, m_outFileNameGen
    );

    closeProjectWithoutSaving();

    m_ptrOutOfMemoryDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_ptrOutOfMemoryDialog.release()->show();
}

void
MainWindow::stylesheetChanged(const QString stylesheetFilePath)
{
    QFileInfo stylesheetFileInfo(stylesheetFilePath);

    QString path = stylesheetFileInfo.absolutePath();

    QString stylesheet;

    QFile stylesheetFile(stylesheetFilePath);

    if (stylesheetFile.open(QIODevice::ReadOnly))
    {
        stylesheet = stylesheetFile.readAll();
    }

#if defined(Q_OS_MAC)
    stylesheet.replace("@path_to_pics@", QCoreApplication::applicationDirPath() + "/" + STYLESHEETS_DIR);
#else
    stylesheet.replace("@path_to_pics@", STYLESHEETS_DIR);
#endif

    if (stylesheet.isEmpty())
    {
        QApplication::setPalette(m_systemPalette);
    }
    else
    {
        QPalette palette = QPalette(m_systemPalette);
        QString stylesheetPalettePath = stylesheetFilePath + ".pal";
        QFile stylesheetPaletteFile(stylesheetPalettePath);
        QDomDocument paletteDoc;
        PaletteReader paletteReader;

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        if (paletteDoc.setContent(&stylesheetPaletteFile, false))
#else
        if (paletteDoc.setContent(&stylesheetPaletteFile, QDomDocument::ParseOption::Default))
#endif
        {
            if (paletteReader.readPalette(paletteDoc, palette))
            {
                QApplication::setPalette(palette);
            }
        }
    }

    setStyleSheet(stylesheet);
}

/**
 * Note: the removed widgets are not deleted.
 */
void
MainWindow::removeWidgetsFromLayout(QLayout* layout)
{
    QLayoutItem *child;
    while ((child = layout->takeAt(0)))
    {
        delete child;
    }
}

void
MainWindow::removeFilterOptionsWidget()
{
    removeWidgetsFromLayout(m_pOptionsFrameLayout);

    // Delete the old widget we were owning, if any.
    m_optionsWidgetCleanup.clear();

    m_ptrOptionsWidget = 0;
}

void
MainWindow::updateProjectActions()
{
    bool const loaded = isProjectLoaded();
    actionSaveProject->setEnabled(loaded);
    actionSaveProjectAs->setEnabled(loaded);
    actionRelinking->setEnabled(loaded);
}

bool
MainWindow::isBatchProcessingInProgress() const
{
    return m_ptrBatchQueue.get() != 0;
}

bool
MainWindow::isProjectLoaded() const
{
    return !m_outFileNameGen.outDir().isEmpty();
}

bool
MainWindow::isBelowSelectContent() const
{
    return isBelowSelectContent(m_curFilter);
}

bool
MainWindow::isBelowSelectContent(int const filter_idx) const
{
    return (filter_idx > m_ptrStages->selectContentFilterIdx());
}

bool
MainWindow::isBelowFixOrientation(int filter_idx) const
{
    return (filter_idx > m_ptrStages->fixOrientationFilterIdx());
}

bool
MainWindow::isOutputFilter() const
{
    return isOutputFilter(m_curFilter);
}

bool
MainWindow::isOutputFilter(int const filter_idx) const
{
    return (filter_idx == m_ptrStages->outputFilterIdx());
}

PageView
MainWindow::getCurrentView() const
{
    return m_ptrStages->filterAt(m_curFilter)->getView();
}

void
MainWindow::updateMainArea()
{
    if(m_unifiedViewStack->currentWidget()==m_stagePreviewHost)returnToProjectView();
    if (m_ptrPages->numImages() == 0)
    {
        filterList->setBatchProcessingPossible(false);
        showNewOpenProjectPanel();
    }
    else if (isBatchProcessingInProgress())
    {
        filterList->setBatchProcessingPossible(false);
        setImageWidget(m_ptrBatchProcessingWidget.get(), KEEP_OWNERSHIP);
    }
    else
    {
        PageInfo const page(m_ptrThumbSequence->selectionLeader());
        if (page.isNull())
        {
            filterList->setBatchProcessingPossible(false);
            removeImageWidget();
            removeFilterOptionsWidget();
        }
        else
        {
            // Note that loadPageInteractive may reset it to false.
            filterList->setBatchProcessingPossible(true);
            loadPageInteractive(page);
        }
    }
}

bool
MainWindow::checkReadyForOutput(PageId const* ignore) const
{
    return m_ptrStages->pageLayoutFilter()->checkReadyForOutput(
               *m_ptrPages, ignore
           );
}

void
MainWindow::loadPageInteractive(PageInfo const& page)
{
    assert(!isBatchProcessingInProgress());
    if(!m_editBinding.isEmpty()){
        auto sequence=m_ptrPages->toPageSequence(PAGE_VIEW);int number=0;
        for(size_t i=0;i<sequence.numPages();++i)if(sequence.pageAt(i).id()==page.id()){number=int(i)+1;break;}
        m_editBinding.insert("page",number);
        m_editLabel->setText(m_editBinding.value("title").toString()+unified_i18n::text(" · sayfa %1 · ").arg(number)+QFileInfo(page.id().imageId().filePath()).fileName()+" · "+page.id().subPageAsString());
    }

    m_ptrInteractiveQueue->cancelAndClear();

    if (isOutputFilter() && !checkReadyForOutput(&page.id()))
    {
        filterList->setBatchProcessingPossible(false);

        QString const err_text(
            tr("Output is not yet possible, as the final size"
               " of pages is not yet known.\nTo determine it,"
               " run batch processing at \"Select Content\" or"
               " \"Margins\".")
        );

        removeFilterOptionsWidget();
        setImageWidget(new ErrorWidget(err_text), TRANSFER_OWNERSHIP);
        return;
    }

    if (!isBatchProcessingInProgress())
    {
        if (m_pImageFrameLayout->indexOf(m_ptrProcessingIndicationWidget.get()) != -1)
        {
            m_ptrProcessingIndicationWidget->processingRestartedEffect();
        }
        setImageWidget(m_ptrProcessingIndicationWidget.get(), KEEP_OWNERSHIP);
        m_ptrStages->filterAt(m_curFilter)->preUpdateUI(this, page.id());
    }

    assert(m_ptrThumbnailCache.get());

    m_ptrInteractiveQueue->cancelAndClear();
    m_ptrInteractiveQueue->addProcessingTask(
        page, createCompositeTask(page, m_curFilter, /*batch=*/false, m_debug)
    );
    m_ptrWorkerThreadPool->submitTask(m_ptrInteractiveQueue->takeForProcessing());
}

void
MainWindow::updateWindowTitle()
{
    QString project_name;
    if (m_projectFile.isEmpty())
    {
        project_name = tr("Unnamed");
    }
    else
    {
        project_name = QFileInfo(m_projectFile).baseName();
    }
    QString const version(unified_i18n::text(VERSION));
    setWindowTitle(tr("%2 - scantailor-ocr-unified 0.5.0 · Experimental %3 [%1bit]").arg(sizeof(void*)*8).arg(project_name, version));
    if(m_unified)m_unified->setEditorContext(isProjectLoaded()?m_editBinding.value("title").toString(project_name):QString(),m_projectFile);
}

/**
 * \brief Closes the currently project, prompting to save it if necessary.
 *
 * \return true if the project was closed, false if the user cancelled the process.
 */
bool
MainWindow::closeProjectInteractive()
{
    if (!isProjectLoaded())
    {
        return true;
    }

    if (m_projectFile.isEmpty())
    {
        switch (promptProjectSave())
        {
        case SAVE:
            saveProjectTriggered();
        // fall through
        case DONT_SAVE:
            break;
        case CANCEL:
            return false;
        }
        closeProjectWithoutSaving();
        return true;
    }

    QFileInfo const project_file(m_projectFile);
    QFileInfo const backup_file(
        project_file.absoluteDir(),
        QLatin1String("Backup.")+project_file.fileName()
    );
    QString const backup_file_path(backup_file.absoluteFilePath());

    ProjectWriter writer(m_ptrPages, m_selectedPage, m_outFileNameGen);

    if (!writer.write(backup_file_path, m_ptrStages->filters()))
    {
        // Backup file could not be written???
        QFile::remove(backup_file_path);
        switch (promptProjectSave())
        {
        case SAVE:
            saveProjectTriggered();
        // fall through
        case DONT_SAVE:
            break;
        case CANCEL:
            return false;
        }
        closeProjectWithoutSaving();
        return true;
    }

    if (compareFiles(m_projectFile, backup_file_path))
    {
        // The project hasn't really changed.
        QFile::remove(backup_file_path);
        closeProjectWithoutSaving();
        return true;
    }

    switch (promptProjectSave())
    {
    case SAVE:
        if (!Utils::overwritingRename(
                    backup_file_path, m_projectFile))
        {
            QMessageBox::warning(
                this, tr("Error"),
                tr("Error saving the project file!")
            );
            return false;
        }
    // fall through
    case DONT_SAVE:
        QFile::remove(backup_file_path);
        break;
    case CANCEL:
        return false;
    }

    closeProjectWithoutSaving();
    return true;
}

void
MainWindow::closeProjectWithoutSaving()
{
    IntrusivePtr<ProjectPages> pages(new ProjectPages());
    switchToNewProject(pages, QString());
}

bool
MainWindow::saveProjectWithFeedback(QString const& project_file)
{
    ProjectWriter writer(m_ptrPages, m_selectedPage, m_outFileNameGen);

    if (!writer.write(project_file, m_ptrStages->filters()))
    {
        QMessageBox::warning(
            this, tr("Error"),
            tr("Error saving the project file!")
        );
        return false;
    }

    if(!m_editBinding.isEmpty()){
        QSaveFile binding(project_file+".unified-edit.json");
        if(!binding.open(QIODevice::WriteOnly)||binding.write(QJsonDocument(m_editBinding).toJson())<0||!binding.commit())return false;
    }
    return true;
}

/**
 * Note: showInsertFileDialog(BEFORE, ImageId()) is legal and means inserting at the end.
 */
void
MainWindow::showInsertFileDialog(BeforeOrAfter before_or_after, ImageId const& existing)
{
    if (isBatchProcessingInProgress() || !isProjectLoaded())
    {
        return;
    }

    // We need to filter out files already in project.
    class ProxyModel : public QSortFilterProxyModel
    {
    public:
        ProxyModel(ProjectPages const& pages)
        {
            setDynamicSortFilter(true);

            PageSequence const sequence(pages.toPageSequence(IMAGE_VIEW));
            unsigned const count = sequence.numPages();
            for (unsigned i = 0; i < count; ++i)
            {
                PageInfo const& page = sequence.pageAt(i);
                m_inProjectFiles.push_back(QFileInfo(page.imageId().filePath()));
            }
        }
    protected:
        virtual bool filterAcceptsRow(int source_row, QModelIndex const& source_parent) const
        {
            QModelIndex const idx(source_parent.sibling(source_row, 0));
            QVariant const data(idx.data(QFileSystemModel::FilePathRole));
            if (data.isNull())
            {
                return true;
            }
            return !m_inProjectFiles.contains(QFileInfo(data.toString()));
        }

        virtual bool lessThan(QModelIndex const& left, QModelIndex const& right) const
        {
            return left.row() < right.row();
        }
    private:
        QFileInfoList m_inProjectFiles;
    };

    std::unique_ptr<QFileDialog> dialog(
        new QFileDialog(
            this, tr("Files to insert"),
            QFileInfo(existing.filePath()).absolutePath()
        )
    );
    dialog->setFileMode(QFileDialog::ExistingFiles);
    dialog->setProxyModel(new ProxyModel(*m_ptrPages));
    dialog->setNameFilter(tr("Images not in project (%1)").arg("*.png *.tiff *.tif *.jpeg *.jpg"));
    // XXX: Adding individual pages from a multi-page TIFF where
    // some of the pages are already in project is not supported right now.

    if (dialog->exec() != QDialog::Accepted)
    {
        return;
    }

    QStringList files(dialog->selectedFiles());
    if (files.empty())
    {
        return;
    }

    // The order of items returned by QFileDialog is platform-dependent,
    // so we enforce our own ordering.
    std::sort(files.begin(), files.end(), SmartFilenameOrdering());

    // I suspect on some platforms it may be possible to select the same file twice,
    // so to be safe, remove duplicates.
    files.erase(std::unique(files.begin(), files.end()), files.end());

    std::vector<ImageFileInfo> new_files;
    std::vector<QString> loaded_files;
    std::vector<QString> failed_files; // Those we failed to read metadata from.

    // dialog->selectedFiles() returns file list in reverse order.
    for (int i = files.size() - 1; i >= 0; --i)
    {
        QFileInfo const file_info(files[i]);
        ImageFileInfo image_file_info(file_info, std::vector<ImageMetadata>());

        ImageMetadataLoader::Status const status = ImageMetadataLoader::load(
                    files.at(i), [&](ImageMetadata const& metadata)
        {
            image_file_info.imageInfo().push_back(metadata);
        }
                );

        if (status == ImageMetadataLoader::LOADED)
        {
            new_files.push_back(image_file_info);
            loaded_files.push_back(file_info.absoluteFilePath());
        }
        else
        {
            failed_files.push_back(file_info.absoluteFilePath());
        }
    }

    if (!failed_files.empty())
    {
        std::unique_ptr<LoadFilesStatusDialog> err_dialog(new LoadFilesStatusDialog(this));
        err_dialog->setLoadedFiles(loaded_files);
        err_dialog->setFailedFiles(failed_files);
        err_dialog->setOkButtonName(QString(" %1 ").arg(tr("Skip failed files")));
        if (err_dialog->exec() != QDialog::Accepted || loaded_files.empty())
        {
            return;
        }
    }

    // Actually insert the new pages.
    BOOST_FOREACH(ImageFileInfo const& file, new_files)
    {
        int image_num = -1; // Zero-based image number in a multi-page TIFF.

        BOOST_FOREACH(ImageMetadata const& metadata, file.imageInfo())
        {
            ++image_num;

            int const num_sub_pages = ProjectPages::adviseNumberOfLogicalPages(metadata.size());
            ImageInfo const image_info(
                ImageId(file.fileInfo(), image_num), metadata, num_sub_pages, false, false
            );
            insertImage(image_info, before_or_after, existing);
        }
    }
}

void
MainWindow::showRemovePagesDialog(std::set<PageId> const& pages)
{
    std::unique_ptr<QDialog> dialog(new QDialog(this));
    Ui::RemovePagesDialog ui;
    ui.setupUi(dialog.get());
    ui.icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxQuestion).pixmap(48, 48));

    ui.text->setText(ui.text->text().arg(pages.size()));

    QPushButton* remove_btn = ui.buttonBox->button(QDialogButtonBox::Ok);
    remove_btn->setText(tr("Remove"));

    dialog->setWindowModality(Qt::WindowModal);
    if (dialog->exec() == QDialog::Accepted)
    {
        removeFromProject(pages);
        eraseOutputFiles(pages);
    }
}

/**
 * Note: insertImage(..., BEFORE, ImageId()) is legal and means inserting at the end.
 */
void
MainWindow::insertImage(ImageInfo const& new_image,
                        BeforeOrAfter before_or_after, ImageId existing)
{
    std::vector<PageInfo> pages(
        m_ptrPages->insertImage(
            new_image, before_or_after, existing, getCurrentView()
        )
    );

    if (before_or_after == BEFORE)
    {
        // The second one will be inserted first, then the first
        // one will be inserted BEFORE the second one.
        std::reverse(pages.begin(), pages.end());
    }

    BOOST_FOREACH(PageInfo const& page_info, pages)
    {
        m_outFileNameGen.disambiguator()->registerFile(page_info.imageId().filePath());
        m_ptrThumbSequence->insert(page_info, before_or_after, existing);
        existing = page_info.imageId();
    }
}

void
MainWindow::removeFromProject(std::set<PageId> const& pages)
{
    m_ptrInteractiveQueue->cancelAndRemove(pages);
    if (m_ptrBatchQueue.get())
    {
        m_ptrBatchQueue->cancelAndRemove(pages);
    }

    m_ptrPages->removePages(pages);
    m_ptrThumbSequence->removePages(pages);

    if (m_ptrThumbSequence->selectionLeader().isNull())
    {
        m_ptrThumbSequence->setSelection(m_ptrThumbSequence->firstPage().id());
    }

    updateMainArea();
}

void
MainWindow::eraseOutputFiles(std::set<PageId> const& pages)
{
    std::vector<PageId::SubPage> erase_variations;
    erase_variations.reserve(3);

    BOOST_FOREACH(PageId const& page_id, pages)
    {
        erase_variations.clear();
        switch (page_id.subPage())
        {
        case PageId::SINGLE_PAGE:
            erase_variations.push_back(PageId::SINGLE_PAGE);
            erase_variations.push_back(PageId::LEFT_PAGE);
            erase_variations.push_back(PageId::RIGHT_PAGE);
            break;
        case PageId::LEFT_PAGE:
            erase_variations.push_back(PageId::SINGLE_PAGE);
            erase_variations.push_back(PageId::LEFT_PAGE);
            break;
        case PageId::RIGHT_PAGE:
            erase_variations.push_back(PageId::SINGLE_PAGE);
            erase_variations.push_back(PageId::RIGHT_PAGE);
            break;
        }

        BOOST_FOREACH(PageId::SubPage subpage, erase_variations)
        {
            QFile::remove(m_outFileNameGen.filePathFor(PageId(page_id.imageId(), subpage)));
        }
    }
}

BackgroundTaskPtr
MainWindow::createCompositeTask(
    PageInfo const& page, int const last_filter_idx, bool const batch, bool debug)
{
    IntrusivePtr<fix_orientation::Task> fix_orientation_task;
    IntrusivePtr<page_split::Task> page_split_task;
    IntrusivePtr<deskew::Task> deskew_task;
    IntrusivePtr<select_content::Task> select_content_task;
    IntrusivePtr<page_layout::Task> page_layout_task;
    IntrusivePtr<output::Task> output_task;

    if (batch)
    {
        debug = false;
    }

    if (last_filter_idx >= m_ptrStages->outputFilterIdx())
    {
        output_task = m_ptrStages->outputFilter()->createTask(
                          page.id(), m_ptrThumbnailCache, m_outFileNameGen, batch, debug
                      );
        debug = false;
    }
    if (last_filter_idx >= m_ptrStages->pageLayoutFilterIdx())
    {
        page_layout_task = m_ptrStages->pageLayoutFilter()->createTask(
                               page.id(), output_task, batch, debug
                           );
        debug = false;
    }
    if (last_filter_idx >= m_ptrStages->selectContentFilterIdx())
    {
        select_content_task = m_ptrStages->selectContentFilter()->createTask(
                                  page.id(), page_layout_task, batch, debug
                              );
        debug = false;
    }
    if (last_filter_idx >= m_ptrStages->deskewFilterIdx())
    {
        deskew_task = m_ptrStages->deskewFilter()->createTask(
                          page.id(), select_content_task, batch, debug
                      );
        debug = false;
    }
    if (last_filter_idx >= m_ptrStages->pageSplitFilterIdx())
    {
        page_split_task = m_ptrStages->pageSplitFilter()->createTask(
                              page, deskew_task, batch, debug
                          );
        debug = false;
    }
    if (last_filter_idx >= m_ptrStages->fixOrientationFilterIdx())
    {
        fix_orientation_task = m_ptrStages->fixOrientationFilter()->createTask(
                                   page.id(), page_split_task, batch
                               );
        debug = false;
    }
    assert(fix_orientation_task);

    return BackgroundTaskPtr(
               new LoadFileTask(
                   batch ? BackgroundTask::BATCH : BackgroundTask::INTERACTIVE,
                   page, m_pAccelerationProvider->getOperations(),
                   m_ptrThumbnailCache, m_ptrPages, fix_orientation_task
               )
           );
}

IntrusivePtr<CompositeCacheDrivenTask>
MainWindow::createCompositeCacheDrivenTask(int const last_filter_idx)
{
    IntrusivePtr<fix_orientation::CacheDrivenTask> fix_orientation_task;
    IntrusivePtr<page_split::CacheDrivenTask> page_split_task;
    IntrusivePtr<deskew::CacheDrivenTask> deskew_task;
    IntrusivePtr<select_content::CacheDrivenTask> select_content_task;
    IntrusivePtr<page_layout::CacheDrivenTask> page_layout_task;
    IntrusivePtr<output::CacheDrivenTask> output_task;

    if (last_filter_idx >= m_ptrStages->outputFilterIdx())
    {
        output_task = m_ptrStages->outputFilter()
                      ->createCacheDrivenTask(m_outFileNameGen);
    }
    if (last_filter_idx >= m_ptrStages->pageLayoutFilterIdx())
    {
        page_layout_task = m_ptrStages->pageLayoutFilter()
                           ->createCacheDrivenTask(output_task);
    }
    if (last_filter_idx >= m_ptrStages->selectContentFilterIdx())
    {
        select_content_task = m_ptrStages->selectContentFilter()
                              ->createCacheDrivenTask(page_layout_task);
    }
    if (last_filter_idx >= m_ptrStages->deskewFilterIdx())
    {
        deskew_task = m_ptrStages->deskewFilter()
                      ->createCacheDrivenTask(select_content_task);
    }
    if (last_filter_idx >= m_ptrStages->pageSplitFilterIdx())
    {
        page_split_task = m_ptrStages->pageSplitFilter()
                          ->createCacheDrivenTask(deskew_task);
    }
    if (last_filter_idx >= m_ptrStages->fixOrientationFilterIdx())
    {
        fix_orientation_task = m_ptrStages->fixOrientationFilter()
                               ->createCacheDrivenTask(page_split_task);
    }

    assert(fix_orientation_task);

    return fix_orientation_task;
}

void
MainWindow::updateDisambiguationRecords(PageSequence const& pages)
{
    int const count = pages.numPages();
    for (int i = 0; i < count; ++i)
    {
        m_outFileNameGen.disambiguator()->registerFile(pages.pageAt(i).imageId().filePath());
    }
}

PageSelectionAccessor
MainWindow::newPageSelectionAccessor()
{
    IntrusivePtr<PageSelectionProvider const> provider(new PageSelectionProviderImpl(this));
    return PageSelectionAccessor(provider);
}

void MainWindow::toggleUnifiedPause() {
    if (!isBatchProcessingInProgress()) return;
    m_batchPaused=!m_batchPaused;
    m_pauseButton->setText(unified_i18n::text(m_batchPaused?"Devam et":"Duraklat"));
    if (m_batchPaused) m_liveLabel->setText(unified_i18n::text("Duraklatılıyor: çalışan sayfalar tamamlanınca bekleyecek."));
    else dispatchUnifiedBatch();
}
void MainWindow::dispatchUnifiedBatch() {
    if (!isBatchProcessingInProgress() || m_batchPaused) return;
    while (m_ptrWorkerThreadPool->hasSpareCapacity()) {
        auto task=m_ptrBatchQueue->takeForProcessing();if(!task)break;
        m_ptrWorkerThreadPool->submitTask(task);
    }
}
void MainWindow::setUnifiedPdfAppearance(QString const& appearance) {
    m_unified->setAppearance(appearance);
    QMainWindow::statusBar()->showMessage(unified_i18n::text("Sonraki PDF işlemlerinin görünümü güncellendi. Output → PDF görünümü alanı ve alt panel aynı ayarı kullanır; çalışan iş değişmez."),7000);
}
void MainWindow::returnToProjectView() {
    if(m_geometryReviewBar)m_geometryReviewBar->setPreview(false);
    if(m_contentReviewBar)m_contentReviewBar->setPreview(false);
    m_unifiedViewStack->setCurrentWidget(m_editorHost);
    m_queueThumbStack->setCurrentIndex(0);
    if(m_unified)m_unified->clearPreviewSelection();
    dockWidgetFilters->widget()->setEnabled(true);
    dockWidgetThumbnails->widget()->setEnabled(true);
    dockWidgetFilters->setWindowTitle(tr("Stages"));
    dockWidgetThumbnails->setWindowTitle(tr("Thumbnails"));
}
void MainWindow::captureUnifiedProject() {
    if (!isProjectLoaded() || isBatchProcessingInProgress()) return;
    // Snapshot never overwrites the user's .ScanTailor file.
    QString snapshot=m_unified->snapshotPath();
    if (!saveProjectWithFeedback(snapshot)) return;
    PageSequence sequence(m_ptrPages->toPageSequence(PAGE_VIEW));
    QJsonArray outputs;
    for (size_t i=0;i<sequence.numPages();++i) outputs.append(m_outFileNameGen.filePathFor(sequence.pageAt(i).id()));
    QString title=m_editBinding.value("title").toString(m_projectFile.isEmpty()?QFileInfo(m_outFileNameGen.outDir()).dir().dirName():QFileInfo(m_projectFile).baseName());
    m_unified->addProject(title,snapshot,outputs);
    m_unifiedDock->show();m_unifiedDock->raise();
}

void MainWindow::openQueuePage(QJsonObject const& target,int stage){
    if(isBatchProcessingInProgress())return;
    if(!m_editBinding.isEmpty()&&m_editBinding.value("job_id")==target.value("job_id")){
        returnToProjectView();selectQueueEditPage(target,stage);return;
    }
    if(!closeProjectInteractive())return;
    try{
        QString project=m_unified->editCopy(target);
        m_pendingQueueEdit=target;m_pendingQueueEdit.insert("edit_project",project);m_pendingQueueEdit.insert("stage",stage);
        openProject(project);
    }catch(std::exception const& e){QMessageBox::warning(this,unified_i18n::text("Düzenleme"),unified_i18n::text(e.what()));}
}
void MainWindow::selectQueueEditPage(QJsonObject const& target,int stage){
    auto pages=m_ptrPages->toPageSequence(PAGE_VIEW);PageId selected;
    for(size_t i=0;i<pages.numPages();++i){auto id=pages.pageAt(i).id();
        if(QFileInfo(id.imageId().filePath()).absoluteFilePath()==QFileInfo(target.value("source").toString()).absoluteFilePath()&&id.imageId().page()==target.value("image_page").toInt()&&id.subPageAsString()==target.value("subpage").toString()){selected=id;break;}
    }
    if(selected.isNull()){
        QMainWindow::statusBar()->showMessage(unified_i18n::text("Sayfa bölme değişmiş. Kaynak sayfayı sağdaki küçük resimlerden seçin."),9000);return;
    }
    returnToProjectView();m_editFillRequested=stage==6;
    filterList->selectRow(qBound(0,stage==6?5:stage,5));goToPage(selected);
    dockWidgetFilters->show();dockWidgetThumbnails->show();
    m_editLabel->setText(target.value("title").toString()+unified_i18n::text(" · sayfa %1 · ").arg(target.value("page").toInt())+QFileInfo(selected.imageId().filePath()).fileName()+" · "+selected.subPageAsString());
}
void MainWindow::reviewBookContent(){
    if(m_ptrPages->toPageSequence(PAGE_VIEW).numPages()==0||isBatchProcessingInProgress())return;
    if(auto existing=findChild<QDialog*>("unifiedBookReview")){existing->show();existing->raise();return;}
    auto stages=m_ptrStages;
    auto samples=unified_content::collect(*m_ptrPages,*stages);
    auto dialog=new UnifiedContentReviewDialog(std::move(samples),int(m_ptrPages->toPageSequence(PAGE_VIEW).numPages()),this);
    dialog->navigate=[this,stages](PageId const& page){if(m_ptrStages!=stages)return;filterList->selectRow(3);goToPage(page);};
    dialog->apply=[this,stages](unified_content::Proposal const& p,unified_content::Sample const& s){
        if(m_ptrStages!=stages||!unified_content::apply(p,s,*stages))return false;
        invalidateThumbnail(s.page.id());if(m_ptrThumbSequence->selectionLeader().id()==s.page.id())reloadRequested();return true;
    };
    dialog->undo=[this,stages](unified_content::Sample const& s,QRectF const& applied){
        if(m_ptrStages!=stages)return false;auto settings=stages->selectContentFilter()->getSettings();auto now=settings->getPageParams(s.page.id());
        if(!now||!now->dependencies().matches(s.params.dependencies()))return false;
        QRectF r=now->contentBox().toTransformedRect(*s.transform);
        if(std::abs(r.x()-applied.x())+std::abs(r.y()-applied.y())+std::abs(r.width()-applied.width())+std::abs(r.height()-applied.height())>.05)return false;
        settings->setPageParams(s.page.id(),s.params);stages->pageLayoutFilter()->setContentBox(s.page.id(),s.rect);invalidateThumbnail(s.page.id());if(m_ptrThumbSequence->selectionLeader().id()==s.page.id())reloadRequested();return true;
    };
    dialog->show();
}
void MainWindow::publishQueueEdit(){
    if(m_editBinding.isEmpty()||!isProjectLoaded()||isBatchProcessingInProgress())return;
    if(!saveProjectWithFeedback(m_projectFile))return;
    QString snapshot=m_unified->revisionSnapshotPath();
    if(!saveProjectWithFeedback(snapshot))return;
    if(m_unified->submitRevision(m_editBinding,snapshot))QMainWindow::statusBar()->showMessage(unified_i18n::text("Düzeltme kaydedildi. Yeni PDF sıraya alındı; önceki PDF saklanıyor."),12000);
}
void MainWindow::reviewAiContent(){
    ensureAiContentReview(true);
}
UnifiedAiReviewDialog* MainWindow::ensureAiContentReview(bool show){
    if(!isProjectLoaded()||isBatchProcessingInProgress())return nullptr;
    if(auto existing=findChild<QDialog*>("unifiedAiReview")){if(show){existing->show();existing->raise();}return static_cast<UnifiedAiReviewDialog*>(existing);}
    auto stages=m_ptrStages;auto samples=unified_content::collect(*m_ptrPages,*stages);
    if(samples.empty()){QMainWindow::statusBar()->showMessage(unified_i18n::text("Önce Geometric Distortions ve Select Content aşamalarını işle."),12000);return nullptr;}
    QString root=qEnvironmentVariable("STOU_ROOT");if(root.isEmpty()){QDir d(QCoreApplication::applicationDirPath());for(int i=0;i<5;++i){if(d.exists("local-tools.json")){root=d.path();break;}d.cdUp();}}
    QString identity=m_editBinding.value("job_id").toString(m_projectFile);
    auto dialog=new UnifiedAiReviewDialog(std::move(samples),m_ptrThumbSequence->selectionLeader().id(),root,int(m_ptrPages->toPageSequence(PAGE_VIEW).numPages()),this,identity);
    dialog->setWindowTitle(QFileInfo(m_projectFile).completeBaseName()+unified_i18n::text(" · Yapay zekâ ile içerik ve sayfa düzeni · 0.5.0"));
    dialog->navigate=[this,stages](PageId const& page){if(m_ptrStages==stages){returnToProjectView();filterList->selectRow(3);goToPage(page);}};
    dialog->refresh=[this,stages]{return m_ptrStages==stages?unified_content::collect(*m_ptrPages,*stages):std::vector<unified_content::Sample>{};};
    dialog->matches=[this,stages](auto const& sample,QRectF const* applied){return m_ptrStages==stages&&unified_content::matchesCurrent(sample,*stages,applied);};
    dialog->apply=[this,stages](unified_content::Proposal const& p,unified_content::Sample const& s,bool manual){if(m_ptrStages!=stages||!unified_content::applyAi(p,s,*stages,manual))return false;invalidateThumbnail(s.page.id());if(m_ptrThumbSequence->selectionLeader().id()==s.page.id())reloadRequested();return true;};
    dialog->undo=[this,stages](unified_content::Sample const& s,QRectF const& r){if(m_ptrStages!=stages||!unified_content::undoAi(s,r,*stages))return false;invalidateThumbnail(s.page.id());if(m_ptrThumbSequence->selectionLeader().id()==s.page.id())reloadRequested();return true;};
    if(show)dialog->show();return dialog;
}
void MainWindow::reviewGeometry(){
    ensureGeometryReview(true);
}
UnifiedGeometryReviewDialog* MainWindow::ensureGeometryReview(bool show){
    if(!isProjectLoaded()||isBatchProcessingInProgress())return nullptr;
    if(auto existing=findChild<QDialog*>("unifiedGeometryReview")){if(show){existing->show();existing->raise();}return static_cast<UnifiedGeometryReviewDialog*>(existing);}
    auto stages=m_ptrStages;auto samples=unified_geometry::collect(*m_ptrPages,*stages);
    if(samples.empty()){QMainWindow::statusBar()->showMessage(unified_i18n::text("Önce sayfa yönü, bölme ve geometri aşamalarını işle."),12000);return nullptr;}
    QString root=qEnvironmentVariable("STOU_ROOT");if(root.isEmpty()){QDir d(QCoreApplication::applicationDirPath());for(int i=0;i<5;++i){if(d.exists("local-tools.json")){root=d.path();break;}d.cdUp();}}
    auto dialog=new UnifiedGeometryReviewDialog(std::move(samples),m_ptrThumbSequence->selectionLeader().id(),root,this,m_editBinding.value("job_id").toString(m_projectFile));
    dialog->refresh=[this,stages]{return m_ptrStages==stages?unified_geometry::collect(*m_ptrPages,*stages):std::vector<unified_geometry::Sample>{};};
    dialog->matches=[this,stages](auto const& sample,unified_geometry::Candidate const* applied){return m_ptrStages==stages&&unified_geometry::matchesCurrent(sample,*stages,applied);};
    dialog->navigate=[this,stages](PageId const& page){if(m_ptrStages==stages){returnToProjectView();filterList->selectRow(2);goToPage(page);}};
    dialog->apply=[this,stages](auto const& s,auto const& c,bool manual){if(m_ptrStages!=stages||!unified_geometry::apply(s,c,*stages,manual))return false;invalidateThumbnail(s.page.id());if(m_ptrThumbSequence->selectionLeader().id()==s.page.id())reloadRequested();return true;};
    dialog->undo=[this,stages](auto const& s,auto const& c){if(m_ptrStages!=stages||!unified_geometry::undo(s,c,*stages))return false;invalidateThumbnail(s.page.id());if(m_ptrThumbSequence->selectionLeader().id()==s.page.id())reloadRequested();return true;};
    if(show)dialog->show();return dialog;
}

void MainWindow::bindStageReviews(){
    auto geometry=m_ptrStages->deskewFilter()->optionsWidget();auto content=m_ptrStages->selectContentFilter()->optionsWidget();
    if(auto old=geometry->findChild<QPushButton*>("geometryReviewButton"))old->parentWidget()->hide();
    if(auto old=content->findChild<QPushButton*>("aiContentReview"))old->parentWidget()->hide();
    m_geometryReviewBar=new UnifiedStageReviewBar(true,geometry);m_contentReviewBar=new UnifiedStageReviewBar(false,content);
    if(auto box=qobject_cast<QVBoxLayout*>(geometry->layout()))box->insertWidget(0,m_geometryReviewBar);
    if(auto box=qobject_cast<QVBoxLayout*>(content->layout()))box->insertWidget(0,m_contentReviewBar);
    m_geometryReviewBar->action=[this](auto a){stageReviewAction(true,int(a));};m_contentReviewBar->action=[this](auto a){stageReviewAction(false,int(a));};
    auto group=m_ptrStages->outputFilter()->optionsWidget()->findChild<QGroupBox*>("unifiedOutputTools");
    if(group){auto settings=new QPushButton(unified_i18n::text("PDF / OCR ayarları"));settings->setObjectName("stageOcrSettings");group->layout()->addWidget(settings);connect(settings,&QPushButton::clicked,[this]{m_unified->showToolsTab(1);m_unifiedDock->show();m_unifiedDock->raise();});
        auto book=new QPushButton(unified_i18n::text("Kitabı kuyruğa ekle"));book->setObjectName("stageQueueBook");book->setToolTip(unified_i18n::text("Açık projeyi mevcut ayarlarıyla kuyruğa ekler. Başlatmak için alttaki PDF oluştur düğmesini kullan."));group->layout()->addWidget(book);connect(book,&QPushButton::clicked,[this]{m_unified->showToolsTab(0);m_unifiedDock->show();m_unifiedDock->raise();if(m_unified->busy())QMainWindow::statusBar()->showMessage(unified_i18n::text("Kuyruk çalışıyor. Bu kitapta yaptığın düzeltmeleri Kaydet ve PDF’yi güncelle ile gönder."),14000);else captureUnifiedProject();});}
    if(group)if(auto button=group->findChild<QPushButton*>("stageFinishPreview"))connect(button,&QPushButton::clicked,this,&MainWindow::previewOutputFinish);
}
void MainWindow::previewOutputFinish(){
    if(!isProjectLoaded()||isBatchProcessingInProgress()||m_curFilter!=5)return;
    auto page=m_ptrThumbSequence->selectionLeader().id();if(page.isNull())return;
    QString source=m_outFileNameGen.filePathFor(page),gray=source+".ocr.tif";
    if(!QFileInfo::exists(gray)||!QFileInfo::exists(source)){QMainWindow::statusBar()->showMessage(unified_i18n::text("Önce bu sayfanın Output işleminin bitmesini bekle."),10000);return;}
    if(auto running=findChild<QProcess*>("stageFinishProcess")){if(running->state()!=QProcess::NotRunning){QMainWindow::statusBar()->showMessage(unified_i18n::text("Önizleme hazırlanıyor…"),5000);return;}running->deleteLater();}
    QString root=qEnvironmentVariable("STOU_ROOT");if(root.isEmpty()){QDir d(QCoreApplication::applicationDirPath());for(int i=0;i<5;++i){if(d.exists("local-tools.json")){root=d.path();break;}d.cdUp();}}
    QString folder=unified_runtime::data(root)+"/resources/reviews/output-previews/"+QUuid::createUuid().toString(QUuid::WithoutBraces);QDir().mkpath(folder);
    auto options=m_unified->outputFinish();auto appearance=m_unified->appearance();auto stamp=QFileInfo(gray).lastModified();auto stages=m_ptrStages;
    QSaveFile request(folder+"/request.json");if(!request.open(QIODevice::WriteOnly))return;
    request.write(QJsonDocument(QJsonObject{{"source",source},{"gray",gray},{"output",folder+"/after.png"},{"before",folder+"/before.png"},{"options",options},{"appearance",appearance}}).toJson());if(!request.commit())return;
    auto process=new QProcess(this);process->setObjectName("stageFinishProcess");process->setWorkingDirectory(root);auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("PYTHONPATH",root+"/src");process->setProcessEnvironment(env);
    QMainWindow::statusBar()->showMessage(unified_i18n::text("Son görüntü önizlemesi hazırlanıyor; önceki aşamalar korunuyor…"));
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[=](int code,QProcess::ExitStatus){
        auto log=unified_i18n::text(process->readAllStandardOutput()),error=unified_i18n::text(process->readAllStandardError());process->deleteLater();
        if(code){QMainWindow::statusBar()->showMessage(unified_i18n::text("Önizleme tamamlanamadı: ")+error.right(700),18000);return;}
        if(m_ptrStages!=stages||m_curFilter!=5||m_ptrThumbSequence->selectionLeader().id()!=page||m_unified->outputFinish()!=options||m_unified->appearance()!=appearance||QFileInfo(gray).lastModified()!=stamp){QMainWindow::statusBar()->showMessage(unified_i18n::text("Sayfa veya ayarlar değişti. Güncel sayfada yeniden önizle."),10000);return;}
        delete m_stagePreviewImage;auto split=new QSplitter;m_stagePreviewImage=split;
        m_stagePreviewNote->setText(unified_i18n::text("SON GÖRÜNTÜ KARŞILAŞTIRMASI · Önceki aşamalar değişmedi. Seçenekler yeni PDF’de uygulanır. Burada sayfa yerleşimi ve OCR katmanı gösterilmez. Yakınlaştırma: tekerlek."));
        for(auto name:{"before","after"}){auto host=new QWidget;auto layout=new QVBoxLayout(host);layout->addWidget(new QLabel(unified_i18n::text(QString(name)=="before"?"Önce · mevcut Output görüntüsü":"Sonra · seçtiğin ek işlemler")));QImage image(folder+"/"+name+".png");layout->addWidget(new BasicImageView(std::make_shared<NonAcceleratedOperations>(),image),1);split->addWidget(host);}
        m_stagePreviewLayout->addWidget(split,1);m_unifiedViewStack->setCurrentWidget(m_stagePreviewHost);
        QString message=unified_i18n::text("Önce / sonra hazır. Seçenekler sonraki PDF işlerine uygulanır.");for(auto line:log.split('\n'))if(line.startsWith("STOUFINISH ")){auto report=QJsonDocument::fromJson(line.mid(11).toUtf8()).object();message+=" "+report.value("lines").toObject().value("reason").toString();}
        QMainWindow::statusBar()->showMessage(message,20000);
    });
    connect(process,&QProcess::errorOccurred,this,[=](QProcess::ProcessError e){if(e==QProcess::FailedToStart){QMainWindow::statusBar()->showMessage(process->errorString(),15000);process->deleteLater();}});
    process->start(unified_runtime::python(root),{"-u","-m","scantailor_ocr_unified.output_finish",folder+"/request.json"});
}
void MainWindow::refreshStageReviews(){
    if(!m_geometryReviewBar||!m_contentReviewBar||!isProjectLoaded())return;
    // Some original professional controls need a horizontal scrollbar. Keep
    // our plain-language Output panel inside the visible column regardless.
    if(auto group=m_ptrStages->outputFilter()->optionsWidget()->findChild<QGroupBox*>("unifiedOutputTools")){
        int width=qMax(220,scrollArea->viewport()->width()-24);
        group->setMaximumWidth(width);
        // The surrounding native options may be wider than this group. Its
        // wrapped text must use the visible width when reserving vertical space.
        group->setMinimumHeight(qMax(0,group->heightForWidth(width)));
    }
    auto page=m_ptrThumbSequence->selectionLeader().id();
    QString key=page.isNull()?QString():QFileInfo(page.imageId().filePath()).fileName()+" · "+page.subPageAsString();
    auto update=[&](auto d,UnifiedStageReviewBar* bar){
        UnifiedStageReviewState s;
        if(d)s=UnifiedStageReviewBridge::state(d,page,bar->candidateFor(key),bar->scope()=="page"?std::set<PageId>{page}:selectedPages(),bar->scope()=="book");
        else {s.page=key;s.detail=unified_i18n::text("Önce yanındaki ▶ Hazırla düğmesine bas. Öneri gelince inceleyip uygula.");}
        if(bar==m_geometryReviewBar){auto params=m_ptrStages->deskewFilter()->getSettings()->getPageParams(page);if(params){auto type=params->distortionType();s.nativeMethod=unified_i18n::text(type==deskew::DistortionType::WARP?"Curved lines · eğrilik":type==deskew::DistortionType::PERSPECTIVE?"Perspektif":type==deskew::DistortionType::ROTATION?"Döndürme / eğiklik":"Düzeltme yok");s.nativeMethod+=params->mode()==MODE_AUTO?" · Auto":" · Manual";}}
        else {auto params=m_ptrStages->selectContentFilter()->getSettings()->getPageParams(page);if(params)s.nativeMethod=params->mode()==MODE_AUTO?unified_i18n::text("Otomatik içerik seçimi · Auto"):unified_i18n::text("Kayıtlı içerik kutusu · Manual");}
        if(s.stale&&bar->previewing())returnToProjectView();
        bar->showState(s);bar->setEnabled(!isBatchProcessingInProgress());
    };
    update(static_cast<UnifiedGeometryReviewDialog*>(findChild<QDialog*>("unifiedGeometryReview")),m_geometryReviewBar);
    update(static_cast<UnifiedAiReviewDialog*>(findChild<QDialog*>("unifiedAiReview")),m_contentReviewBar);
}
void MainWindow::stageReviewAction(bool geometry,int action){
    if(!isProjectLoaded()||isBatchProcessingInProgress())return;auto bar=geometry?m_geometryReviewBar:m_contentReviewBar;if(!bar)return;
    auto page=m_ptrThumbSequence->selectionLeader().id();if(page.isNull())return;
    if(action==UnifiedStageReviewBar::Changed){refreshStageReviews();return;}
    if(action==UnifiedStageReviewBar::Preview&&!bar->previewing()){returnToProjectView();return;}
    bool all=bar->scope()=="book",single=bar->scope()=="page";auto pages=single?std::set<PageId>{page}:selectedPages();
    auto run=[&](auto dialog){if(!dialog)return;QString feedback;
        switch(action){
        case UnifiedStageReviewBar::Analyze:feedback=UnifiedStageReviewBridge::analyze(dialog,pages,all,bar->device());break;
        case UnifiedStageReviewBar::Preview:{auto im=UnifiedStageReviewBridge::preview(dialog,page,bar->candidate());if(im.isNull()){bar->setPreview(false);feedback=unified_i18n::text("Önizleme hazır değil; yeniden öneri hazırla.");break;}delete m_stagePreviewImage;m_stagePreviewImage=new BasicImageView(std::make_shared<NonAcceleratedOperations>(),im);auto reviewState=UnifiedStageReviewBridge::state(dialog,page,bar->candidate());m_stagePreviewNote->setText(unified_i18n::text(geometry?"EĞRİ KARŞILAŞTIRMASI · ":"İÇERİK KARŞILAŞTIRMASI · ")+reviewState.detail+unified_i18n::text(" Yakınlaştırma: tekerlek."));m_stagePreviewLayout->addWidget(m_stagePreviewImage,1);m_unifiedViewStack->setCurrentWidget(m_stagePreviewHost);bar->setPreview(true);break;}
        case UnifiedStageReviewBar::Apply:feedback=UnifiedStageReviewBridge::apply(dialog,pages,all,single,bar->allowManual(),bar->candidate());returnToProjectView();break;
        case UnifiedStageReviewBar::Undo:feedback=UnifiedStageReviewBridge::undo(dialog);returnToProjectView();break;
        case UnifiedStageReviewBar::Stop:UnifiedStageReviewBridge::stop(dialog);break;
        case UnifiedStageReviewBar::Advanced:dialog->show();dialog->raise();break;
        default:break;}
        if(!feedback.isEmpty())QMainWindow::statusBar()->showMessage(feedback,16000);
    };
    if(geometry)run(ensureGeometryReview(false));else run(ensureAiContentReview(false));refreshStageReviews();
}
