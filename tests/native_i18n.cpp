// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"
#include "MainWindow.h"
#include "CommandLine.h"
#include "UnifiedI18n.h"
#include "UnifiedStageReviewBar.h"
#include "UnifiedOcrOptions.h"
#include "UnifiedPanel.h"
#include <QAction>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QFontDatabase>
#include <iostream>

static void check(bool ok,char const* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){
    Application app(argc,argv);app.setOrganizationName("Scan Tailor Tests");app.setApplicationName("Language test");
    QTemporaryDir temp;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    qputenv("STOU_ROOT",temp.path().toUtf8());qputenv("STOU_DATA",temp.path().toUtf8());qputenv("STOU_QUEUE_PATH",(temp.path()+"/queue.json").toUtf8());qunsetenv("STOU_MONITOR_REQUEST");qunsetenv("STOU_UI_LANGUAGE");
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
        const auto queueBytes=QJsonDocument(QJsonObject{{"schema",1},{"jobs",QJsonArray{queuedJob}}}).toJson();
        QFile queue(temp.path()+"/queue.json");check(queue.open(QIODevice::WriteOnly),"write synthetic queue");queue.write(queueBytes);queue.close();
        MainWindow window;window.resize(1280,900);window.show();
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
        window.close();app.removeTranslator(&english);
        check(unified_i18n::text("▶ Hazırla")==QString::fromUtf8("▶ Hazırla"),"Turkish text restored without translator");
        std::cout<<"PASS English/Turkish selection, persistence, separate play buttons, placeholders, filenames, OCR codes and queue controls\n";
        return 0;
    }catch(std::exception const& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
