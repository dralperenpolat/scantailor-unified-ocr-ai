/*
    Scan Tailor - Interactive post-processing tool for scanned pages.

    ConsoleBatch - Batch processing scanned pages from command line.
    Copyright (C) 2011 Petr Kovar <pejuko@gmail.com>

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
#include <vector>
#include <iostream>
#include <assert.h>
#include <future>
#include <algorithm>


#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QIODevice>
#include <QDomDocument>
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QSaveFile>
#include <QtCore/QMap>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QDebug>
#include <QtCore/QTextStream>
#include <QtCore/QStringList>
#include <QtCore/QSize>

#include "Utils.h"
#include "UnifiedContentReview.h"
#include "IntrusivePtr.h"
#include "NonCopyable.h"
#include "ProjectPages.h"
#include "PageSelectionAccessor.h"
#include "StageSequence.h"
#include "ProcessingTaskQueue.h"
#include "FileNameDisambiguator.h"
#include "OutputFileNameGenerator.h"
#include "ImageInfo.h"
#include "ImageFileInfo.h"
#include "PageInfo.h"
#include "PageSequence.h"
#include "ImageId.h"
#include "ThumbnailPixmapCache.h"
#include "LoadFileTask.h"
#include "ProjectWriter.h"
#include "ProjectReader.h"
#include "OrthogonalRotation.h"
#include "SelectedPage.h"
#include "acceleration/DefaultAccelerationProvider.h"
#include "acceleration/NonAcceleratedOperations.h"

#include "stages/fix_orientation/Settings.h"
#include "stages/fix_orientation/Filter.h"
#include "stages/fix_orientation/Task.h"
#include "stages/fix_orientation/CacheDrivenTask.h"
#include "stages/page_split/Settings.h"
#include "stages/page_split/Filter.h"
#include "stages/page_split/Task.h"
#include "stages/page_split/CacheDrivenTask.h"
#include "stages/deskew/Settings.h"
#include "stages/deskew/Filter.h"
#include "stages/deskew/Task.h"
#include "stages/deskew/CacheDrivenTask.h"
#include "stages/select_content/Settings.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/Task.h"
#include "stages/select_content/CacheDrivenTask.h"
#include "stages/page_layout/Settings.h"
#include "stages/page_layout/Filter.h"
#include "stages/page_layout/Task.h"
#include "stages/page_layout/CacheDrivenTask.h"
#include "stages/output/Settings.h"
#include "stages/output/Params.h"
#include "stages/output/Filter.h"
#include "stages/output/Task.h"
#include "stages/output/CacheDrivenTask.h"



#include "ConsoleBatch.h"
#include "CommandLine.h"

ConsoleBatch::ConsoleBatch(std::vector<ImageFileInfo> const& images, QString const& output_directory, ::Qt::LayoutDirection const layout)
    :   batch(true), debug(true),
        m_pAccelerationProvider(nullptr),
        m_ptrDisambiguator(new FileNameDisambiguator()),
        m_ptrPages(new ProjectPages(images, ProjectPages::AUTO_PAGES, layout))
{
    try {
        m_pAccelerationProvider = new DefaultAccelerationProvider(::QCoreApplication::instance());
    } catch (...) {
        std::cerr << "Warning: Failed to initialize acceleration provider, continuing without acceleration." << std::endl;
    }
    
    PageSelectionAccessor const accessor((::IntrusivePtr<PageSelectionProvider>())); // Won't really be used anyway.
    m_ptrStages = ::IntrusivePtr<StageSequence>(new StageSequence(m_ptrPages, accessor));

    std::shared_ptr<AcceleratableOperations> accel_ops = std::make_shared<NonAcceleratedOperations>();
    if (m_pAccelerationProvider) {
        try {
            accel_ops = m_pAccelerationProvider->getOperations();
        } catch (...) {
            std::cerr << "Warning: Failed to get acceleration operations, using non-accelerated mode." << std::endl;
        }
    }
    m_ptrThumbnailCache = Utils::createThumbnailCache(output_directory, accel_ops);
    m_outFileNameGen = OutputFileNameGenerator(m_ptrDisambiguator, output_directory, m_ptrPages->layoutDirection());
}

ConsoleBatch::ConsoleBatch(::QString const project_file)
    :   batch(true), debug(true),
        m_pAccelerationProvider(nullptr)
{
    try {
        m_pAccelerationProvider = new DefaultAccelerationProvider(::QCoreApplication::instance());
    } catch (...) {
        std::cerr << "Warning: Failed to initialize acceleration provider, continuing without acceleration." << std::endl;
    }
    
    ::QFile file(project_file);
    if (!file.open(::QIODevice::ReadOnly))
    {
        throw std::runtime_error("Unable to open the project file.");
    }

    ::QDomDocument doc;
    if (!doc.setContent(&file))
    {
        throw std::runtime_error("The project file is broken.");
    }

    file.close();

    m_ptrReader.reset(new ProjectReader(doc));
    m_ptrPages = m_ptrReader->pages();

    PageSelectionAccessor const accessor((::IntrusivePtr<PageSelectionProvider>())); // Won't be used anyway.
    m_ptrDisambiguator = m_ptrReader->namingDisambiguator();

    m_ptrStages = ::IntrusivePtr<StageSequence>(new StageSequence(m_ptrPages, accessor));
    m_ptrReader->readFilterSettings(m_ptrStages->filters());

    CommandLine const& cli = CommandLine::get();
    ::QString output_directory = m_ptrReader->outputDirectory();
    if (!cli.outputDirectory().isEmpty())
    {
        output_directory = cli.outputDirectory();
    }

    // Create thumbnail cache with null pointer check
    std::shared_ptr<AcceleratableOperations> accel_ops = std::make_shared<NonAcceleratedOperations>();
    if (m_pAccelerationProvider) {
        try {
            // Use public interface to get operations
            accel_ops = m_pAccelerationProvider->getOperations();
        } catch (...) {
            std::cerr << "Warning: Failed to get acceleration operations, using non-accelerated mode." << std::endl;
        }
    }
    m_ptrThumbnailCache = Utils::createThumbnailCache(output_directory, accel_ops);
    m_outFileNameGen = OutputFileNameGenerator(m_ptrDisambiguator, output_directory, m_ptrPages->layoutDirection());
}


BackgroundTaskPtr
ConsoleBatch::createCompositeTask(
    PageInfo const& page,
    int const last_filter_idx)
{
    ::IntrusivePtr<fix_orientation::Task> fix_orientation_task;
    ::IntrusivePtr<page_split::Task> page_split_task;
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

    std::shared_ptr<AcceleratableOperations> accel_ops = std::make_shared<NonAcceleratedOperations>();
    if (m_pAccelerationProvider) {
        try {
            // Use public interface to get operations
            accel_ops = m_pAccelerationProvider->getOperations();
        } catch (...) {
            std::cerr << "Warning: Failed to get acceleration operations, using non-accelerated mode." << std::endl;
        }
    }
    
    return BackgroundTaskPtr(
               new LoadFileTask(
                   BackgroundTask::BATCH, page,
                   accel_ops,
                   m_ptrThumbnailCache, m_ptrPages, fix_orientation_task
               )
           );
}


// process the image vector **images** and save output to **output_dir**
namespace {
void unifiedEvent(QJsonObject const& object) {
    if (qEnvironmentVariableIsSet("STOU_PROGRESS"))
        std::cout << "STOU " << QJsonDocument(object).toJson(QJsonDocument::Compact).constData() << std::endl;
}
void unifiedCheckpoint() {
    QString path=qEnvironmentVariable("STOU_CONTROL_FILE");if(path.isEmpty())return;
    bool paused=false;
    for (;;) {
        QFile file(path);QString state;
        if(file.open(QIODevice::ReadOnly))state=QJsonDocument::fromJson(file.readAll()).object().value("state").toString();
        if(state=="cancel")throw std::runtime_error("Unified: cancelled at page boundary");
        if(state!="pause")return;
        if(!paused){unifiedEvent({{"stage",unified_i18n::text("Duraklatıldı")},{"detail",unified_i18n::text("Mevcut sayfa korundu.")}});paused=true;}
        QThread::msleep(150);
    }
}
}
void
ConsoleBatch::process()
{
    CommandLine const& cli = CommandLine::get();
    auto contentReview=[&]{
        auto samples=unified_content::collect(*m_ptrPages,*m_ptrStages);
        auto result=unified_content::review(std::move(samples),[&](int n,int total){unifiedCheckpoint();unifiedEvent({{"stage",unified_i18n::text("Kitap içerik denetimi")},{"page",n},{"total",total}});return true;});
        if(qEnvironmentVariable("STOU_CONTENT_REVIEW")=="apply-safe")for(auto const& p:result.proposals)if(p.safe)unified_content::apply(p,result.samples[p.index],*m_ptrStages);
        QString path=qEnvironmentVariable("STOU_CONTENT_REVIEW_REPORT");
        if(path.isEmpty()&&cli.hasOutputProject())path=cli.outputProjectFile()+".content-review.json";
        if(!path.isEmpty()){QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(result.toJson()).toJson())<0||!file.commit())throw std::runtime_error("Content review report could not be saved");
            if(qEnvironmentVariableIsSet("STOU_CONTENT_REVIEW_PREVIEWS"))for(auto const& p:result.proposals)p.preview.save(path+QString(".page-%1.png").arg(result.samples[p.index].ordinal));}
    };
    if(qEnvironmentVariableIsSet("STOU_CONTENT_REVIEW_ONLY")){contentReview();return;}

    int startFilterIdx = m_ptrStages->fixOrientationFilterIdx();
    if (cli.hasStartFilterIdx())
    {
        unsigned int sf = cli.getStartFilterIdx();
        if (sf>=m_ptrStages->filters().size())
            throw std::runtime_error("Start filter out of range");
        startFilterIdx = sf;
    }

    //int endFilterIdx = m_ptrStages->outputFilterIdx();
    int endFilterIdx = m_ptrStages->selectContentFilterIdx();
    if (cli.hasEndFilterIdx())
    {
        unsigned int ef = cli.getEndFilterIdx();
        if (ef>=m_ptrStages->filters().size())
            throw std::runtime_error("End filter out of range");
        endFilterIdx = ef;
    }

    bool resumeOk=true;
    int resumePage=qEnvironmentVariableIsSet("STOU_RESUME_PAGE")
        ? qEnvironmentVariableIntValue("STOU_RESUME_PAGE",&resumeOk) : 0;
    if(!resumeOk || resumePage<0)throw std::runtime_error("Invalid resume page");
    for (int j=startFilterIdx; j<=endFilterIdx; j++)
    {
        if (cli.isVerbose())
            std::cout << "Filter: " << (j+1) << "\n";

        PageSequence page_sequence = m_ptrPages->toPageSequence(PAGE_VIEW);
        unsigned firstPage=j==startFilterIdx ? unsigned(resumePage) : 0;
        if(firstPage>page_sequence.numPages())throw std::runtime_error("Resume page out of range");
        setupFilter(j, page_sequence.selectAll());
        if(j==m_ptrStages->pageLayoutFilterIdx()&&qEnvironmentVariableIsSet("STOU_CONTENT_REVIEW"))contentReview();
        // Earlier passes establish geometry and the book-wide layout first.
        // Only final, independent output tasks may overlap. Task creation and
        // project checkpoints stay on this thread, outside all active tasks.
        int output_workers = qBound(1, qEnvironmentVariableIntValue("STOU_OUTPUT_WORKERS"), 4);
        if (j == m_ptrStages->outputFilterIdx() && output_workers > 1)
        {
            for (unsigned first = firstPage; first < page_sequence.numPages(); first += output_workers)
            {
                unifiedCheckpoint();
                unsigned end = std::min<unsigned>(page_sequence.numPages(), first + output_workers);
                std::vector<std::future<FilterResultPtr>> tasks;
                std::vector<BackgroundTaskPtr> preparedTasks;
                unifiedEvent({{"stage",m_ptrStages->filterAt(j)->getName()}, {"page",int(first)}, {"total",int(page_sequence.numPages())}, {"detail",unified_i18n::text("Parallel output: %1 pages").arg(end-first)}});
                for (unsigned i = first; i < end; ++i)
                {
                    preparedTasks.push_back(createCompositeTask(page_sequence.pageAt(i), j));
                }
                // Construct every pipeline before workers enter shared filters.
                // Creating a later task while the earlier one was running could
                // invalidate state read by that worker (notably split spreads).
                for (auto const& task : preparedTasks)
                {
                    tasks.push_back(std::async(std::launch::async, [task] { return (*task)(); }));
                }
                for (auto& task : tasks) task.get();
                if (cli.hasOutputProject() && qEnvironmentVariableIsSet("STOU_PROGRESS")) saveProject(cli.outputProjectFile());
                for (unsigned i = first; i < end; ++i)
                {
                    PageInfo page = page_sequence.pageAt(i);
                    unifiedEvent({{"stage",m_ptrStages->filterAt(j)->getName()}, {"page",int(i+1)}, {"total",int(page_sequence.numPages())}, {"detail",QFileInfo(page.imageId().filePath()).fileName()}, {"preview",m_outFileNameGen.filePathFor(page.id())}});
                }
            }
            continue;
        }
        for (unsigned i=firstPage; i<page_sequence.numPages(); i++)
        {
            unifiedCheckpoint();
            PageInfo page = page_sequence.pageAt(i);
            unifiedEvent({{"stage",m_ptrStages->filterAt(j)->getName()}, {"page",int(i)}, {"total",int(page_sequence.numPages())}, {"detail",QFileInfo(page.imageId().filePath()).fileName()}});
            if (cli.isVerbose())
                std::cout << "\tProcessing: " << page.imageId().filePath().toLocal8Bit().constData() << "\n";
            BackgroundTaskPtr bgTask = createCompositeTask(page, j);
            (*bgTask)();
            QJsonObject event{{"stage",m_ptrStages->filterAt(j)->getName()},{"page",int(i+1)},{"total",int(page_sequence.numPages())},{"detail",QFileInfo(page.imageId().filePath()).fileName()}};
            if(j==m_ptrStages->outputFilterIdx())event.insert("preview",m_outFileNameGen.filePathFor(page.id()));
            unifiedEvent(event);
            if(cli.hasOutputProject() && qEnvironmentVariableIsSet("STOU_PROGRESS")) saveProject(cli.outputProjectFile());
        }
    }
}

void
ConsoleBatch::saveProject(QString const project_file)
{
    PageInfo fpage = m_ptrPages->toPageSequence(PAGE_VIEW).pageAt(0);
    SelectedPage sPage(fpage.id(), IMAGE_VIEW);
    ProjectWriter writer(m_ptrPages, sPage, m_outFileNameGen);
    if(!writer.write(project_file, m_ptrStages->filters()))throw std::runtime_error("Project checkpoint write failed");
    QJsonArray outputs;
    auto sequence=m_ptrPages->toPageSequence(PAGE_VIEW);
    for(size_t i=0;i<sequence.numPages();++i)outputs.append(m_outFileNameGen.filePathFor(sequence.pageAt(i).id()));
    QSaveFile manifest(project_file+".outputs.json");
    if(manifest.open(QIODevice::WriteOnly)){manifest.write(QJsonDocument(QJsonObject{{"pages",outputs}}).toJson());manifest.commit();}
}


void
ConsoleBatch::setupFilter(int idx, std::set<PageId> allPages)
{
    if (idx == m_ptrStages->fixOrientationFilterIdx())
        setupFixOrientation(allPages);
    else if (idx == m_ptrStages->pageSplitFilterIdx())
        setupPageSplit(allPages);
    else if (idx == m_ptrStages->deskewFilterIdx())
        setupDeskew(allPages);
    else if (idx == m_ptrStages->selectContentFilterIdx())
        setupSelectContent(allPages);
#if 0
    else if (idx == m_ptrStages->pageLayoutFilterIdx())
        setupPageLayout(allPages);
    else if (idx == m_ptrStages->outputFilterIdx())
        setupOutput(allPages);
#endif
}


void
ConsoleBatch::setupFixOrientation(std::set<PageId> allPages)
{
    IntrusivePtr<fix_orientation::Filter> fix_orientation = m_ptrStages->fixOrientationFilter();
    CommandLine const& cli = CommandLine::get();

    for (std::set<PageId>::iterator i=allPages.begin(); i!=allPages.end(); i++)
    {
        PageId page = *i;

        OrthogonalRotation rotation;
        // FIX ORIENTATION FILTER
        if (cli.hasOrientation())
        {
            switch(cli.getOrientation())
            {
            case CommandLine::LEFT:
                rotation.prevClockwiseDirection();
                break;
            case CommandLine::RIGHT:
                rotation.nextClockwiseDirection();
                break;
            case CommandLine::UPSIDEDOWN:
                rotation.nextClockwiseDirection();
                rotation.nextClockwiseDirection();
                break;
            default:
                break;
            }
            fix_orientation->getSettings()->applyRotation(page.imageId(), rotation);
        }
    }
}


void
ConsoleBatch::setupPageSplit(std::set<PageId> allPages)
{
    Q_UNUSED(allPages);
    
    IntrusivePtr<page_split::Filter> page_split = m_ptrStages->pageSplitFilter();
    CommandLine const& cli = CommandLine::get();

    // PAGE SPLIT
    if (cli.hasLayout())
    {
        page_split->getSettings()->setLayoutTypeForAllPages(cli.getLayout());
    }
}


void
ConsoleBatch::setupDeskew(std::set<PageId> allPages)
{
    IntrusivePtr<deskew::Filter> deskew = m_ptrStages->deskewFilter();
    CommandLine const& cli = CommandLine::get();

    for (std::set<PageId>::iterator i=allPages.begin(); i!=allPages.end(); i++)
    {
        PageId page = *i;

        // DESKEW FILTER
        OrthogonalRotation rotation;
        if (cli.hasDeskewAngle() || cli.hasDeskew())
        {
            double angle = 0.0;
            if (cli.hasDeskewAngle())
                angle = cli.getDeskewAngle();
            deskew::Dependencies deps(QPolygonF()/*XXX*/, rotation);
            deskew::Params params(deps);
            params.setDistortionType(deskew::DistortionType::ROTATION);
            params.getParamsRotation().setCompensationAngleDeg(angle);
            deskew->getSettings()->setPageParams(page, params);
        }
    }
}


