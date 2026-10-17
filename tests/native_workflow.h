// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StageSequence.h"
#include "ProjectPages.h"
#include "PageSequence.h"
#include "UnifiedWorkflow.h"
#include "UnifiedProjectCache.h"
#include "UnifiedContentReview.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/Settings.h"
#include "stages/page_layout/Filter.h"
#include <QPainter>
#include <QToolButton>
#include <QTextBrowser>
#include <QHeaderView>
#include <QProcess>
#include <QStackedWidget>

class UnifiedWorkflowTest {
    template<class Condition> static void wait(Condition done,int limit=90000){
        QElapsedTimer timer;timer.start();
        while(!done()&&timer.elapsed()<limit){QApplication::processEvents();QThread::msleep(20);}
        if(!done())throw std::runtime_error("workflow timed out");
        QApplication::processEvents();
    }
public:
    static void profileProject(){
        auto path=qEnvironmentVariable("STOU_TEST_PROJECT");if(path.isEmpty())return;
        MainWindow window;window.show();QElapsedTimer clock;clock.start();window.openProject(path);
        wait([&]{return window.isProjectLoaded();});auto openMs=clock.elapsed();
        clock.restart();auto stamp=window.workflowFingerprint();auto firstMs=clock.elapsed();clock.restart();
        for(int i=0;i<5;++i)check(window.workflowFingerprint()==stamp,"large project fingerprint stays stable");
        std::cerr<<"large project: pages="<<window.workflowPages().size()<<" open_ms="<<openMs<<" fingerprint_ms="<<firstMs<<" repeat_average_ms="<<clock.elapsed()/5<<std::endl;
        window.m_closing=true;
    }
    static void queueRecovery(MainWindow& window,QString const& folder){
        auto panel=window.m_unified;auto original=panel->queuePath();auto title=window.windowTitle();
        auto put=[](QString const& path,QByteArray const& bytes){QFile file(path);check(file.open(QIODevice::WriteOnly),"queue recovery fixture");check(file.write(bytes)==bytes.size(),"queue recovery bytes");};
        QJsonArray pages;for(int i=1;i<=12000;++i)pages.append(QJsonObject{{"page",i},{"preview",folder+QString("/offline/page-%1.png").arg(i)}});
        auto path=folder+"/saved-run.json";auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"output_root",folder+"/recovered-output"},{"jobs",QJsonArray{QJsonObject{{"id","recovery"},{"title","Recovered book"},{"status","Durduruldu"},{"pages",pages}}}}}).toJson();put(path,bytes);
        window.openProject(path);check(panel->queuePath()==path,"File/Open routes JSON to run recovery");
        check(window.windowTitle().contains(title),"opening saved run keeps application identity");
        auto recoveredTitle=window.windowTitle();window.openProject(path);check(window.windowTitle()==recoveredTitle,"repeated saved-run launch keeps a stable window title");
        check(!panel->busy(),"opening saved run never starts processing");
        auto recent=panel->findChild<QTreeWidget*>("unifiedCompletedPages");check(recent->topLevelItemCount()==500,"legacy queue only materializes 500 recent records");
        check(recent->topLevelItem(0)->text(0).endsWith("11501")&&recent->topLevelItem(499)->text(0).endsWith("12000"),"recent records retain correct order even with offline previews");
        QFile before(path);check(before.open(QIODevice::ReadOnly)&&before.readAll()==bytes,"opening legacy queue is read-only");before.close();
        CommandLine cli(QStringList{"test",path},true);check(cli.projectFile()==path,"saved run opens from desktop command line");
        put(folder+"/broken.json","{\"schema\":");check(!panel->openSavedQueue(folder+"/broken.json")&&panel->queuePath()==path,"damaged run leaves active queue intact");
        auto request=folder+"/request.json";put(request,QJsonDocument(QJsonObject{{"queue","saved-run.json"},{"control",folder+"/control.json"}}).toJson());
        check(panel->openSavedQueue(request)&&panel->queuePath()==path,"request resolves relative queue path");
        check(panel->openSavedQueue(request+".session.json")&&panel->queuePath()==path,"session file resolves its request");
        auto env=qgetenv("STOU_QUEUE_PATH");qunsetenv("STOU_QUEUE_PATH");{UnifiedPanel restored;check(restored.queuePath()==path,"last opened run is restored on restart");}qputenv("STOU_QUEUE_PATH",env);
        auto real=qEnvironmentVariable("STOU_TEST_RECOVERY_QUEUE");
        if(!real.isEmpty()){QElapsedTimer elapsed;elapsed.start();check(panel->openSavedQueue(real),"real legacy queue opens");std::cerr<<"legacy queue opened in "<<elapsed.elapsed()<<" ms; books="<<panel->findChild<QTreeWidget*>("unifiedBooks")->topLevelItemCount()<<std::endl;}
        check(panel->openSavedQueue(original),"restore test queue");
        window.refreshWorkflowState();
        check(!window.findChild<QPushButton*>("workflowOpenPdf")->isEnabled(),"no PDF means Open latest PDF is disabled");
        check(!window.findChild<QPushButton*>("workflowRefreshPdf")->isEnabled(),"no project means refresh is disabled");
        check(!window.findChild<QPushButton*>("workflow_cleanup_apply")->isEnabled(),"cleanup apply waits for a proposal");
    }
    static void layout(MainWindow& window,QString const& captures,QString const& lang){
        auto list=window.filterList;
        check(list->model()->rowCount()==15,"all ordered stages, settings and help are visible");
        for(int row=0;row<15;++row){
            auto step=unified_workflow::steps().at(row);
            check(list->stepId(row)==step.id,"workflow identity remains separate from engine index");
            if(step.playable){auto b=list->findChild<QToolButton*>("stagePlay_"+step.id);check(b&&!b->icon().isNull(),"every operation has its own visible play icon");}
        }
        list->selectStep("input");QApplication::processEvents();
        window.resize(1366,820);QApplication::processEvents();
        check(list->columnWidth(0)>=QFontMetrics(list->font()).horizontalAdvance(unified_workflow::steps().at(9).title),"long cleanup stage fits without shrinking font");
        check(list->viewport()->height()>=list->verticalHeader()->length(),"all stage rows fit without scrolling");
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-input-"+lang+".png"),"input screenshot");
        list->selectStep("help");QApplication::processEvents();
        auto browser=window.m_workflowHelp->findChild<QTextBrowser*>("workflowHelpText");
        check(browser->toPlainText().contains(lang=="tr"?"doğruluk":"accuracy"),"localized accuracy explanation exists");
        check(!browser->document()->resource(QTextDocument::ImageResource,QUrl("help-button:1")).isNull(),"guide embeds button images");
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-help-"+lang+".png"),"help screenshot");
        list->selectStep("storage");QApplication::processEvents();
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-cache-"+lang+".png"),"localized cache stage");
        if(lang=="tr")check(list->model()->index(5,0).data().toString().contains(QString::fromUtf8("İçerik seçimi")),"Turkish content stage is translated");
    }
    static void editorImport(QString const& source){
        if(source.isEmpty())return;
        MainWindow window;window.show();auto panel=window.m_unified;
        QTemporaryDir existing;auto previous=existing.path()+"/existing.png";QImage oldImage(600,800,QImage::Format_RGB32);oldImage.fill(Qt::white);check(oldImage.save(previous),"existing editor fixture");
        check(window.importWorkflowImages({previous}),"open previous unsaved book");
        auto queue=panel->findChild<QTreeWidget*>("unifiedBooks");int before=queue->topLevelItemCount();
        panel->importPdf(source,true);
        auto pdf=static_cast<UnifiedPdfToolsDialog*>(panel->findChild<QWidget*>("unifiedPdfTools"));check(pdf&&pdf->running(),"editor PDF import starts");
        window.refreshWorkflowState();check(!window.findChild<QPushButton*>("workflowRun")->isEnabled()&&!window.findChild<QToolButton*>("stagePlay_content")->isEnabled(),"import disables conflicting stage actions");
        window.runWorkflowStep("content");check(!window.isBatchProcessingInProgress(),"direct stage action also refuses overlapping import");
        window.openProject(existing.path()+"/blocked.ScanTailor");check(window.isProjectLoaded(),"File/Open also refuses project replacement during import");
        check(!panel->openSavedQueue(panel->queuePath()),"cannot replace queue during import");
        bool cancelled=false;QTimer dismissImport;QObject::connect(&dismissImport,&QTimer::timeout,[&]{if(auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){if(auto cancel=box->button(QMessageBox::Cancel)){cancelled=true;cancel->click();}}});dismissImport.start(20);
        wait([&]{return !pdf->running()&&cancelled;});dismissImport.stop();
        check(window.m_ptrPages->toPageSequence(IMAGE_VIEW).pageAt(0).id().imageId().filePath()==previous,"cancel project replacement keeps current book");
        auto retry=pdf->findChild<QPushButton*>("pdfImportAddReady");check(!retry->isHidden(),"cancel retains prepared import for retry");
        QTimer::singleShot(0,[]{auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());check(box,"retry asks about unsaved project");box->button(QMessageBox::Discard)->click();});retry->click();
        wait([&]{return window.isProjectLoaded()&&!panel->busy();});
        auto inputs=window.m_ptrPages->toPageSequence(IMAGE_VIEW);auto input=inputs.pageAt(0).id().imageId().filePath();
        check(QFileInfo::exists(input),"stage zero imports editable PDF image");
        check(input!=previous&&retry->isHidden(),"retry opens prepared images without importing again");
        QTimer::singleShot(0,[]{auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());check(box,"invalid input shows error");box->accept();});
        check(!window.importWorkflowImages({existing.path()+"/missing.png"}),"missing input is rejected");
        check(window.isProjectLoaded()&&QFileInfo::exists(input),"invalid input preserves current editor and extracted pages");
        check(queue->topLevelItemCount()==before,"editor PDF import does not create a second queue owner");
        auto dismiss=[](QMessageBox::StandardButton choice){QTimer::singleShot(0,[choice]{auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());check(box,"close project prompt");box->button(choice)->click();});};
        dismiss(QMessageBox::Cancel);check(!window.closeProjectInteractive(),"cancel close keeps project");
        check(QFileInfo::exists(input),"cancel never cleans inputs");
        dismiss(QMessageBox::Discard);check(window.closeProjectInteractive(),"discard closes new project");
        check(!QFileInfo::exists(input),"discard removes only owned extracted image");
        check(QFileInfo::exists(source),"original PDF survives discard");
        window.m_closing=true;
    }
    static void storage(QString const& folder,QString const& finalPdf){
        QString root=folder+"/cache-test",work=root+"/work",inputs=root+"/inputs",ocr=root+"/ocr",outside=folder+"/source-book.jpg";
        for(auto path:{root,work,inputs,ocr})QDir().mkpath(path);
        auto put=[](QString const& path){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);check(f.open(QIODevice::WriteOnly),"cache fixture");f.write("owned test data");};
        auto visible=root+"/page.tif",gray=visible+".ocr.tif",input=inputs+"/page-00001.png",preview=work+"/page-before.png",mask=work+"/page-mask.png",temporary=work+"/image-pdf.part.pdf",cache=ocr+"/text.pdf";
        for(auto path:{visible,gray,input,preview,mask,temporary,cache,outside})put(path);
        QJsonArray pages{QJsonObject{{"source",visible},{"gray",gray}},QJsonObject{{"source",outside},{"gray",outside}}};
        UnifiedProjectCache widget;widget.context(root,{},pages,finalPdf,"wrong hash");
        check(widget.own(root,"native")&&widget.own(work,"work")&&widget.own(inputs,"sources")&&widget.own(ocr,"ocr"),"cache roots owned exclusively");
        widget.findChild<QCheckBox*>("workflowCacheKeep_native")->setChecked(false);
        widget.findChild<QCheckBox*>("workflowCacheKeep_ocr")->setChecked(false);
        widget.scan();auto table=widget.findChild<QTableWidget*>("workflowCacheTable");
        check(table->item(2,2)->text()!="0 B","OCR category reports actual bytes");
        auto result=widget.clean();
        check(result.value("files").toInt()>=3,"selected cache categories removed");
        check(QFileInfo::exists(visible)&&QFileInfo::exists(input)&&QFileInfo::exists(mask)&&QFileInfo::exists(outside),"unverified output, active inputs, edits and original protected");
        check(!QFileInfo::exists(cache)&&!QFileInfo::exists(preview)&&!QFileInfo::exists(temporary),"OCR, previews and temporary PDF deleted");
        for(auto path:{gray,cache,temporary})put(path);widget.own(ocr,"ocr");
        widget.findChild<QCheckBox*>("workflowCacheKeep_ocr")->setChecked(true);
        widget.clean("discard");
        check(!QFileInfo::exists(input),"unsaved discard removes generated input");
        check(!QFileInfo::exists(mask),"unsaved discard removes temporary correction");
        check(!QFileInfo::exists(cache),"unsaved discard overrides OCR keep");
        check(!QFileInfo::exists(gray),"unsaved discard removes gray cache");
        check(QFileInfo::exists(visible)&&QFileInfo::exists(finalPdf)&&QFileInfo::exists(outside),"discard retains Output, final PDF and source book");
        for(auto path:{input,mask,temporary})put(path);
        auto saved=root+"/saved.ScanTailor";put(saved);widget.context(root,saved,pages,finalPdf,{});widget.own(inputs,"sources");widget.own(work,"work");
        widget.clean("discard");
        check(QFileInfo::exists(input)&&QFileInfo::exists(mask)&&QFileInfo::exists(temporary),"saved project discard respects manual retention policy");
        widget.findChild<QComboBox*>("workflowCacheWhen")->setCurrentIndex(3);
        widget.clean("close");
        check(QFileInfo::exists(input)&&QFileInfo::exists(mask)&&!QFileInfo::exists(temporary),"close policy protects saved dependencies");
        widget.busy=[] {return true;};put(temporary);widget.clean();
        check(QFileInfo::exists(temporary),"active jobs block cache deletion");
    }
    static void run(MainWindow& window,QString const& folder,QString const& captures){
        layout(window,captures,"en");
        QStringList files;
        for(int i=0;i<2;++i){QImage image(800,1100,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);painter.setPen(Qt::black);painter.setFont(QFont("Arial",15));
            for(int line=0;line<32;++line)painter.drawText(80,95+line*27,"Printed book text 123. Test page.");
            painter.fillRect(0,300,20,65,Qt::black);painter.end();QString path=folder+QString("/workflow-input-%1.png").arg(i);check(image.save(path),"workflow fixture");files<<path;}
        std::cerr<<"workflow input"<<std::endl;
        window.importWorkflowImages(files);check(window.isProjectLoaded(),"input images open an editable project");
        std::cerr<<"workflow content"<<std::endl;
        window.runWorkflowStep("content");wait([&]{return !window.isBatchProcessingInProgress();});
        auto settings=window.m_ptrStages->selectContentFilter()->getSettings();
        auto samples=unified_content::collect(*window.m_ptrPages,*window.m_ptrStages);check(samples.size()==2,"normal content stage prepared both pages");
        auto sample=samples.front();auto params=settings->getPageParams(sample.page.id());check(bool(params),"content params available");
        auto originalStamp=window.workflowFingerprint();auto rect=sample.rect.adjusted(-3,-3,3,3);params->setContentBox(ContentBox(*sample.transform,rect));params->setContentSizePx(rect.size());params->setMode(MODE_MANUAL);
        settings->setPageParams(sample.page.id(),*params);window.m_ptrStages->pageLayoutFilter()->setContentBox(sample.page.id(),rect);window.invalidateThumbnail(sample.page.id());
        check(originalStamp!=window.workflowFingerprint(),"manual content edit invalidates downstream fingerprint");
        window.filterList->selectStep("ai_content");QApplication::processEvents();
        check(window.m_curFilter==3&&!window.m_contentReviewBar->isHidden(),"AI content uses same engine filter with its own controls");
        auto shared=settings->getPageParams(sample.page.id());check(shared->mode()==MODE_MANUAL,"AI screen retains normal manual edit");
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-ai-content-en.png"),"AI content screenshot");
        window.filterList->selectStep("content");check(window.m_contentReviewBar->isHidden(),"normal content hides AI proposal controls");
        std::cerr<<"workflow PDF"<<std::endl;
        window.runWorkflowStep("pdf");
        wait([&]{return !window.m_workflowPdf.isEmpty()&&!window.isBatchProcessingInProgress()&&window.m_workflowProcess&&window.m_workflowProcess->state()==QProcess::NotRunning;});
        check(QFileInfo::exists(window.m_workflowPdf),"PDF stage produces an actual PDF");
        check(window.workflowFingerprint()==window.m_workflowPdfStamp,"PDF fingerprint is current after rendering");
        auto firstPdf=window.m_workflowPdf;
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-pdf-en.png"),"PDF stage screenshot");
        std::cerr<<"workflow cleanup"<<std::endl;
        window.runWorkflowStep("cleanup");
        wait([&]{return !window.m_workflowReview.isEmpty()&&window.m_workflowProcess->state()==QProcess::NotRunning;});
        window.startWorkflowProcess("cleanup_apply");wait([&]{return window.m_workflowProcess->state()==QProcess::NotRunning;});
        check(window.m_workflowReceipt.value("applied").toBool(),"cleanup apply stores explicit approval");
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-cleanup-en.png"),"cleanup preview screenshot");
        window.runWorkflowStep("pdf");wait([&]{return window.m_workflowPdf!=firstPdf&&!window.isBatchProcessingInProgress()&&window.m_workflowProcess->state()==QProcess::NotRunning;});
        check(QFileInfo::exists(firstPdf),"refresh preserves previous PDF version");
        std::cerr<<"workflow OCR"<<std::endl;
        window.runWorkflowStep("ocr");
        wait([&]{return window.m_workflowProcess->state()==QProcess::NotRunning;});
        if(!window.m_workflowPdf.contains("-OCR.pdf"))std::cerr<<window.m_workflowStatus->text().toUtf8().constData()<<std::endl;
        check(window.m_workflowPdf.contains("-OCR.pdf"),"real Tesseract OCR runs as the final stage");
        auto pdfBeforeEdit=window.m_workflowPdf;
        auto updated=unified_content::collect(*window.m_ptrPages,*window.m_ptrStages).front();
        params=settings->getPageParams(sample.page.id());auto corrected=updated.rect.adjusted(-2,-2,2,2);params->setContentBox(ContentBox(*updated.transform,corrected));params->setContentSizePx(corrected.size());settings->setPageParams(sample.page.id(),*params);
        window.m_ptrStages->pageLayoutFilter()->setContentBox(sample.page.id(),corrected);
        window.runWorkflowStep("ocr");
        check(window.m_workflowPdf==pdfBeforeEdit&&window.m_workflowProcess->state()==QProcess::NotRunning,"OCR refuses stale upstream settings");
        std::cerr<<"workflow single page refresh"<<std::endl;
        window.runWorkflowStep("pdf");
        wait([&]{return window.m_workflowPdf!=pdfBeforeEdit&&window.m_workflowPdf.contains("-OCR.pdf")&&!window.isBatchProcessingInProgress()&&window.m_workflowProcess->state()==QProcess::NotRunning;});
        check(window.m_batchTotal==1,"single page edit renders only that native output");
        QFile report(window.m_workflowResult);check(report.open(QIODevice::ReadOnly),"incremental OCR receipt");
        auto incremental=QJsonDocument::fromJson(report.readAll()).object();
        check(incremental.value("changed_pages").toArray().size()==1&&incremental.value("reused_pages").toArray().size()==1,"only edited PDF page receives OCR");
        check(window.m_workflowTools->findChild<QLineEdit*>("workflowPdfOutputFolder")->text()==window.m_unified->workflowOutput(),"PDF stage visibly identifies its output folder");
        storage(folder,window.m_workflowPdf);
        window.filterList->selectStep("storage");window.syncWorkflowCache(true);QApplication::processEvents();
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-cache-en.png"),"cache stage screenshot");
        window.m_workflowCache->findChild<QCheckBox*>("workflowCacheKeep_native")->setChecked(false);
        window.runWorkflowStep("storage");
        auto rows=window.workflowPages();check(!QFileInfo::exists(rows[1].toObject().value("source").toString()),"intermediate TIFF removed after verified PDF");
        auto beforeCacheEdit=window.m_workflowPdf;
        updated=unified_content::collect(*window.m_ptrPages,*window.m_ptrStages).front();
        params=settings->getPageParams(sample.page.id());corrected=updated.rect.adjusted(-1,-1,1,1);params->setContentBox(ContentBox(*updated.transform,corrected));params->setContentSizePx(corrected.size());settings->setPageParams(sample.page.id(),*params);
        window.m_ptrStages->pageLayoutFilter()->setContentBox(sample.page.id(),corrected);
        std::cerr<<"workflow edit after cache removal"<<std::endl;
        window.runWorkflowStep("pdf");
        wait([&]{return window.m_workflowPdf!=beforeCacheEdit&&window.m_workflowPdf.contains("-OCR.pdf")&&!window.isBatchProcessingInProgress()&&window.m_workflowProcess->state()==QProcess::NotRunning;});
        check(window.m_batchTotal==1,"cache removal still allows one-page native refresh");
        check(QFileInfo::exists(beforeCacheEdit),"cache cleanup preserves previous final PDF");
        check(window.saveProjectWithFeedback(folder+"/workflow.ScanTailor"),"test project saved without dialogs");
        layout(window,captures,"en");
        auto font=window.font();font.setPointSize(12);window.setFont(font);window.filterList->setFont(font);QApplication::processEvents();
        window.filterList->selectStep("input");QApplication::processEvents();
        check(window.filterList->columnWidth(0)>=QFontMetrics(window.filterList->font()).horizontalAdvance(unified_workflow::steps().at(9).title),"large font stage names fit");
        if(!captures.isEmpty())check(window.grab().save(captures+"/workflow-large-font-en.png"),"large font screenshot");
        window.m_closing=true; // Test-owned project already saved; no modal close prompt.
    }
};
