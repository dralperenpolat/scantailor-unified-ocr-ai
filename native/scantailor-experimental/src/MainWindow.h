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

#ifndef MAINWINDOW_H_
#define MAINWINDOW_H_

#include <QMainWindow>
#include <QProgressBar>
#include <QLabel>
#include <QElapsedTimer>
class UnifiedPanel;
class UnifiedProjectCache;
class UnifiedQueuePreview;
class UnifiedQueueThumbnails;
class UnifiedStageReviewBar;
class UnifiedAiReviewDialog;
class UnifiedGeometryReviewDialog;
class QStackedWidget;
#include <QString>
#include <QJsonObject>
#include <QPointer>
#include <QObjectCleanupHandler>
#include <QSizeF>
#include <QApplication>
#include <QLineF>
#include <QPointer>
#include <QWidget>
#include <QDialog>
#include <QCloseEvent>
#include <QStackedLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLayoutItem>
#include <QScrollBar>
#include <QPushButton>
#include <QCheckBox>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QString>
#include <QByteArray>
#include <QVariant>
#include <QModelIndex>
#include <QFileDialog>
#include <QMessageBox>
#include <QPalette>
#include <QStyle>
#include <QSettings>
#include <QDomDocument>
#include <QSortFilterProxyModel>
#include <QFileSystemModel>
#include <QFileInfo>
#include <QResource>
#include <Qt>
#include <QDebug>
#include <memory>
#include <vector>
#include <set>
#include <algorithm>
#include <vector>
#include <stddef.h>
#include <math.h>
#include <assert.h>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include "ui_MainWindow.h"
#include "FilterUiInterface.h"
#include "NonCopyable.h"
#include "AbstractCommand.h"
#include "IntrusivePtr.h"
#include "BackgroundTask.h"
#include "FilterResult.h"
#include "ThumbnailSequence.h"
#include "OutputFileNameGenerator.h"
#include "PageId.h"
#include "PageView.h"
#include "PageRange.h"
#include "SelectedPage.h"
#include "BeforeOrAfter.h"

class AbstractFilter;
class AbstractRelinker;
class DefaultAccelerationProvider;
class ThumbnailPixmapCache;
class ProjectPages;
class PageSequence;
class StageSequence;
class PageOrderProvider;
class PageSelectionAccessor;
class FilterOptionsWidget;
class ProcessingIndicationWidget;
class ImageInfo;
class PageInfo;
class QStackedLayout;
class WorkerThreadPool;
class ProjectReader;
class DebugImages;
class ContentBoxPropagator;
class PageOrientationPropagator;
class ProjectCreationContext;
class ProjectOpeningContext;
class CompositeCacheDrivenTask;
class TabbedDebugImages;
class ProcessingTaskQueue;
class OutOfMemoryDialog;
class QLineF;
class QRectF;
class QLayout;
class QProcess;

