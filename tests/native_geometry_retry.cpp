// A scoped maintenance runner: read a snapshot, emit verified proposals only.
// It never writes the source project or a PDF, or visits unrequested pages.
#include "UnifiedGeometryReview.h"
#include "ProjectReader.h"
#include "ProjectPages.h"
#include "StageSequence.h"
#include "PageSelectionAccessor.h"
#include "PageSelectionProvider.h"
#include "CommandLine.h"
#include "ImageLoader.h"
#include "TaskStatus.h"
#include "CachingFactory.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/Task.h"
#include "stages/select_content/Settings.h"
#include "stages/page_layout/Task.h"
#include "acceleration/NonAcceleratedOperations.h"
#include "imageproc/GrayImage.h"
#include "TiffMetadataLoader.h"
#include "FastImageMetadataLoader.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QDomDocument>
#include <iostream>
#include <set>
struct Status:TaskStatus{bool stopped=false;void cancel()override{stopped=true;}bool isCancelled()const override{return stopped;}void throwIfCancelled()const override{if(stopped)throw std::runtime_error("cancelled");}};
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setOrganizationName("Scan Tailor Tests");app.setApplicationName("geometry-retry");
    CommandLine::set(CommandLine(QStringList{},true));TiffMetadataLoader::registerMyself();FastImageMetadataLoader::registerMyself();
    if(argc!=2)return 2;
    try{
        QFile request(QString::fromLocal8Bit(argv[1]));if(!request.open(QIODevice::ReadOnly))return 3;
        auto spec=QJsonDocument::fromJson(request.readAll()).object();
        QFile file(spec.value("project").toString());QDomDocument doc;
        if(!file.open(QIODevice::ReadOnly)||!doc.setContent(&file))return 4;
        ProjectReader reader(doc,file.fileName());StageSequence stages(reader.pages(),PageSelectionAccessor(IntrusivePtr<PageSelectionProvider>{}));reader.readFilterSettings(stages.filters());
        std::set<int> selected;for(auto n:spec.value("pages").toArray())selected.insert(n.toInt());
        std::vector<unified_geometry::Sample> samples;std::vector<int> indices;
        for(auto const& sample:unified_geometry::collect(*reader.pages(),stages))if(selected.count(sample.ordinal)){
            if(sample.params.mode()==MODE_MANUAL)continue;
            indices.push_back(int(samples.size()));samples.push_back(sample);
        }
        QFile results(spec.value("results").toString());if(!results.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 5;
        auto ops=std::make_shared<NonAcceleratedOperations>();Status status;
        auto cancelled=[&]{return QFile::exists(spec.value("cancel_file").toString());};
        unified_geometry::run(samples,indices,spec.value("root").toString(),"cpu",false,QFileInfo(results).absolutePath(),cancelled,[&](auto result){
            auto const& sample=samples[result.index];
            QJsonObject row{{"page",sample.ordinal},{"changed",false},{"before",result.currentError},{"seconds",result.seconds},{"error",result.error},{"report",result.report}};
            if(result.recommended>=0){
                auto candidate=result.candidates[result.recommended];
                try{
                    auto params=candidate.params;params.getParamsDewarping().setMode(MODE_AUTO);
                    auto transform=unified_geometry::currentTransform(sample,params);
                    auto image=ImageLoader::load(sample.page.imageId());CachingFactory<imageproc::GrayImage> gray([&]{return imageproc::GrayImage(image);});
                    auto task=stages.selectContentFilter()->createTask(sample.page.id(),IntrusivePtr<page_layout::Task>{},true,false);
                    task->process(status,ops,image,gray,transform);
                    auto content=stages.selectContentFilter()->getSettings()->getPageParams(sample.page.id());
                    QDomDocument geometryXml,contentXml;geometryXml.appendChild(params.toXml(geometryXml,"params"));contentXml.appendChild(content->toXml(contentXml,"params"));
                    row.insert("changed",true);row.insert("after",candidate.error);row.insert("method",candidate.name);
                    row.insert("geometry",geometryXml.toString(-1));row.insert("content",contentXml.toString(-1));
                }catch(std::exception const& e){row.insert("error",QString::fromUtf8(e.what()));}
            }
            auto line=QJsonDocument(row).toJson(QJsonDocument::Compact)+"\n";
            if(results.write(line)!=line.size()||!results.flush())throw std::runtime_error("retry receipt could not be saved");
            std::cout<<sample.ordinal<<" "<<(row.value("changed").toBool()?"improved":"kept")<<std::endl;
        },false);
        return cancelled()?6:0;
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
