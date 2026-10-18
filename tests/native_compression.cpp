// Real widgets -> JSON service -> verified PDFs, with isolated preferences.
#include "UnifiedWorkspaceDialog.h"
#include "UnifiedStartupDialog.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QCryptographicHash>
#include <QFontDatabase>
#include <iostream>
#include <stdexcept>

static void check(bool condition,char const* message){if(!condition)throw std::runtime_error(message);}
template<class Predicate> static void waitFor(Predicate done,int timeout=180000){
    QElapsedTimer timer;timer.start();
    while(!done()&&timer.elapsed()<timeout){QApplication::processEvents();QThread::msleep(15);}
    check(done(),"asynchronous workspace action timed out");QApplication::processEvents();
}
static QByteArray hash(QString const& path){QFile file(path);check(file.open(QIODevice::ReadOnly),"read PDF");QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);return hash.result();}

int main(int argc,char** argv){
    QApplication app(argc,argv);
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/arial.ttf"));
    app.setFont(QFont("Arial",9));
#endif
    try{
        check(argc==4,"usage: test source-root source-pdf isolated-output");
        QString root=QString::fromLocal8Bit(argv[1]),source=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);
        QDir().mkpath(output+"/data");qputenv("STOU_DATA",(output+"/data").toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,output+"/settings");
        app.setOrganizationName("ScanTailorQA060");app.setApplicationName("CompressionTest");
        unified_i18n::EnglishTranslator english;app.installTranslator(&english);
        auto originalHash=hash(source);
        UnifiedWorkspaceDialog workspace(root,output+"/queue.json",output,nullptr);workspace.setAttribute(Qt::WA_DeleteOnClose,false);workspace.show();
        auto status=workspace.findChild<QLabel*>("workspaceStatus");
        waitFor([&]{return status->text()=="Operation completed."||workspace.findChild<QComboBox*>("workspaceProfile")->count()>0;});
        std::cerr<<"PASS workspace startup\n";
        workspace.findChild<QTabWidget*>()->setCurrentIndex(2);
        auto profile=workspace.findChild<QComboBox*>("compressionProfile");check(profile&&profile->count()==5,"all five compression modes are selectable");
        profile->setCurrentIndex(profile->findData("target"));
        auto target=workspace.findChild<QDoubleSpinBox*>("compressionTargetMiB");check(target->isVisible(),"target size is visible in target mode");target->setValue(.001);
        workspace.findChild<QLineEdit*>("workspaceSourcePdf")->setText(source);
        auto create=workspace.findChild<QPushButton*>("workspaceCreateCompressedPdf");auto cancel=workspace.findChild<QPushButton*>("workspaceCancel");
        create->click();check(cancel->isEnabled(),"compression can be cancelled");
        waitFor([&]{return !cancel->isEnabled();});
        check(status->text().contains("could not be reached"),qPrintable(status->text()));
        auto outputs=QDir(output).entryList({"Compressed-*.pdf"},QDir::Files);check(outputs.size()==1,"one verified PDF was published");
        check(hash(source)==originalHash,"source PDF unchanged");
        std::cerr<<"PASS target compression\n";
        workspace.findChild<QPushButton*>("workspaceCompareCompression")->click();
        waitFor([&]{return !cancel->isEnabled();});
        auto comparisons=workspace.findChild<QTreeWidget*>("compressionComparisons");check(comparisons->topLevelItemCount()==3,qPrintable(status->text()));
        check(comparisons->topLevelItem(0)->text(0)=="Lossless","profile result is translated");
        comparisons->setCurrentItem(comparisons->topLevelItem(2));
        auto scroll=workspace.findChild<QTabWidget*>()->currentWidget();
        for(int pass=0;pass<4;++pass){QApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();}
        if(auto area=qobject_cast<QScrollArea*>(scroll))area->ensureWidgetVisible(comparisons);
        check(workspace.grab().save(output+"/compression-en.png"),"save compression screenshot");
        std::cerr<<"PASS profile previews\n";
        create->click();cancel->click();
        waitFor([&]{return status->text().contains("Compression cancelled");});
        check(QDir(output).entryList({"Compressed-*.pdf"},QDir::Files).size()==1,"cancel does not publish a partial PDF");
        check(QDir(output).entryList({".stou-compress-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty(),"cancel cleans private work directory");
        {
            UnifiedWorkspaceDialog closing(root,output+"/close-queue.json",output+"/close-test",nullptr);
            closing.setAttribute(Qt::WA_DeleteOnClose,false);
            auto ready=closing.findChild<QComboBox*>("workspaceProfile");waitFor([&]{return ready->count()>0;});
            closing.findChild<QLineEdit*>("workspaceSourcePdf")->setText(source);
            closing.findChild<QPushButton*>("workspaceCreateCompressedPdf")->click();
            check(closing.findChild<QPushButton*>("workspaceCancel")->isEnabled(),"close regression starts a live request");
        }
        QApplication::processEvents();std::cerr<<"PASS active process destruction\n";
        workspace.hide();app.removeTranslator(&english);
        UnifiedStartupDialog setup({{"setup_complete",true},{"compression_profile","target"},
                                   {"compression_options",QJsonObject{{"target_mib",18.5},{"min_dpi",180}}}},output);
        check(setup.values().value("compression_profile")=="target","startup retains custom compression mode");
        check(setup.values().value("compression_options").toObject().value("target_mib").toDouble()==18.5,"startup retains target size");
        setup.show();setup.next();QApplication::processEvents();
        check(setup.grab().save(output+"/compression-setup-tr.png"),"save Turkish startup screenshot");
        std::cout<<"PASS target result, preview, cancellation, source preservation and startup round trip\n";
        return 0;
    }catch(std::exception const& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
