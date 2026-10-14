// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include "UnifiedGeometryReview.h"
#include "ProjectPages.h"
#include "PageSequence.h"
#include "StageSequence.h"
#include "ImageLoader.h"
#include "TaskStatus.h"
#include "stages/deskew/Filter.h"
#include "stages/deskew/Settings.h"
#include "stages/page_split/Filter.h"
#include "stages/page_split/Settings.h"
#include "stages/fix_orientation/Filter.h"
#include "stages/fix_orientation/Settings.h"
#include "dewarping/DewarpingImageTransform.h"
#include "acceleration/NonAcceleratedOperations.h"
#include "imageproc/GrayImage.h"
#include <QDomDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <stdexcept>

namespace unified_geometry {
using namespace imageproc;
using namespace dewarping;
namespace {
struct Status:TaskStatus {
    std::function<bool()> stop;
    explicit Status(std::function<bool()> f):stop(std::move(f)){}
    void cancel()override{}
    bool isCancelled()const override{return stop();}
    void throwIfCancelled()const override{if(stop())throw std::runtime_error("Geometri analizi durduruldu.");}
};
std::shared_ptr<AbstractImageTransform> transform(Sample const& s,deskew::Params const& params) {
    if(!params.validForDistortionType(params.distortionType()))throw std::runtime_error("Mevcut geometri modeli geçerli değil.");
    if(params.distortionType()==deskew::DistortionType::NONE)return std::make_shared<AffineImageTransform>(s.transform);
    if(params.distortionType()==deskew::DistortionType::ROTATION){
        auto result=std::make_shared<AffineImageTransform>(s.transform);
        result->rotate(params.getParamsRotation().compensationAngleDeg());return result;
    }
    std::vector<QPointF> top,bottom;
    auto const& warp=params.getParamsDewarping();
    if(params.distortionType()==deskew::DistortionType::WARP){
        top=warp.distortionModel().topCurve().polyline();bottom=warp.distortionModel().bottomCurve().polyline();
    }else{
        auto const& p=params.getParamsPerspective();
        top={p.corner(deskew::ParamsPerspective::TOP_LEFT),p.corner(deskew::ParamsPerspective::TOP_RIGHT)};
        bottom={p.corner(deskew::ParamsPerspective::BOTTOM_LEFT),p.corner(deskew::ParamsPerspective::BOTTOM_RIGHT)};
    }
    return std::make_shared<DewarpingImageTransform>(s.transform.origSize(),s.transform.origCropArea(),top,bottom,
        params.getParamsSource().photo()?params.getParamsSource().fov():0.,
        params.distortionType()==deskew::DistortionType::WARP?warp.depthPerception():DepthPerception(),
        params.distortionType()==deskew::DistortionType::WARP?warp.correctCurves():DepthPerception(),
        params.distortionType()==deskew::DistortionType::WARP?warp.correctAngle():DepthPerception());
}
void savePreview(QImage const& source,AbstractImageTransform const& input,QString const& path,
                 std::shared_ptr<AcceleratableOperations> const& ops){
    auto t=input.clone();auto bounds=t->transformedCropArea().boundingRect();
    if(bounds.isEmpty()||!deskew::unifiedFinite(bounds.width())||!deskew::unifiedFinite(bounds.height()))throw std::runtime_error("Geometri sınırı geçersiz.");
    double scale=std::min(1.,1400./std::max(bounds.width(),bounds.height()));t->scale(scale,scale);
    auto rect=t->transformedCropArea().boundingRect().toAlignedRect();
    if(rect.width()>1600||rect.height()>1600||rect.width()<8||rect.height()<8)throw std::runtime_error("Geometri önizleme boyutu geçersiz.");
    auto image=t->materialize(source,rect,Qt::white,ops);
    if(!image.save(path))throw std::runtime_error("Geometri önizlemesi yazılamadı.");
}
struct Process {
    QProcess child;QByteArray buffer,errors;
    ~Process(){if(child.state()!=QProcess::NotRunning){child.terminate();if(!child.waitForFinished(500)){child.kill();child.waitForFinished(500);}}}
    QJsonObject request(QJsonObject const& input,QString const& root,std::function<bool()> const& cancel){
        if(child.state()==QProcess::NotRunning){
            auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("PYTHONIOENCODING","utf-8");env.insert("OMP_NUM_THREADS","2");
            child.setProcessEnvironment(env);child.setWorkingDirectory(root);
            child.start(unified_runtime::python(root),{"-u",root+"/src/scantailor_ocr_unified/geometry_runtime.py"});
            if(!child.waitForStarted(10000))throw std::runtime_error("UVDoc çalışma ortamı açılamadı.");
        }
        child.write(QJsonDocument(input).toJson(QJsonDocument::Compact)+"\n");child.waitForBytesWritten(1000);
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<120000){
            if(cancel())throw std::runtime_error("Geometri analizi durduruldu.");
            child.waitForReadyRead(100);buffer+=child.readAllStandardOutput();errors+=child.readAllStandardError();
            if(errors.size()>4096)errors=errors.right(4096);
            int end;while((end=buffer.indexOf('\n'))>=0){auto line=buffer.left(end);buffer.remove(0,end+1);
                if(!line.startsWith("STOUGEOMETRY "))continue;
                auto result=QJsonDocument::fromJson(line.mid(13)).object();if(result.value("id")==input.value("id"))return result;
            }
            if(child.state()==QProcess::NotRunning)throw std::runtime_error((unified_i18n::text("UVDoc kapandı: ")+unified_i18n::text(errors)).toStdString());
            if(buffer.size()>4*1024*1024)throw std::runtime_error("UVDoc yanıt boyutu sınırı aşıldı.");
        }
        throw std::runtime_error("UVDoc zamanında yanıt vermedi; mevcut geometri korundu.");
    }
};
}
QString state(deskew::Params const& params){QDomDocument doc;auto el=params.toXml(doc,"params");el.removeAttribute("unifiedWarning");el.removeAttribute("unifiedGeometry");el.firstChildElement("warp").removeAttribute("unifiedDetection");doc.appendChild(el);return doc.toString(-1);}
std::vector<Sample> collect(ProjectPages const& pages,StageSequence& stages){
    std::vector<Sample> samples;auto seq=pages.toPageSequence(PAGE_VIEW);
    for(size_t i=0;i<seq.numPages();++i){
        auto page=seq.pageAt(i);auto rotation=stages.fixOrientationFilter()->getSettings()->getRotationFor(page.imageId());
        auto split=stages.pageSplitFilter()->getSettings()->getPageRecord(page.imageId());
        if(!split.params())continue;
        page_split::Dependencies deps(page.metadata().size(),rotation,split.combinedLayoutType());
        if(!deps.compatibleWith(*split.params()))continue;
        AffineImageTransform t(page.metadata().size());t.rotate(rotation.toDegrees());
        auto layout=split.params()->pageLayout();
        if(layout.uncutOutline().isEmpty())layout.setUncutOutline(QRectF(QPointF(0,0),rotation.rotate(page.metadata().size())));
        t.setOrigCropArea(t.transform().inverted().map(layout.pageOutline(page.id().subPage())));
        auto params=stages.deskewFilter()->getSettings()->getPageParams(page.id());
        if(!params||!params->dependencies().matches(deskew::Dependencies(t.origCropArea(),rotation)))continue;
        samples.push_back({page,int(i+1),*params,t,state(*params)});
    }return samples;
}
void run(std::vector<Sample> const& samples,std::vector<int> const& indices,QString const& root,
    QString const& device,bool useAi,QString const& directory,std::function<bool()> const& cancelled,
    std::function<void(Result)> const& completed){
    QDir().mkpath(directory);Process process;Status status(cancelled);auto ops=std::make_shared<NonAcceleratedOperations>();
    for(int index:indices){
        if(cancelled())break;if(index<0||size_t(index)>=samples.size())continue;
        auto const& s=samples[index];Result out;out.index=index;QElapsedTimer timer;timer.start();
        try{
            QImage source=ImageLoader::load(s.page.imageId());if(source.isNull())throw std::runtime_error("Kaynak fotoğraf okunamadı.");
            out.source=directory+QString("/%1-source.png").arg(index);savePreview(source,s.transform,out.source,ops);
            std::shared_ptr<AbstractImageTransform> current;
            try{current=transform(s,s.params);}catch(std::exception const&){current=std::make_shared<AffineImageTransform>(s.transform);}
            out.current=directory+QString("/%1-current.png").arg(index);savePreview(source,*current,out.current,ops);
            std::vector<deskew::UnifiedDewarpCandidate> extra;
            QString aiError;
            if(useAi){
                // Fit in the correctly oriented, already split page. Coordinates
                // are mapped back to the original photograph before saving.
                AffineImageTransform small(s.transform);small.scaleTo(QSize(1500,1500),Qt::KeepAspectRatio);
                auto rect=small.transformedCropArea().boundingRect().toAlignedRect();
                auto image=small.materialize(source,rect,Qt::white,ops);
                QPainter painter(&image);QPainterPath outside;outside.addRect(image.rect());
                auto polygon=small.transformedCropArea();polygon.translate(-rect.x(),-rect.y());QPainterPath inside;inside.addPolygon(polygon);
                painter.fillPath(outside.subtracted(inside),Qt::white);painter.end();
                QString input=directory+QString("/%1-ai-input.png").arg(index);image.save(input);
                try{
                    auto json=process.request({{"id",index},{"image",input},{"device",device}},root,cancelled);
                    if(!json.value("ok").toBool())throw std::runtime_error(json.value("error").toString().toStdString());
                    out.cached=json.value("cached").toBool();deskew::UnifiedDewarpCandidate ai;ai.source=unified_i18n::text("UVDoc eğri önerisi");
                    auto curve=[&](char const* key){std::vector<QPointF> points;for(auto value:json.value(key).toArray()){auto p=value.toArray();if(p.size()!=2)throw std::runtime_error("UVDoc eğri koordinatı eksik.");points.push_back(small.transform().inverted().map(QPointF(p[0].toDouble()+rect.x(),p[1].toDouble()+rect.y())));}return Curve(points);};
                    ai.model.setTopCurve(curve("top"));ai.model.setBottomCurve(curve("bottom"));
                    QStringList reasons;for(auto v:json.value("reasons").toArray())reasons<<unified_i18n::display(v.toString());ai.metrics.reason=reasons.join(' ');extra.push_back(std::move(ai));
                }catch(std::exception const& e){aiError=unified_i18n::text(e.what());}
            }
            status.throwIfCancelled();
            auto selection=deskew::unifiedSelectAutoDewarp(GrayImage(source),s.transform,s.params.getParamsDewarping(),
                s.params.getParamsSource().photo()?s.params.getParamsSource().fov():0.,ops,status,nullptr,extra,current->forwardMapper());
            out.currentError=selection.currentError;out.report=selection.report;
            if(!aiError.isEmpty())out.report+=unified_i18n::text("\nUVDoc: ")+aiError;
            int best=deskew::unifiedChooseDewarpCandidate(selection.candidates);
            for(auto const& candidate:selection.candidates){
                status.throwIfCancelled();auto params=s.params;params.setDistortionType(deskew::DistortionType::WARP);
                params.getParamsDewarping().setDistortionModel(candidate.model);params.getParamsDewarping().setMode(MODE_MANUAL);
                params.getParamsDewarping().setDetectionReport(unified_i18n::text("İncelenmiş geometri · ")+candidate.source);
                params.unifiedWarning.clear();params.unifiedGeometry=candidate.metrics.summary();
                Candidate c{candidate.source,{},candidate.metrics.reason,params,candidate.metrics,candidate.lineError,candidate.score,candidate.accepted()};
                // Sparse covers cannot be ranked by text residual. A UVDoc model
                // that passed all geometric checks is still reviewable by hand,
                // but is never a bulk recommendation.
                if(candidate.model.isValid()&&candidate.source.startsWith("UVDoc")&&candidate.metrics.reason.startsWith(unified_i18n::text("kenarı doğrulayacak yeterli satır yok"))){c.accepted=true;c.reason=unified_i18n::text("Az metin: yalnız görsel incelemeyle tek sayfaya uygulanabilir; toplu önerilmez.");}
                if(c.accepted){c.path=directory+QString("/%1-candidate-%2.png").arg(index).arg(out.candidates.size());
                    try{savePreview(source,*transform(s,params),c.path,ops);}catch(std::exception const& e){c.accepted=false;c.reason=unified_i18n::text(e.what());c.path.clear();}}
                out.candidates.push_back(std::move(c));
            }
            if(best>=0&&out.candidates[best].accepted&&deskew::unifiedFinite(selection.currentError)&&
               out.candidates[best].error+.25<selection.currentError*.90)out.recommended=best;
            out.report+=out.recommended>=0?unified_i18n::text("\nÖnerilen: ")+out.candidates[out.recommended].name:
                unified_i18n::text("\nMevcut sonucu değiştirecek yeterli iyileşme bulunmadı.");
        }catch(std::exception const& e){out.error=unified_i18n::text(e.what());}
        out.seconds=timer.elapsed()/1000.;completed(std::move(out));
    }
}
bool matchesCurrent(Sample const& s,StageSequence& stages,Candidate const* applied){
    auto current=stages.deskewFilter()->getSettings()->getPageParams(s.page.id());
    if(!current||state(*current)!=(applied?state(applied->params):s.state))return false;
    // Check rotation/split too: cached parameters may not yet be recomputed.
    auto rotation=stages.fixOrientationFilter()->getSettings()->getRotationFor(s.page.imageId());
    auto split=stages.pageSplitFilter()->getSettings()->getPageRecord(s.page.imageId());
    if(!split.params())return false;
    if(!page_split::Dependencies(s.page.metadata().size(),rotation,split.combinedLayoutType()).compatibleWith(*split.params()))return false;
    AffineImageTransform t(s.page.metadata().size());t.rotate(rotation.toDegrees());
    auto layout=split.params()->pageLayout();if(layout.uncutOutline().isEmpty())layout.setUncutOutline(QRectF(QPointF(0,0),rotation.rotate(s.page.metadata().size())));
    auto outline=layout.pageOutline(s.page.id().subPage());
    t.setOrigCropArea(t.transform().inverted().map(outline));
    return t.fingerprint()==s.transform.fingerprint()&&s.params.dependencies().matches(deskew::Dependencies(t.origCropArea(),rotation));
}
bool apply(Sample const& s,Candidate const& c,StageSequence& stages,bool allowManual){
    if(!c.accepted||!matchesCurrent(s,stages)||(s.params.mode()==MODE_MANUAL&&!allowManual))return false;
    stages.deskewFilter()->getSettings()->setPageParams(s.page.id(),c.params);return true;
}
bool undo(Sample const& s,Candidate const& c,StageSequence& stages){
    if(!matchesCurrent(s,stages,&c))return false;
    stages.deskewFilter()->getSettings()->setPageParams(s.page.id(),s.params);return true;
}
}
