// Replays recorded, real book parameters without touching the source project.
#include "UnifiedGeometryReview.h"
#include "UnifiedReviewStore.h"
#include "XmlUnmarshaller.h"
#include "TiffMetadataLoader.h"
#include "FastImageMetadataLoader.h"
#include "ProjectReader.h"
#include "ProjectPages.h"
#include "StageSequence.h"
#include "PageSelectionAccessor.h"
#include "PageSelectionProvider.h"
#include "CommandLine.h"
#include <QApplication>
#include <QImage>
#include <iostream>
#include <set>
int main(int argc,char** argv){
    QApplication app(argc,argv);TiffMetadataLoader::registerMyself();FastImageMetadataLoader::registerMyself();
    CommandLine::set(CommandLine(QStringList{},true));
    if(argc<4)return 2;auto saved=unified_review::load(QString::fromLocal8Bit(argv[1]));
    QString output=QString::fromLocal8Bit(argv[2]),root=QString::fromLocal8Bit(argv[3]);QDir().mkpath(output);
    QMap<QString,QJsonObject> latest;for(auto value:saved.value("rows").toArray()){auto row=value.toObject();latest[row.value("key").toString()]=row;}
    std::vector<unified_geometry::Sample> samples;std::vector<int> ids;QJsonArray result;int ordinal=0;
    std::set<int> selected={9,31,44,74,100,152,170,190};
    auto requested=qEnvironmentVariable("STOU_PILOT_PAGES");if(!requested.isEmpty()){selected.clear();for(auto n:requested.split(','))selected.insert(n.toInt());}
    if(QString::fromLocal8Bit(argv[1]).endsWith(".ScanTailor",Qt::CaseInsensitive)) {
        QFile file(QString::fromLocal8Bit(argv[1]));QDomDocument doc;
        if(!file.open(QIODevice::ReadOnly)||!doc.setContent(&file))return 5;
        ProjectReader reader(doc,file.fileName());
        StageSequence stages(reader.pages(),PageSelectionAccessor(IntrusivePtr<PageSelectionProvider>{}));
        reader.readFilterSettings(stages.filters());
        for(auto const& sample:unified_geometry::collect(*reader.pages(),stages)) {
            if(!selected.count(sample.ordinal))continue;
            ids.push_back(int(samples.size()));samples.push_back(sample);
        }
    }
    for(auto it=latest.begin();it!=latest.end();++it){++ordinal;if(!selected.count(ordinal))continue;
        auto row=it.value();auto key=it.key().split('|');if(key.size()!=3)return 3;QImage image(key[0]);if(image.isNull())return 4;
        auto element=unified_review::element(row.value("params").toString());deskew::Params params(element);
        auto dependencies=element.firstChildElement("dependencies");OrthogonalRotation rotation(dependencies.firstChildElement("rotation"));
        imageproc::AffineImageTransform transform(image.size());transform.rotate(rotation.toDegrees());transform.setOrigCropArea(XmlUnmarshaller::polygonF(dependencies.firstChildElement("page-outline")));
        auto side=key[2]=="left"?PageId::LEFT_PAGE:key[2]=="right"?PageId::RIGHT_PAGE:PageId::SINGLE_PAGE;
        PageInfo page(PageId(ImageId(key[0],key[1].toInt()),side),ImageMetadata(image.size()),2,false,false);
        ids.push_back(int(samples.size()));samples.push_back({page,ordinal,params,transform,unified_geometry::state(params)});
    }
    unified_geometry::run(samples,ids,root,"cpu",qEnvironmentVariable("STOU_PILOT_AI")!="0",output,[]{return false;},[&](auto row){
        QJsonArray candidates;for(auto const& candidate:row.candidates)candidates.append(QJsonObject{{"method",candidate.name},{"accepted",candidate.accepted},{"error",candidate.error},{"reason",candidate.reason},{"path",candidate.path},{"params",unified_geometry::state(candidate.params)}});
        auto const& sample=samples[row.index];auto old=latest.value(unified_review::key(sample.page));
        result.append(QJsonObject{{"page",sample.ordinal},{"key",unified_review::key(sample.page)},{"baseline",old.value("currentError")},{"current",row.currentError},{"recommended",row.recommended},{"seconds",row.seconds},{"error",row.error},{"report",row.report},{"before",row.current},{"candidates",candidates}});
        std::cout<<"page "<<sample.ordinal<<" before="<<row.currentError<<" recommended="<<row.recommended<<" seconds="<<row.seconds<<std::endl;
        unified_review::save(output+"/pilot.json",{{"pages",result}});
    });return 0;
}
