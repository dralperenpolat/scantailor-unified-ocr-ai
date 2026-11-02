// Scoped experiment: exchange reviewed content rectangles with an external reviewer.
// Source projects are read-only. Geometry and existing manual content are preserved.
#include "UnifiedContentReview.h"
#include "ProjectReader.h"
#include "ProjectPages.h"
#include "PageSequence.h"
#include "ProjectWriter.h"
#include "SelectedPage.h"
#include "StageSequence.h"
#include "PageSelectionAccessor.h"
#include "PageSelectionProvider.h"
#include "CommandLine.h"
#include "ImageLoader.h"
#include "OutputFileNameGenerator.h"
#include "TiffMetadataLoader.h"
#include "FastImageMetadataLoader.h"
#include "ContentBox.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/Settings.h"
#include "stages/page_layout/Filter.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QApplication>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QElapsedTimer>
#include <QImageReader>
#include <QSet>
#include <cmath>
#include <iostream>

namespace {
QByteArray read(QString const& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read input");return f.readAll();}
QString hash(QByteArray const& data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
void saveNew(QString const& path,QByteArray const& data){
    if(QFileInfo::exists(path))throw std::runtime_error("Refusing to overwrite an existing output");
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()||!f.commit())throw std::runtime_error("Cannot save output");
}
QJsonArray rect(QRectF const& r){return {r.left(),r.top(),r.right(),r.bottom()};}
QRectF parseRect(QJsonValue const& v){
    auto a=v.toArray();if(a.size()!=4)throw std::runtime_error("Rectangle must have four coordinates");
    for(auto n:a)if(!n.isDouble()||!std::isfinite(n.toDouble()))throw std::runtime_error("Non-finite rectangle");
    QRectF r(QPointF(a[0].toDouble(),a[1].toDouble()),QPointF(a[2].toDouble(),a[3].toDouble()));
    if(r.isEmpty())throw std::runtime_error("Empty or reversed rectangle");
    return r;
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setOrganizationName("Scan Tailor Tests");app.setApplicationName("content-exchange");
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QImageReader::setAllocationLimit(0);
#endif
    CommandLine::set(CommandLine(QStringList{},true));TiffMetadataLoader::registerMyself();FastImageMetadataLoader::registerMyself();
    if(argc!=2)return 2;
    try{
        QElapsedTimer timer;timer.start();
        auto spec=QJsonDocument::fromJson(read(QString::fromLocal8Bit(argv[1]))).object();
        QString project=spec.value("project").toString();auto projectData=read(project);QDomDocument xml;
        if(!xml.setContent(projectData))throw std::runtime_error("Invalid project XML");
        ProjectReader reader(xml,project);
        StageSequence stages(reader.pages(),PageSelectionAccessor(IntrusivePtr<PageSelectionProvider>{}));reader.readFilterSettings(stages.filters());
        auto samples=unified_content::collect(*reader.pages(),stages);
        if(samples.size()!=reader.pages()->toPageSequence(PAGE_VIEW).numPages())throw std::runtime_error("Some content/geometry checkpoints are missing");
        QString action=spec.value("action").toString();
        if(action=="export"){
            QString directory=spec.value("directory").toString();QDir().mkpath(directory);QJsonArray rows;
            auto ops=std::make_shared<NonAcceleratedOperations>();
            for(auto const& s:samples){
                double scale=std::min(1.,1600./std::max(s.crop.width(),s.crop.height()));
                auto transform=s.transform->clone();transform->scale(scale,scale);
                QRect crop=transform->transformedCropArea().boundingRect().toAlignedRect();
                if(crop.isEmpty()||crop.width()>1800||crop.height()>1800)throw std::runtime_error("Unexpected preview dimensions");
                QImage source=ImageLoader::load(s.page.imageId());if(source.isNull())throw std::runtime_error("Cannot read source photo");
                auto image=transform->materialize(source,crop,Qt::white,ops);
                QString path=directory+QString("/page-%1.jpg").arg(s.ordinal,3,10,QChar('0'));
                if(QFileInfo::exists(path)||!image.save(path,"JPG",90))throw std::runtime_error("Preview already exists or could not be saved");
                auto px=QRectF(s.rect.x()*scale-crop.x(),s.rect.y()*scale-crop.y(),s.rect.width()*scale,s.rect.height()*scale);
                QFileInfo sourceInfo(s.page.imageId().filePath());
                rows.append(QJsonObject{{"page",s.ordinal},{"image",path},{"width",crop.width()},{"height",crop.height()},
                    {"current",rect(px)},{"manual",s.params.mode()==MODE_MANUAL},{"file",s.page.imageId().filePath()},
                    {"source_size",double(sourceInfo.size())},{"source_mtime",double(sourceInfo.lastModified().toMSecsSinceEpoch())},
                    {"subpage",s.page.id().subPageAsString()},{"transform",s.transform->fingerprint()},
                    {"scale",scale},{"pixel_origin",QJsonArray{crop.x(),crop.y()}}});
                std::cout<<"export "<<s.ordinal<<std::endl;
            }
            saveNew(directory+"/manifest.json",QJsonDocument(QJsonObject{{"schema",1},{"project",project},
                {"project_sha256",hash(projectData)},{"seconds",timer.elapsed()/1000.},{"pages",rows}}).toJson());
        }else if(action=="apply"){
            auto manifestData=read(spec.value("manifest").toString());auto manifest=QJsonDocument::fromJson(manifestData).object();
            auto decisions=QJsonDocument::fromJson(read(spec.value("decisions").toString())).object();
            if(manifest.value("project_sha256").toString()!=hash(projectData)||decisions.value("manifest_sha256").toString()!=hash(manifestData))throw std::runtime_error("Stale or mismatched review");
            QMap<int,QJsonObject> rows;
            for(auto v:manifest.value("pages").toArray()){auto r=v.toObject();rows.insert(r.value("page").toInt(),r);}
            struct Edit{int index;QRectF rect;bool blank;};std::vector<Edit> edits;QSet<int> seen;
            for(auto v:decisions.value("pages").toArray()){
                auto d=v.toObject();int page=d.value("page").toInt();QString choice=d.value("decision").toString();
                if(seen.contains(page)||!rows.contains(page))throw std::runtime_error("Duplicate or unknown page");
                seen.insert(page);
                if(choice=="keep"||choice=="review")continue;
                if(choice!="apply"&&choice!="blank")throw std::runtime_error("Unknown decision");
                int index=-1;for(size_t i=0;i<samples.size();++i)if(samples[i].ordinal==page){index=int(i);break;}
                if(index<0)throw std::runtime_error("Unknown project page");
                auto const& s=samples[index];auto m=rows[page];
                if(s.params.mode()==MODE_MANUAL)throw std::runtime_error("Existing manual selection is protected");
                QFileInfo sourceInfo(s.page.imageId().filePath());
                if(m.value("file").toString()!=s.page.imageId().filePath()||m.value("subpage").toString()!=s.page.id().subPageAsString()||m.value("transform").toString()!=s.transform->fingerprint()
                    ||qint64(m.value("source_size").toDouble())!=sourceInfo.size()||qint64(m.value("source_mtime").toDouble())!=sourceInfo.lastModified().toMSecsSinceEpoch())throw std::runtime_error("Source or transform changed");
                QRectF native;
                if(choice=="apply"){
                    auto pixels=parseRect(d.value("rect"));
                    if(!QRectF(0,0,m.value("width").toInt(),m.value("height").toInt()).contains(pixels))throw std::runtime_error("Selection outside preview");
                    auto origin=m.value("pixel_origin").toArray();double scale=m.value("scale").toDouble();
                    native=QRectF((pixels.x()+origin[0].toDouble())/scale,(pixels.y()+origin[1].toDouble())/scale,pixels.width()/scale,pixels.height()/scale).intersected(s.crop);
                    if(native.isEmpty())throw std::runtime_error("Empty selection");
                }
                edits.push_back({index,native,choice=="blank"});
            }
            for(auto const& e:edits){auto const& s=samples[e.index];
                if(e.blank){
                    if(!unified_content::matchesCurrent(s,stages))throw std::runtime_error("Selection became stale");
                    auto p=s.params;p.setContentBox(ContentBox());p.setContentSizePx(QSizeF());p.setMode(MODE_MANUAL);
                    stages.selectContentFilter()->getSettings()->setPageParams(s.page.id(),p);stages.pageLayoutFilter()->setContentBox(s.page.id(),QRectF());
                }else{
                    unified_content::Proposal p;p.index=e.index;p.rect=e.rect;
                    if(!unified_content::applyAi(p,s,stages,false))throw std::runtime_error("Cannot apply selection");
                }
            }
            QString output=spec.value("output_project").toString();
            if(QFileInfo(output).absoluteFilePath()==QFileInfo(project).absoluteFilePath())throw std::runtime_error("Source project is immutable");
            auto first=reader.pages()->toPageSequence(PAGE_VIEW).pageAt(0);
            OutputFileNameGenerator names(reader.namingDisambiguator(),spec.value("output_directory").toString(),reader.pages()->layoutDirection());
            ProjectWriter writer(reader.pages(),SelectedPage(first.id(),PAGE_VIEW),names);
            saveNew(output,writer.toXml(stages.filters(),QFileInfo(output).absolutePath()).toByteArray());
            std::cout<<"applied "<<edits.size()<<" of "<<samples.size()<<" in "<<timer.elapsed()/1000.<<" seconds"<<std::endl;
        }else throw std::runtime_error("Expected export or apply");
        return 0;
    }catch(std::exception const& e){std::cerr<<e.what()<<std::endl;return 1;}
}
