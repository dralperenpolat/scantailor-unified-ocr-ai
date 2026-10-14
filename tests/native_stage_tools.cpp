// Exercise the real stage controls, queue binding and preview tab routing.
#include "UnifiedStageTools.h"
#include "UnifiedPdfToolsDialog.h"
#include "UnifiedStageReviewBar.h"
#include "stages/page_layout/Filter.h"
#include "stages/page_layout/OptionsWidget.h"
#include "stages/deskew/Filter.h"
#include "stages/deskew/OptionsWidget.h"
#include "ProjectPages.h"
#include "stages/output/Filter.h"
#include "stages/output/TabbedImageView.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/OptionsWidget.h"
#include "PageSelectionProvider.h"
#include "CommandLine.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QFontDatabase>
#include <QScrollArea>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>

void check(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
template<typename T> T* child(QObject* parent,char const* name){auto result=parent->findChild<T*>(name);check(result,name);return result;}

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    QTemporaryDir root;check(root.isValid(),"temporary directory");
    app.setOrganizationName("Scan Tailor Tests");app.setApplicationName("Stage tools test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path());
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/arial.ttf"));
    app.setFont(QFont("Arial",9));
    CommandLine::set(CommandLine(QStringList{},true));
    qputenv("STOU_ROOT",root.path().toUtf8());
    qputenv("STOU_QUEUE_PATH",(root.path()+"/queue.json").toUtf8());
    qunsetenv("STOU_MONITOR_REQUEST");
    QFile queue(root.path()+"/queue.json");check(queue.open(QIODevice::WriteOnly),"queue fixture");
    QByteArray original("{\"schema\":1,\"jobs\":[]}");queue.write(original);queue.close();
    try {
        UnifiedPanel panel;
        PageSelectionAccessor accessor(IntrusivePtr<PageSelectionProvider>{});
        IntrusivePtr<output::Filter> outputFilter(new output::Filter(accessor));
        auto options=outputFilter->optionsWidget();
        panel.setAppearance("soft_local");panel.setCleanupEnabled(false);
        bindUnifiedOutputTools(&panel,options);
        auto timing=child<QComboBox>(options,"stageOcrTiming");timing->setCurrentIndex(timing->findData("after"));check(panel.ocrTiming()=="after","Output OCR timing updates worker setting");panel.setOcrTiming("none");check(timing->currentData()=="none","OCR timing synchronized both ways");panel.setOcrTiming("during");
        child<QPushButton>(options,"stagePdfCompact")->click();auto tools=child<QWidget>(&panel,"unifiedPdfTools");check(child<QComboBox>(tools,"pdfToolsMode")->currentData()=="compact","Output opens lossless compression directly");panel.showToolsTab(0);
        auto profile=child<QComboBox>(options,"stagePdfAppearance");
        auto cleanup=child<QCheckBox>(options,"stagePdfCleanup");
        auto preview=child<QPushButton>(options,"stagePdfPreview");
        auto manual=child<QPushButton>(options,"stageManualCleanup");
        check(profile->currentData()=="soft_local"&&!cleanup->isChecked(),"initial queue values seed stage controls");
        int changes=0;
        QObject::connect(&panel,&UnifiedPanel::pdfOutputSettingsChanged,[&]{++changes;});
        profile->setCurrentIndex(profile->findData("gray"));
        check(panel.appearance()=="gray"&&changes==1,"stage changes queue once, without feedback loop");
        panel.setAppearance("soft");
        check(profile->currentData()=="soft"&&changes==2,"queue changes stage");
        cleanup->setChecked(true);
        check(panel.cleanupEnabled()&&changes==3,"stage cleanup changes queue");
        panel.setCleanupEnabled(false);
        check(!cleanup->isChecked()&&changes==4,"queue cleanup changes stage");
        panel.setAppearance("native");
        check(!cleanup->isEnabled()&&!cleanup->isChecked(),"native output cannot claim automatic PDF cleanup");
        panel.setAppearance("gray");
        check(cleanup->isEnabled()&&!cleanup->isChecked(),"switching appearance retains cleanup preference");
        panel.setAppearance("not-a-mode");
        check(panel.appearance()=="gray","unknown appearance ignored");
        std::cout<<"PASS stage/queue appearance and cleanup synchronization\n";

        PageId page(ImageId("page.png"),PageId::SINGLE_PAGE);
        options->preUpdateUI(page);
        check(!preview->isEnabled()&&!manual->isEnabled(),"pending page has no stale preview or editor action");
        output::TabbedImageView tabs;
        for(auto tab:{output::TAB_OUTPUT,output::TAB_SOFT,output::TAB_LOCAL_SOFT,output::TAB_OCR_GRAY,output::TAB_FILL_ZONES})
            tabs.addTab(new QWidget,QString::number(int(tab)),tab);
        QObject::connect(options,&output::OptionsWidget::outputTabRequested,&tabs,&output::TabbedImageView::setCurrentTab);
        QObject::connect(&tabs,&output::TabbedImageView::tabChanged,options,&output::OptionsWidget::tabChanged);
        options->postUpdateUI(QSize(1200,1800));
        int index=0;
        for(auto mode:{"native","soft","soft_local","gray"}) {
            profile->setCurrentIndex(profile->findData(mode));preview->click();
            check(tabs.currentIndex()==index++,"each profile opens its real native preview tab");
        }
        manual->click();check(tabs.currentIndex()==4&&options->lastTab()==output::TAB_FILL_ZONES,"manual cleanup opens and remembers fill zones");
        options->postUpdateUI(QSize(1200,1800),false);
        check(preview->isEnabled()&&!manual->isEnabled(),"batch preview stays available, manual edits wait");
        tabs.setCurrentTab(output::TAB_OUTPUT);tabs.setTabEnabled(4,false);
        tabs.setCurrentTab(output::TAB_FILL_ZONES);
        check(tabs.currentIndex()==0,"direct requests cannot enter batch-disabled zone editor");
        options->postUpdateUI(QSize(1200,1800));
        std::cout<<"PASS preview selection, pending-page protection and batch edit guard\n";

        IntrusivePtr<select_content::Filter> contentFilter(new select_content::Filter(accessor));
        int aiOpens=0,bookOpens=0;
        contentFilter->onAiReview(&app,[&]{++aiOpens;});
        contentFilter->onBookReview(&app,[&]{++bookOpens;});
        auto content=contentFilter->optionsWidget();
        child<QPushButton>(content,"aiContentReview")->click();
        child<QPushButton>(content,"bookContentReview")->click();
        check(aiOpens==1&&bookOpens==1,"AI and book review remain separate working actions");
        check(content->findChild<QPushButton*>("applyToBtn")&&options->findChild<QComboBox*>("thresholdMethodSelector"),"existing native controls preserved");
        IntrusivePtr<page_layout::Filter> layoutFilter(new page_layout::Filter(IntrusivePtr<ProjectPages>(),accessor));
        bindUnifiedLayoutTools(&panel,layoutFilter->optionsWidget());
        child<QComboBox>(layoutFilter->optionsWidget(),"stagePdfPaper")->setCurrentIndex(2);
        child<QDoubleSpinBox>(layoutFilter->optionsWidget(),"stagePdfMargin")->setValue(7);
        check(panel.pdfLayoutOptions().value("paper")=="a4_fill"&&panel.pdfLayoutOptions().value("margin_mm").toDouble()==7,"stage 5 layout updates queue");
        panel.setPdfLayoutOptions({{"paper","a4_text"},{"margin_mm",12.},{"content_fit",false},{"target_x_mm",2.}});
        check(child<QComboBox>(layoutFilter->optionsWidget(),"stagePdfPaper")->currentData()=="a4_text"&&!child<QCheckBox>(layoutFilter->optionsWidget(),"stagePdfContentFit")->isChecked(),"queue updates stage 5 layout");
        IntrusivePtr<deskew::Filter> geometryFilter(new deskew::Filter(accessor));int geometryOpens=0;geometryFilter->onGeometryReview(&app,[&]{++geometryOpens;});
        child<QPushButton>(geometryFilter->optionsWidget(),"geometryReviewButton")->click();check(geometryOpens==1,"stage 3 opens geometry review");
        QString captures=argc>1?QString::fromLocal8Bit(argv[1]):root.path();QDir().mkpath(captures);
        for(int font:{9,12}) {
            app.setFont(QFont("Arial",font));
            QScrollArea scroll;scroll.setWidgetResizable(true);
            scroll.resize(font==9?340:430,780);scroll.setWidget(options);scroll.show();app.processEvents();
            auto group=child<QGroupBox>(options,"unifiedOutputTools");
            auto controls=group->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly);
            for(int i=0;i<controls.size();++i) {
                check(group->rect().contains(controls[i]->geometry()),"stage control stays inside its group");
                for(int j=0;j<i;++j)check(!controls[i]->geometry().intersects(controls[j]->geometry()),"stage controls do not overlap");
            }
            check(scroll.grab().save(captures+QString("/output-tools-%1.png").arg(font)),"output stage capture");
            scroll.takeWidget();scroll.setWidget(content);app.processEvents();
            check(scroll.grab().save(captures+QString("/content-tools-%1.png").arg(font)),"content stage capture");
            scroll.takeWidget();options->setParent(nullptr);content->setParent(nullptr);
            auto bar=new UnifiedStageReviewBar(true,nullptr);UnifiedStageReviewState state;state.page="Test page 2";state.detail=QString::fromUtf8("Öneri hazır. Önce görüntüyü incele.");state.choices=QStringList{"AI", "Metin", "Kenar"};state.choice=0;state.available=state.bulkAvailable=true;bar->showState(state);
            int actions=0;bar->action=[&](auto){++actions;};bar->trigger(UnifiedStageReviewBar::Apply);check(actions==1,"stage primary action");
            scroll.setWidget(bar);app.processEvents();check(child<QComboBox>(bar,"stageReviewScope")->isVisible(),"bulk scope is visible without opening advanced settings");
            child<QComboBox>(bar,"stageReviewScope")->setCurrentIndex(2);bar->showState(state);check(child<QPushButton>(bar,"stageReviewApply")->text().contains(QString::fromUtf8("Toplu")),"bulk button describes scope");
            auto fields=bar->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly);for(int i=0;i<fields.size();++i){if(fields[i]->isHidden())continue;for(int j=0;j<i;++j)if(!fields[j]->isHidden())check(!fields[i]->geometry().intersects(fields[j]->geometry()),"inline stage fields do not overlap");}
            check(scroll.grab().save(captures+QString("/inline-review-%1.png").arg(font)),"inline stage capture");scroll.takeWidget();delete bar;
        }
        // Reopening projects creates fresh widgets while keeping queue options.
        outputFilter.reset();
        panel.setAppearance("soft_local");panel.setCleanupEnabled(true);
        outputFilter.reset(new output::Filter(accessor));
        bindUnifiedOutputTools(&panel,outputFilter->optionsWidget());
        check(child<QComboBox>(outputFilter->optionsWidget(),"stagePdfAppearance")->currentData()=="soft_local","project switch restores shared PDF appearance");
        check(queue.open(QIODevice::ReadOnly)&&queue.readAll()==original,"stage settings never overwrite active queue");
        if(argc>3){UnifiedPdfToolsDialog pdf(QString::fromLocal8Bit(argv[2]),nullptr);bool ready=false;pdf.importReady=[&](QJsonObject const& result){ready=QFileInfo::exists(result.value("folder").toString()+"/page-00001.png");return ready;};child<QLineEdit>(&pdf,"pdfToolsSource")->setText(QString::fromLocal8Bit(argv[3]));child<QLineEdit>(&pdf,"pdfToolsDestination")->setText(root.path()+"/pdf-import");auto start=child<QPushButton>(&pdf,"pdfToolsStart");pdf.show();start->click();QElapsedTimer t;t.start();while(!start->isEnabled()&&t.elapsed()<45000){app.processEvents();QThread::msleep(20);}check(ready&&start->isEnabled(),"PDF import real process updates Qt and returns ready folder");check(pdf.grab().save(captures+"/pdf-tools.png"),"PDF tools capture");std::cout<<"PASS actual PDF import UI process and completion\n";}
        std::cout<<"PASS AI entry, native controls, narrow layouts, project switching and unchanged queue\n";
        return 0;
    }catch(std::exception const& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
