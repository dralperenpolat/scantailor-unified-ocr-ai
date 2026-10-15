// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"
#include "MainWindow.h"
#include "CommandLine.h"
#include "UnifiedI18n.h"
#include "UnifiedStageReviewBar.h"
#include "UnifiedOcrOptions.h"
#include "UnifiedPanel.h"
#include "UnifiedPdfToolsDialog.h"
#include "UnifiedStartupDialog.h"
#include <QAction>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QFontDatabase>
#include <QTabWidget>
#include <QScrollArea>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

static void check(bool ok,char const* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){
    Application app(argc,argv);app.setOrganizationName("Scan Tailor Tests");app.setApplicationName("Language test");
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/arial.ttf"));
    app.setFont(QFont("Arial",9));
#endif
    QTemporaryDir temp;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    qputenv("STOU_ROOT",argc>2?QByteArray(argv[2]):temp.path().toUtf8());qputenv("STOU_DATA",temp.path().toUtf8());qputenv("STOU_QUEUE_PATH",(temp.path()+"/queue.json").toUtf8());qunsetenv("STOU_MONITOR_REQUEST");qunsetenv("STOU_UI_LANGUAGE");
    CommandLine::set(CommandLine(QStringList{},true));
    if(argc>1)QDir().mkpath(QString::fromLocal8Bit(argv[1]));
    try {
        QSettings().setValue("unified/ocr_languages","deu+eng");QSettings().setValue("unified/paddle_language","de");
        QSettings().setValue("unified/ui_language","tr");check(unified_i18n::selectedLanguage()=="tr","saved Turkish selection");
        {
            UnifiedStageReviewBar bar(true,nullptr);
            check(bar.findChild<QPushButton*>("stageReviewAnalyze")->text()==QString::fromUtf8("▶ Hazırla"),"Turkish prepare button");
            check(bar.findChild<QPushButton*>("stageReviewApply")->text()==QString::fromUtf8("▶ Uygula"),"Turkish apply button");
        }
        QSettings().setValue("unified/ui_language","en");QSettings().sync();check(unified_i18n::selectedLanguage()=="en","English selection survives settings reload");
        unified_i18n::EnglishTranslator english;check(!english.isEmpty(),"embedded translation catalog exists");check(app.installTranslator(&english),"install English translator");app.setProperty("unifiedUiLanguage","en");
        check(unified_i18n::text("▶ Hazırla")=="▶ Prepare","English prepare translation");
        {
            UnifiedStartupDialog setup({},temp.path()+"/output");setup.show();app.processEvents();
            auto values=setup.values();
            check(values.value("ocr_engine")=="tesseract"&&values.value("tess_quality")=="fast"&&values.value("ocr_timing")=="after","CPU fast defaults to Tesseract Fast after images");
            check(!values.value("auto_warp").toBool()&&!values.value("page_cleanup").toBool()&&values.value("native_scale")==1,"fast profile avoids expensive automatic processing");
            check(setup.windowTitle()=="Startup settings","English startup title");
            check(setup.button(QWizard::NextButton)->isVisible()&&setup.button(QWizard::NextButton)->isEnabled(),"startup Next button is visible and usable");
            check(setup.button(QWizard::NextButton)->mapTo(&setup,QPoint(0,0)).y()+setup.button(QWizard::NextButton)->height()<=setup.height(),"startup Next button fits the window");
            if(argc>1)check(setup.grab().save(QString::fromLocal8Bit(argv[1])+"/startup-en-ocr.png"),"startup OCR screenshot");
            setup.findChild<QComboBox*>("setupProfile")->setCurrentIndex(1);values=setup.values();
            check(values.value("tess_quality")=="best"&&values.value("auto_warp").toBool(),"quality profile changes OCR and geometry together");
            setup.next();setup.next();app.processEvents();
            check(setup.button(QWizard::FinishButton)->isVisible(),"startup Save button is visible");
            auto output=setup.findChild<QLineEdit*>("setupOutputFolder"),temporary=setup.findChild<QLineEdit*>("setupTempFolder");
            check(output&&temporary,"startup exposes output and temporary folders");
            output->setText(temp.path()+"/chosen-output");temporary->setText(temp.path()+"/chosen-temp");
            check(setup.validateCurrentPage(),"selected startup folders are writable");
            values=setup.values();check(values.value("preferred_output")==temp.path()+"/chosen-output","chosen output is saved in preferences");
            if(argc>1)check(setup.grab().save(QString::fromLocal8Bit(argv[1])+"/startup-en-folders.png"),"startup folders screenshot");
            values.insert("structured_text",false);
            UnifiedStartupDialog restored(values,values.value("preferred_output").toString());
            check(restored.values().value("tess_quality")=="best"&&!restored.values().value("structured_text").toBool(),"custom preferences survive reopening");
            check(restored.values().value("preferred_output")==values.value("preferred_output"),"output folder survives reopening");
        }
        check(unified_i18n::display(QString::fromUtf8("12/50 · 1450 OCR karakteri · kontrol edin"))=="12/50 · 1450 OCR characters · review required","formatted live worker message");
        check(unified_i18n::display(QString::fromUtf8("Page %2 — Türkçe.pdf"))==QString::fromUtf8("Page %2 — Türkçe.pdf"),"unknown filenames are unchanged");
        check(english.translate("OtherContext","Görünüm").isNull(),"native translation contexts are not intercepted");
        check(unified_i18n::display(QString::fromUtf8("Geçersiz sayfa boyutu: Türkçe %2.pdf"))==QString::fromUtf8("Invalid page size: Türkçe %2.pdf"),"captured filename placeholders are not reinterpreted");
        UnifiedStageReviewBar bar(true,nullptr);bar.resize(310,780);bar.show();
        auto prepare=bar.findChild<QPushButton*>("stageReviewAnalyze"),apply=bar.findChild<QPushButton*>("stageReviewApply");
        check(prepare->text()=="▶ Prepare"&&apply->text()=="▶ Apply","separate English play controls");
        check(bar.findChild<QComboBox*>("stageReviewMethod")->currentData()=="geometry","method identity is language-independent");
        auto scope=bar.findChild<QComboBox*>("stageReviewScope");scope->setCurrentIndex(2);check(scope->currentData()=="book"&&apply->text()=="▶ Batch","batch scope identity and label");
        scope->setCurrentIndex(0);
        UnifiedStageReviewState state;state.page="example.png";state.selectedMethod="UVDoc";state.choices<<"UVDoc";state.choice=0;state.available=true;bar.showState(state);
        check(bar.findChild<QLabel*>("stageReviewSelected")->text()=="Selected: UVDoc","selected candidate label");
        check(bar.findChild<QLabel*>("stageReviewApplied")->text()=="Applied to this page: not applied yet","applied candidate remains distinct");
        QApplication::processEvents();check(prepare->geometry().right()<=prepare->parentWidget()->width(),"prepare button fits panel");
        check(apply->geometry().right()<=apply->parentWidget()->width(),"apply button fits panel");
        if(argc>1)check(bar.grab().save(QString::fromLocal8Bit(argv[1])+"/language-en-stage.png"),"English stage screenshot");
        UnifiedOcrOptions ocr(temp.path());auto opts=ocr.options();check(opts.value("language")=="deu+eng"&&opts.value("paddle_language")=="de","interface language preserves OCR language codes");
        const QJsonObject queuedJob{{"id","language-test"},{"title",QString::fromUtf8("Türkçe %2 Kitap")},{"status",QString::fromUtf8("Sayfa hazır")},{"detail",QString::fromUtf8("12/50 · 1450 OCR karakteri · kontrol edin")}};
        const QString outputRoot=temp.path()+"/selected-output-drive";
        const auto queueBytes=QJsonDocument(QJsonObject{{"schema",1},{"output_root",outputRoot},{"jobs",QJsonArray{queuedJob}}}).toJson();
        QFile queue(temp.path()+"/queue.json");check(queue.open(QIODevice::WriteOnly),"write synthetic queue");queue.write(queueBytes);queue.close();
        MainWindow window;window.resize(1280,900);window.show();
        check(window.windowTitle().contains("0.5.0"),"window reports the current application version");
        auto languageEn=window.findChild<QAction*>("unifiedLanguage_en"),languageTr=window.findChild<QAction*>("unifiedLanguage_tr");
        check(languageEn&&languageTr&&languageEn->isChecked(),"English menu choice checked");
        check(languageTr->text()==QString::fromUtf8("Türkçe"),"Turkish endonym stays recognizable");
        languageTr->trigger();check(QSettings().value("unified/ui_language")=="tr","menu persists Turkish selection");
        check(app.property("unifiedUiLanguage")=="en","language change defers until reopening");
        check(QSettings().value("unified/ocr_languages")=="deu+eng","language menu does not alter OCR settings");
        languageEn->trigger();
        auto panel=window.findChild<UnifiedPanel*>();check(panel,"queue panel exists");
        panel->showToolsTab(0);panel->parentWidget()->show();QApplication::processEvents();
        check(panel->findChild<QPushButton*>("unifiedCaptureProject")->text()=="Add open project to queue","English queue controls");
        auto books=panel->findChild<QTreeWidget*>("unifiedBooks");check(books&&books->topLevelItemCount()==1,"synthetic queue loaded");
        const auto row=books->topLevelItem(0);
        check(row->text(0)==QString::fromUtf8("Türkçe %2 Kitap"),"user book title is preserved");
        check(row->text(1)=="Page ready"&&row->text(2)=="12/50 · 1450 OCR characters · review required","queue messages translated at display boundary");
        check(queue.open(QIODevice::ReadOnly)&&queue.readAll()==queueBytes,"language selection leaves queue bytes unchanged");queue.close();
        if(argc>1)check(window.grab().save(QString::fromLocal8Bit(argv[1])+"/language-en-window.png"),"English window screenshot");
        panel->openPdfTools();QApplication::processEvents();
        auto tabs=panel->findChild<QTabWidget*>("unifiedToolTabs");
        check(tabs->tabText(tabs->currentIndex())=="Import PDF","dedicated English PDF import tab");
        auto pdf=static_cast<UnifiedPdfToolsDialog*>(panel->findChild<QWidget*>("unifiedPdfTools"));check(pdf,"embedded PDF tools exist");
        check(!pdf->isWindow(),"PDF tools stay in the main window");
        check(pdf->findChild<QLineEdit*>("pdfToolsDestination")->text()==outputRoot+"/PDFImports","PDF preparation follows the selected output drive");
        check(pdf->findChild<QComboBox*>("pdfImportStrategy")->currentData()=="auto","native image extraction is the default");
        check(pdf->findChild<QComboBox*>("pdfImportSplit")->currentData()=="1","imported PDF pages are not split twice by default");
        check(window.findChild<QAction*>("unifiedImportPdfAction"),"PDF entry in menu");
        panel->setPanelHeight(650);QApplication::processEvents();
        if(argc>1)check(window.grab().save(QString::fromLocal8Bit(argv[1])+"/pdf-import-en.png"),"English PDF tab screenshot");
        if(argc>3){
            const QString source=QString::fromLocal8Bit(argv[3]);
            CommandLine cli(QStringList{"test",source},true);check(cli.projectFile()==source,"PDF command line routes to importer");
            const auto title=window.windowTitle();
            window.openProject(source); // same entry used by File/Open and startup
            auto start=pdf->findChild<QPushButton*>("pdfToolsStart");
            check(pdf->running()&&!start->isEnabled(),"opening PDF automatically prepares its images");
            check(!pdf->selectSource(source),"second import cannot overwrite active import state");
            check(!pdf->findChild<QLineEdit*>("pdfToolsSource")->isEnabled(),"active source cannot change");
            QElapsedTimer elapsed;elapsed.start();
            while(!start->isEnabled()&&elapsed.elapsed()<60000){app.processEvents();QThread::msleep(20);}
            check(start->isEnabled(),"PDF process finished");
            if(books->topLevelItemCount()!=2)std::cerr<<pdf->findChild<QLabel*>("pdfToolsStatus")->text().toUtf8().constData()<<'\n';
            check(books->topLevelItemCount()==2,"prepared PDF added as one book");
            check(window.windowTitle()==title,"PDF import preserves editor project");
            check(queue.open(QIODevice::ReadOnly),"read imported queue");auto imported=QJsonDocument::fromJson(queue.readAll()).object().value("jobs").toArray();queue.close();
            check(imported.at(0).toObject()==queuedJob,"existing queue job preserved");
            const auto job=imported.at(1).toObject();
            check(job.value("split")=="1"&&QFileInfo(job.value("source_pdf").toString()).canonicalFilePath()==QFileInfo(source).canonicalFilePath(),"source PDF and layout retained");
            check(job.value("pdf_import").toObject().value("pages").toInt()==2,"two PDF pages imported in order");
            check(QFileInfo::exists(job.value("folder").toString()+"/page-00002.png"),"last imported page exists");
            if(argc>1)check(window.grab().save(QString::fromLocal8Bit(argv[1])+"/pdf-import-complete-en.png"),"completed PDF tab screenshot");
            // Retrying an existing import must preserve its prior layout choice.
            pdf->findChild<QComboBox*>("pdfImportSplit")->setCurrentIndex(1);window.openProject(source);
            elapsed.restart();while(!start->isEnabled()&&elapsed.elapsed()<60000){app.processEvents();QThread::msleep(20);}
            check(queue.open(QIODevice::ReadOnly),"read repeated import");imported=QJsonDocument::fromJson(queue.readAll()).object().value("jobs").toArray();queue.close();
            check(imported.size()==2&&imported.at(1).toObject()==job,"duplicate import leaves edited job untouched");
            // A busy queue can reject insertion without losing prepared images.
            bool accept=false,added=false;UnifiedPdfToolsDialog deferred(QString::fromLocal8Bit(argv[2]),nullptr);
            deferred.importReady=[&](QJsonObject const& r){added=accept&&r.value("split")=="2";return added;};
            deferred.findChild<QComboBox*>("pdfImportSplit")->setCurrentIndex(1);
            deferred.selectSource(source);elapsed.restart();
            while(deferred.running()&&elapsed.elapsed()<60000){app.processEvents();QThread::msleep(20);}
            app.processEvents();auto add=deferred.findChild<QPushButton*>("pdfImportAddReady");
            check(!added&&!add->isHidden(),"prepared import retained when queue is busy");accept=true;add->click();
            check(added&&add->isHidden(),"retry queues prepared pages with selected two-page layout");
        }
        panel->findChild<QPushButton*>("unifiedStartupButton")->click();app.processEvents();
        auto startup=static_cast<UnifiedStartupDialog*>(panel->findChild<QWidget*>("unifiedStartup"));check(startup,"startup settings can be reopened from the panel");
        startup->next();startup->next();startup->findChild<QLineEdit*>("setupOutputFolder")->setText(temp.path()+"/saved-output");
        startup->button(QWizard::FinishButton)->click();app.processEvents();
        QFile preferences(temp.path()+"/preferences.json");check(preferences.open(QIODevice::ReadOnly),"startup preferences were saved");
        check(QJsonDocument::fromJson(preferences.readAll()).object().value("preferred_output")==temp.path()+"/saved-output","selected output is persisted");preferences.close();
        check(queue.open(QIODevice::ReadOnly),"read selected output from queue");
        check(QJsonDocument::fromJson(queue.readAll()).object().value("output_root")==temp.path()+"/saved-output","queue uses selected startup output");queue.close();
        panel->findChild<QPushButton*>("unifiedWorkspaceButton")->click();app.processEvents();
        auto workspace=panel->findChild<QDialog*>("unifiedWorkspace");check(workspace,"workspace opens");
        auto preset=workspace->findChild<QComboBox*>("workspaceProfile");QElapsedTimer service;service.start();
        while(preset->count()==0&&service.elapsed()<30000){app.processEvents();QThread::msleep(20);}
        check(preset->findData("cpu_fast")>=0,"workspace service loads CPU Fast profile");
        if(argc>1)check(workspace->grab().save(QString::fromLocal8Bit(argv[1])+"/workspace-en.png"),"English workspace screenshot");workspace->close();
        window.close();app.removeTranslator(&english);
        check(unified_i18n::text("▶ Hazırla")==QString::fromUtf8("▶ Hazırla"),"Turkish text restored without translator");
        {
            UnifiedStartupDialog setup({},temp.path()+"/output");setup.show();setup.next();setup.next();app.processEvents();
            if(argc>1)check(setup.grab().save(QString::fromLocal8Bit(argv[1])+"/startup-tr-folders.png"),"Turkish startup folders screenshot");
            UnifiedPanel turkish;turkish.resize(1100,650);turkish.show();turkish.openPdfTools();app.processEvents();
            auto trTabs=turkish.findChild<QTabWidget*>("unifiedToolTabs");
            check(trTabs->tabText(trTabs->currentIndex())==QString::fromUtf8("PDF’den içe aktar"),"Turkish PDF tab");
            if(argc>1)check(turkish.grab().save(QString::fromLocal8Bit(argv[1])+"/pdf-import-tr.png"),"Turkish PDF tab screenshot");
        }
        std::cout<<"PASS English/Turkish selection, persistence, separate play buttons, placeholders, filenames, OCR codes and queue controls\n";
        return 0;
    }catch(std::exception const& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