void
ConsoleBatch::setupSelectContent(std::set<PageId> allPages)
{
    IntrusivePtr<select_content::Filter> select_content = m_ptrStages->selectContentFilter();
    CommandLine const& cli = CommandLine::get();

    for (std::set<PageId>::iterator i=allPages.begin(); i!=allPages.end(); i++)
    {
        PageId page = *i;

        // SELECT CONTENT FILTER
        if (cli.hasContentRect())
        {
#if 0
            QRectF rect(cli.getContentRect());
            QSizeF size_mm(rect.width(), rect.height());
            select_content::Dependencies deps;
            select_content::Params params(rect, size_mm, deps, MODE_MANUAL);
            select_content->getSettings()->setPageParams(page, params);
#endif
        }
    }
}

#if 0
void
ConsoleBatch::setupPageLayout(std::set<PageId> allPages)
{
    IntrusivePtr<page_layout::Filter> page_layout = m_ptrStages->pageLayoutFilter();
    CommandLine const& cli = CommandLine::get();

    for (std::set<PageId>::iterator i=allPages.begin(); i!=allPages.end(); i++)
    {
        PageId page = *i;

        // PAGE LAYOUT FILTER
        page_layout::Alignment alignment = cli.getAlignment();
        if (cli.hasMargins())
            page_layout->getSettings()->setHardMarginsMM(page, cli.getMargins());
        if (cli.hasAlignment())
            page_layout->getSettings()->setPageAlignment(page, alignment);
    }
}