class MainWindow :
    public QMainWindow,
    private FilterUiInterface,
    private Ui::MainWindow
{
    DECLARE_NON_COPYABLE(MainWindow)
    Q_OBJECT
public:
    MainWindow();

    virtual ~MainWindow();

    PageSequence allPages() const;

    std::set<PageId> selectedPages() const;

    std::vector<PageRange> selectedRanges() const;
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    virtual void closeEvent(QCloseEvent* event);

    virtual void timerEvent(QTimerEvent* event);
public slots:
    void openProject(QString const& project_file);
    void captureUnifiedProject();
    void toggleUnifiedPause();
    void dispatchUnifiedBatch();
private:
    friend class UnifiedWindowTest;
    friend class UnifiedWorkflowTest;
    void initWorkflow();
    bool selectWorkflowRow(int row);
    void runWorkflowStep(QString const& id);
    void showWorkflowTools();
    void importWorkflowImages(QStringList files);
    void startWorkflowProcess(QString const& mode);
    void refreshWorkflowState();
    void saveWorkflowState();
    void syncWorkflowCache(bool scan=false);
    void finishWorkflowProject(bool discard);
    void cancelWorkflowProcess();
    UnifiedProjectCache* m_workflowCache=nullptr;
    QByteArray workflowFingerprint() const;
    QJsonArray workflowPages() const;
    QJsonArray m_workflowLastPages;
    std::set<PageId> m_workflowPageTargets;
    bool m_workflowPartial=false;
    QWidget *m_workflowInput=nullptr,*m_workflowTools=nullptr,*m_workflowHelp=nullptr,*m_workflowCleanup=nullptr;
    QLabel *m_workflowTitle=nullptr,*m_workflowStatus=nullptr,*m_cleanupStatus=nullptr,*m_workflowDirty=nullptr;
    QPushButton *m_workflowRun=nullptr,*m_workflowStop=nullptr;
    QComboBox* m_cleanupScope=nullptr;
    QProcess* m_workflowProcess=nullptr;
    QString m_workflowVisualPdf,m_workflowVisualHash;
    QString m_workflowStep="input",m_workflowPending,m_workflowResult,m_workflowReview,m_workflowPdf,m_workflowProjectKey;
    QJsonObject m_workflowReceipt;
    QByteArray m_workflowPdfStamp;
    QElapsedTimer m_workflowElapsed,m_aiGeometryElapsed,m_aiContentElapsed;
    bool m_aiGeometryRunning=false,m_aiContentRunning=false;
    bool m_workflowReady=false;
    QByteArray m_defaultPanelState;
    UnifiedPanel* m_unified = nullptr;
    QDockWidget* m_unifiedDock = nullptr;
    QStackedWidget* m_unifiedViewStack = nullptr;
    UnifiedQueuePreview* m_queuePreview = nullptr;
    QStackedWidget* m_queueThumbStack = nullptr;
    UnifiedQueueThumbnails* m_queueThumbnails = nullptr;
    QWidget *m_editorHost = nullptr, *m_editBar = nullptr;
    QCheckBox* m_keepEditMargins = nullptr;
    QLabel* m_editLabel = nullptr;
    QJsonObject m_editBinding, m_pendingQueueEdit;
    bool m_editFillRequested = false;
    void openQueuePage(QJsonObject const& target,int stage);
    void selectQueueEditPage(QJsonObject const& target,int stage);
    void publishQueueEdit();
    void reviewBookContent();
    void reviewAiContent();
    void reviewGeometry();
    UnifiedAiReviewDialog* ensureAiContentReview(bool show);
    UnifiedGeometryReviewDialog* ensureGeometryReview(bool show);
    void bindStageReviews();
    void stageReviewAction(bool geometry,int action);
    void refreshStageReviews();
    void previewOutputFinish();
    QPointer<UnifiedStageReviewBar> m_geometryReviewBar,m_contentReviewBar;
    QWidget* m_stagePreviewHost = nullptr;
    QVBoxLayout* m_stagePreviewLayout = nullptr;
    QWidget* m_stagePreviewImage = nullptr;
    QLabel* m_stagePreviewNote = nullptr;
    void returnToProjectView();
    QVBoxLayout* m_liveLayout = nullptr;
    QProgressBar* m_liveProgress = nullptr;
    QLabel* m_liveLabel = nullptr;
    QPushButton* m_pauseButton = nullptr;
    bool m_batchPaused = false;
    int m_batchDone = 0, m_batchTotal = 0;
    QElapsedTimer m_batchElapsed;
    enum MainAreaAction { UPDATE_MAIN_AREA, CLEAR_MAIN_AREA };
private slots:
    void goFirstPage();

    void goLastPage();

    void goNextPage();

    void goPrevPage();

    void goToPage(PageId const& page_id);

    void currentPageChanged(
        PageInfo const& page_info, QRectF const& thumb_rect,
        ThumbnailSequence::SelectionFlags flags);

    void pageContextMenuRequested(
        PageInfo const& page_info, QPoint const& screen_pos, bool selected);

    void pastLastPageContextMenuRequested(QPoint const& screen_pos);

    void thumbViewFocusToggled(bool checked);

    void thumbViewScrolled();

    void filterSelectionChanged(QItemSelection const& selected);

    void pageOrderingChanged(int idx);

    void reloadRequested();

    void startBatchProcessing();

    void stopBatchProcessing(MainAreaAction main_area = UPDATE_MAIN_AREA);

    void invalidateThumbnail(PageId const& page_id);

    void invalidateThumbnail(PageInfo const& page_info);

    void invalidateAllThumbnails();

    void showRelinkingDialog();

    void filterResult(
        BackgroundTaskPtr const& task,
        FilterResultPtr const& result);

    void debugToggled(bool enabled);

    void saveProjectTriggered();

    void saveProjectAsTriggered();

    void newProject();

    void newProjectCreated(ProjectCreationContext* context);

    void openProject();

    void projectOpened(ProjectOpeningContext* context);

    void closeProject();

    void openSettingsDialog();

    void showAboutDialog();

    void handleOutOfMemorySituation();

    void stylesheetChanged(const QString stylesheetFilePath);
private:
    class PageSelectionProviderImpl;
    enum SavePromptResult { SAVE, DONT_SAVE, CANCEL };

    typedef IntrusivePtr<AbstractFilter> FilterPtr;

    virtual void setOptionsWidget(
        FilterOptionsWidget* widget, Ownership ownership);

    void setUnifiedPdfAppearance(QString const& appearance) override;

    virtual void setImageWidget(
        QWidget* widget, Ownership ownership,
        DebugImagesImpl* debug_images = 0);

    virtual IntrusivePtr<AbstractCommand0<void> > relinkingDialogRequester();

    void switchToNewProject(
        IntrusivePtr<ProjectPages> const& pages,
        QString const& out_dir,
        QString const& project_file_path = QString(),
        ProjectReader const* project_reader = 0);

    IntrusivePtr<ThumbnailPixmapCache> createThumbnailCache();

    bool eventFilter(QObject* obj, QEvent* ev);

    void setupThumbView();

    void showNewOpenProjectPanel();

    SavePromptResult promptProjectSave();

    static bool compareFiles(QString const& fpath1, QString const& fpath2);

    IntrusivePtr<PageOrderProvider const> currentPageOrderProvider() const;

    void updateSortOptions();

    void resetThumbSequence(
        IntrusivePtr<PageOrderProvider const> const& page_order_provider);

    void removeWidgetsFromLayout(QLayout* layout);

    void removeFilterOptionsWidget();

    void removeImageWidget();

    void updateProjectActions();

    bool isBatchProcessingInProgress() const;

    bool isProjectLoaded() const;

    bool isBelowSelectContent() const;

    bool isBelowSelectContent(int filter_idx) const;

    bool isBelowFixOrientation(int filter_idx) const;

    bool isOutputFilter() const;

    bool isOutputFilter(int filter_idx) const;

    PageView getCurrentView() const;

    void updateMainArea();

    bool checkReadyForOutput(PageId const* ignore = 0) const;

    void loadPageInteractive(PageInfo const& page);

    void updateWindowTitle();

    bool closeProjectInteractive();

    void closeProjectWithoutSaving();

    bool saveProjectWithFeedback(QString const& project_file);

    void showInsertFileDialog(
        BeforeOrAfter before_or_after, ImageId const& existig);

    void showRemovePagesDialog(std::set<PageId> const& pages);

    void insertImage(ImageInfo const& new_image,
                     BeforeOrAfter before_or_after, ImageId existing);

    void removeFromProject(std::set<PageId> const& pages);

    void eraseOutputFiles(std::set<PageId> const& pages);

    BackgroundTaskPtr createCompositeTask(
        PageInfo const& page, int last_filter_idx, bool batch, bool debug);

    IntrusivePtr<CompositeCacheDrivenTask>
    createCompositeCacheDrivenTask(int last_filter_idx);

    void createBatchProcessingWidget();

    void updateDisambiguationRecords(PageSequence const& pages);

    void performRelinking(IntrusivePtr<AbstractRelinker> const& relinker);

    PageSelectionAccessor newPageSelectionAccessor();

    QSizeF m_maxLogicalThumbSize;
    IntrusivePtr<ProjectPages> m_ptrPages;
    IntrusivePtr<StageSequence> m_ptrStages;
    QString m_projectFile;
    OutputFileNameGenerator m_outFileNameGen;
    IntrusivePtr<ThumbnailPixmapCache> m_ptrThumbnailCache;
    std::unique_ptr<ThumbnailSequence> m_ptrThumbSequence;
    std::unique_ptr<WorkerThreadPool> m_ptrWorkerThreadPool;
    std::unique_ptr<ProcessingTaskQueue> m_ptrBatchQueue;
    std::unique_ptr<ProcessingTaskQueue> m_ptrInteractiveQueue;
    DefaultAccelerationProvider* m_pAccelerationProvider;
    QStackedLayout* m_pImageFrameLayout;
    QStackedLayout* m_pOptionsFrameLayout;
    QPointer<FilterOptionsWidget> m_ptrOptionsWidget;
    std::unique_ptr<TabbedDebugImages> m_ptrTabbedDebugImages;
    std::unique_ptr<ContentBoxPropagator> m_ptrContentBoxPropagator;
    std::unique_ptr<PageOrientationPropagator> m_ptrPageOrientationPropagator;
    std::unique_ptr<QWidget> m_ptrBatchProcessingWidget;
    std::unique_ptr<ProcessingIndicationWidget> m_ptrProcessingIndicationWidget;
    boost::function<bool()> m_checkBeepWhenFinished;
    SelectedPage m_selectedPage;
    QObjectCleanupHandler m_optionsWidgetCleanup;
    QObjectCleanupHandler m_imageWidgetCleanup;
    std::unique_ptr<OutOfMemoryDialog> m_ptrOutOfMemoryDialog;
    int m_curFilter;
    int m_ignoreSelectionChanges;
    int m_ignorePageOrderingChanges;
    bool m_debug;
    bool m_closing;
    bool m_beepOnBatchProcessingCompletion;
    QPalette m_systemPalette;
};

#endif