void
ConsoleBatch::setupOutput(std::set<PageId> allPages)
{
    IntrusivePtr<output::Filter> output = m_ptrStages->outputFilter();
    CommandLine const& cli = CommandLine::get();

    for (std::set<PageId>::iterator i=allPages.begin(); i!=allPages.end(); i++)
    {
        PageId page = *i;

        // OUTPUT FILTER
        output::Params params(output->getSettings()->getParams(page));
        if (cli.hasOutputDpi())
        {
            Dpi outputDpi = cli.getOutputDpi();
            params.setOutputDpi(outputDpi);
        }

        output::ColorParams colorParams = params.colorParams();
        if (cli.hasColorMode())
            colorParams.setColorMode(cli.getColorMode());

        if (cli.hasWhiteMargins() || cli.hasNormalizeIllumination())
        {
            output::ColorGrayscaleOptions cgo;
            if (cli.hasWhiteMargins())
                cgo.setWhiteMargins(true);
            if (cli.hasNormalizeIllumination())
                cgo.setNormalizeIllumination(true);
            colorParams.setColorGrayscaleOptions(cgo);
        }

        if (cli.hasThreshold())
        {
            output::BlackWhiteOptions bwo;
            bwo.setThresholdAdjustment(cli.getThreshold());
            colorParams.setBlackWhiteOptions(bwo);
        }

        params.setColorParams(colorParams);

        if (cli.hasDespeckle())
            params.setDespeckleLevel(cli.getDespeckleLevel());

        if (cli.hasDewarping())
            params.setDewarpingMode(cli.getDewarpingMode());
        if (cli.hasDepthPerception())
            params.setDepthPerception(cli.getDepthPerception());

        output->getSettings()->setParams(page, params);
    }
}
#endif
