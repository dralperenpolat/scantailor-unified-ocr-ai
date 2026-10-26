// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedI18n.h"
#include "UnifiedAutoDewarp.h"
#include "UnifiedLineFit.h"
#include "UnifiedTextLineTracer.h"
#include "ParamsDewarping.h"
#include "TaskStatus.h"
#include "acceleration/AcceleratableOperations.h"
#include "dewarping/DistortionModelBuilder.h"
#include "dewarping/TextLineTracer.h"
#include "dewarping/DetectVerticalBounds.h"
#include "dewarping/TopBottomEdgeTracer.h"
#include "dewarping/DewarpingImageTransform.h"
#include "imageproc/AffineImageTransform.h"
#include "imageproc/AffineTransformedImage.h"
#include "imageproc/Binarize.h"
#include "imageproc/ConnCompEraser.h"
#include "imageproc/GrayImage.h"
#include "imageproc/Morphology.h"
#include "imageproc/AffineTransform.h"
#include "imageproc/PolygonRasterizer.h"
#include <QStringList>
#include <stdexcept>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

namespace deskew {
using namespace imageproc;
using namespace dewarping;
namespace {
double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size()-1, size_t(p*(values.size()-1)))];
}
std::vector<QPointF> mappedCurve(std::vector<QPointF> const& curve, QTransform const& transform) {
    std::vector<QPointF> result; result.reserve(curve.size());
    for (auto const& p:curve) result.push_back(transform.map(p));
    return result;
}
struct Evidence {
    QTransform sampleTransform;
    std::vector<QPointF> glyphs;
    double padding = 0;
    double glyphAspect = 0;
    bool sideways = false;
};
Evidence sourceEvidence(GrayImage const& image, AffineImageTransform const& transform,
    std::shared_ptr<AcceleratableOperations> const& ops, TaskStatus const& status) {
    Evidence e;
    AffineImageTransform small(transform); small.scaleTo(QSize(1100,1100),Qt::KeepAspectRatio);
    QRect rect = small.transformedCropArea().boundingRect().toRect();
    if (rect.isEmpty()) return e;
    GrayImage sample(ops->affineTransform(image,small.transform(),rect,OutsidePixels::assumeColor(Qt::white)));
    QTransform local; local.translate(-rect.x(),-rect.y()); e.sampleTransform=small.transform()*local;
    BinaryImage binary(binarizeWolf(sample,25,.30f));
    auto crop=small.transformedCropArea();crop.translate(-rect.x(),-rect.y());
    PolygonRasterizer::fillExcept(binary,WHITE,crop,Qt::WindingFill);
    QRect interior(QPoint(0,0),rect.size());
    interior.adjust(interior.width()*.12,interior.height()*.12,-interior.width()*.12,-interior.height()*.12);
    auto lineSupport=[&](bool vertical){
        BinaryImage joined(closeBrick(binary,vertical?QSize(3,17):QSize(17,3)));
        ConnCompEraser lines(joined,CONN8); double score=0;
        for(auto c=lines.nextConnComp();!c.isNull();c=lines.nextConnComp()){
            int length=vertical?c.height():c.width(),thickness=vertical?c.width():c.height();
            if(length>60&&thickness>2&&thickness<30&&length>thickness*4&&interior.contains(c.rect().center()))score+=c.pixCount();
        }
        return score;
    };
    double horizontal=lineSupport(false),vertical=lineSupport(true);
    e.sideways=vertical>1500&&vertical>horizontal*3;
    ConnCompEraser components(binary,CONN8); std::vector<double> heights,ratios;
    for(auto c=components.nextConnComp();!c.isNull();c=components.nextConnComp()){
        if(c.width()<3||c.height()<3||c.width()>35||c.height()>35||c.pixCount()<7||!interior.contains(c.rect().center()))continue;
        double ratio=double(c.width())/c.height();
        if(ratio>.2&&ratio<5){e.glyphs.push_back(c.rect().center());heights.push_back(c.height());ratios.push_back(ratio);}
    }
    if(heights.size()>=150){e.padding=3*percentile(heights,.5);e.glyphAspect=percentile(ratios,.5);}
    status.throwIfCancelled();
    return e;
}
}

double unifiedTextLineError(std::deque<std::vector<QPointF>> const& lines,
    std::function<QPointF(QPointF const&)> const& mapper, bool removeGlobalSkew) {
    std::vector<std::vector<QPointF>> mapped;
    std::vector<double> slopes;
    for (auto const& line:lines) {
        if (line.size()<2) continue;
        std::vector<QPointF> points;
        size_t count=std::min(size_t(40),line.size());
        for(size_t i=0;i<count;++i){
            QPointF p=mapper(line[i*(line.size()-1)/(count-1)]);
            if(!unifiedFinite(p.x())||!unifiedFinite(p.y()))return std::numeric_limits<double>::infinity();
            points.push_back(p);
        }
        double dx=points.back().x()-points.front().x();
        if(dx<=1e-6)return std::numeric_limits<double>::infinity();
        slopes.push_back((points.back().y()-points.front().y())/dx);
        mapped.push_back(std::move(points));
    }
    if(mapped.empty())return std::numeric_limits<double>::infinity();
    double slope=removeGlobalSkew?percentile(slopes,.5):0;
    std::vector<double> residuals;
    for(auto const& line:mapped){
        std::vector<double> ys;
        for(auto const& p:line)ys.push_back(p.y()-slope*p.x());
        double width=line.back().x()-line.front().x();
        residuals.push_back(1000*(percentile(ys,.9)-percentile(ys,.1))/width);
    }
    // Do not let a lone hand/underline dominate, but also do not hide a bad
    // upper/lower quarter behind a perfect median of body lines.
    return .6*percentile(residuals,.5)+.4*percentile(residuals,.8);
}

int unifiedChooseDewarpCandidate(std::vector<UnifiedDewarpCandidate> const& candidates) {
    int best=-1;
    for(size_t i=0;i<candidates.size();++i){
        auto const& c=candidates[i];
        if(c.accepted() && (best<0 || c.score<candidates[best].score-1e-6))best=int(i);
    }
    return best;
}

UnifiedDewarpSelection unifiedSelectAutoDewarp(GrayImage const& image,
    AffineImageTransform const& transform, ParamsDewarping const& params, double fov,
    std::shared_ptr<AcceleratableOperations> const& ops, TaskStatus const& status, DebugImages* dbg,
    std::vector<UnifiedDewarpCandidate> const& extra,
    std::function<QPointF(QPointF const&)> const& currentMapper,bool optimizeStrength,QString const& tracePath) {
    UnifiedDewarpSelection result;
    QElapsedTimer profile;profile.start();QJsonObject timings;
    Evidence const evidence=sourceEvidence(image,transform,ops,status);
    timings["evidence_ms"]=double(profile.restart());
    if(evidence.sideways){result.report=unified_i18n::text("Karşılaştırma: yan basılmış metinde otomatik eğrilik uygulanmadı");return result;}
    QTransform const inverse=transform.transform().inverted();
    // A direction is a vector: translations must cancel (not map(0,1) alone).
    Vec2d const down(inverse.map(QPointF(0,1))-inverse.map(QPointF(0,0)));
    DistortionModelBuilder text(down),edges(down);
    bool const wordEvidence=unifiedTraceWords(image,transform,ops,status,text);
    if(!wordEvidence && evidence.glyphs.size()>=40)
        TextLineTracer::trace(AffineTransformedImage(image,transform),text,ops,status,dbg,true);
    timings["trace_ms"]=double(profile.restart());
    auto bounds=text.verticalBounds();
    if(bounds.first.isNull()||bounds.second.isNull()){
        QRectF const crop=transform.transformedCropArea().boundingRect();
        bounds={inverse.map(QLineF(crop.topLeft(),crop.bottomLeft())),inverse.map(QLineF(crop.topRight(),crop.bottomRight()))};
    }
    edges.setVerticalBounds(bounds.first,bounds.second);
    TopBottomEdgeTracer::trace(image,bounds,edges,status,dbg);
    timings["edges_ms"]=double(profile.restart());
    DistortionModelBuilder mixed=text;
    mixed.setVerticalBounds(bounds.first,bounds.second);
    for(auto const& curve:edges.horizontalCurves())mixed.addHorizontalCurve(curve);

    auto measure=[&](DistortionModel const& model){
        auto m=unifiedMeasureCurves(mappedCurve(model.topCurve().polyline(),transform.transform()),
            mappedCurve(model.bottomCurve().polyline(),transform.transform()),transform.transformedCropArea().boundingRect());
        m.glyphAspect=evidence.glyphAspect;
        unifiedMeasureTextCoverage(m,mappedCurve(model.topCurve().polyline(),evidence.sampleTransform),
            mappedCurve(model.bottomCurve().polyline(),evidence.sampleTransform),evidence.glyphs,evidence.padding);
        return m;
    };
    QString rejectedReason;
    auto acceptable=[&](DistortionModel const& model){
        status.throwIfCancelled();auto m=measure(model);
        if(!m.reason.isEmpty())rejectedReason=m.reason;
        return m.reason.isEmpty();
    };
    double baseline=unifiedTextLineError(text.horizontalCurves(),[&](QPointF const& p){return transform.transform().map(p);},true);
    result.lineCount=int(text.horizontalCurves().size());
    result.currentError=currentMapper?unifiedTextLineError(text.horizontalCurves(),currentMapper):baseline;
    std::deque<std::vector<QPointF>> validationLines;
    int lineIndex=0;for(auto const& line:text.horizontalCurves())if(++lineIndex%4==0)validationLines.push_back(line);
    result.currentValidationError=currentMapper?unifiedTextLineError(validationLines,currentMapper):
        unifiedTextLineError(validationLines,[&](QPointF const& p){return transform.transform().map(p);},true);
    if(!tracePath.isEmpty()){
        QJsonArray lines;for(auto const& line:text.horizontalCurves()){QJsonArray points;for(auto const& p:line){auto q=transform.transform().map(p);points.append(QJsonArray{q.x(),q.y()});}lines.append(points);}
        auto crop=transform.transformedCropArea().boundingRect();QFile file(tracePath);if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"lines",lines},{"crop",QJsonArray{crop.x(),crop.y(),crop.width(),crop.height()}}}).toJson(QJsonDocument::Compact));
    }
    QStringList report;
    result.candidates.push_back(unifiedFitTextSheet(text.horizontalCurves(),bounds,transform,1));
    result.candidates.push_back(unifiedFitTextSheet(text.horizontalCurves(),bounds,transform,3));
    result.candidates.push_back(unifiedFitTextSheet(text.horizontalCurves(),bounds,transform,5));
    // Indented contents entries have different line lengths. Their left and
    // right margin fits can converge inside the text, giving a wide upper
    // curve and a very short lower one. Compare a common text-supported span
    // as an alternative; keep the perspective model as a competing candidate.
    std::vector<double> starts,ends;
    for(auto const& line:text.horizontalCurves())if(line.size()>=4) {
        starts.push_back(transform.transform().map(line.front()).x());
        ends.push_back(transform.transform().map(line.back()).x());
    }
    if(starts.size()>=8) {
        auto paper=transform.transformedCropArea().boundingRect();
        double x0=percentile(starts,.02),x1=percentile(ends,.98),pad=(x1-x0)*.025;
        x0=std::max(paper.left(),x0-pad);x1=std::min(paper.right(),x1+pad);
        std::pair<QLineF,QLineF> parallel={inverse.map(QLineF(x0,paper.top(),x0,paper.bottom())),
            inverse.map(QLineF(x1,paper.top(),x1,paper.bottom()))};
        for(int degree:{1,3,5}) {
            auto candidate=unifiedFitTextSheet(text.horizontalCurves(),parallel,transform,degree);
            candidate.source=unified_i18n::text("Dengeli sınırlar · ")+candidate.source;
            result.candidates.push_back(std::move(candidate));
        }
    }
    // Keep the existing mixed model on exact ties, avoiding arbitrary changes.
    for(auto const& entry:std::vector<std::pair<QString,DistortionModelBuilder const*>>{
        {unified_i18n::text("Birleşik"),&mixed},{unified_i18n::text("Metin"),&text},{unified_i18n::text("Kenar"),&edges}}){
        status.throwIfCancelled();
        UnifiedDewarpCandidate candidate; candidate.source=entry.first;
        rejectedReason.clear();
        candidate.model=entry.second->tryBuildModel(nullptr,nullptr,acceptable);
        if(!candidate.model.isValid())candidate.metrics.reason=rejectedReason.isEmpty()?unified_i18n::text("güvenli sınır çifti bulunamadı"):rejectedReason;
        result.candidates.push_back(std::move(candidate));
    }
    result.candidates.insert(result.candidates.end(),extra.begin(),extra.end());
    timings["build_ms"]=double(profile.restart());
    for(auto& candidate:result.candidates) {
        status.throwIfCancelled();
        if(candidate.model.isValid()&&candidate.metrics.reason.isEmpty()) {
            candidate.metrics=measure(candidate.model);
            try {
                // Reuse the same traced lines. Tune the bounded native warp
                // strengths, rather than rerunning inference or weakening gates.
                std::vector<std::vector<double>> strengths={{candidate.depth>=0?candidate.depth:params.depthPerception().value(),candidate.curves>=0?candidate.curves:params.correctCurves().value(),candidate.angle>=0?candidate.angle:params.correctAngle().value()}};
                if(optimizeStrength){strengths.push_back({1.,1.,1.});strengths.push_back({1.,2.,2.});strengths.push_back({2.,1.,1.});strengths.push_back({3.,2.,2.});}
                auto baseMetrics=candidate.metrics;auto best=candidate;bool first=true;
                for(auto const& strength:strengths){
                    status.throwIfCancelled();auto trial=candidate;trial.metrics=baseMetrics;
                    trial.depth=strength[0];trial.curves=strength[1];trial.angle=strength[2];
                    DewarpingImageTransform warp(transform.origSize(),transform.origCropArea(),trial.model.topCurve().polyline(),
                        trial.model.bottomCurve().polyline(),fov,DepthPerception(trial.depth),DepthPerception(trial.curves),DepthPerception(trial.angle));
                    unifiedMeasureGrid(trial.metrics,warp.transformedCropArea(),warp.backwardMapper());
                    trial.lineError=unifiedTextLineError(text.horizontalCurves(),warp.forwardMapper());
                    trial.validationError=unifiedTextLineError(validationLines,warp.forwardMapper());
                    if(trial.metrics.reason.isEmpty()){
                        // At least two independent held-out lines are needed.
                        // Sparse title pages can fit straight lines while the
                        // unconstrained page aspect ratio becomes implausible.
                        if(text.horizontalCurves().size()<8 || !unifiedFinite(trial.lineError))trial.metrics.reason=unified_i18n::text("kenarı doğrulayacak yeterli satır yok; elle kontrol gerekli");
                        else if(unifiedFinite(baseline) && trial.lineError>baseline*1.15+.5)trial.metrics.reason=unified_i18n::text("metin satırları mevcut görüntüden daha fazla bozuluyor");
                        else trial.score=trial.lineError+1.5*std::log(std::max(1.,trial.metrics.localStretch))+.5*std::log(std::max(1.,trial.metrics.areaRatio))+.75*std::log(std::max(1.,trial.metrics.lengthRatio))+4*(1-trial.metrics.textCoverage);
                    }
                    if(first||(trial.accepted()&&(!best.accepted()||trial.score<best.score))){best=trial;first=false;}
                }
                candidate=best;
            } catch(std::runtime_error const&){candidate.metrics.reason=unified_i18n::text("kararlı dönüşüm üretilemedi");}
        }
        report << (candidate.accepted()?unified_i18n::text("%1: %2").arg(candidate.source).arg(candidate.score,0,'f',2)
            :unified_i18n::text("%1: elendi (%2)").arg(candidate.source,candidate.metrics.reason));
    }
    int const best=unifiedChooseDewarpCandidate(result.candidates);
    if(best>=0){auto const& c=result.candidates[best];result.model=c.model;result.source=c.source;result.depth=c.depth;result.curves=c.curves;result.angle=c.angle;}
    result.report=unified_i18n::text("Metin + kenar karşılaştırması · %1\n%2\nSatır: %3 · kenar: %4 · başlangıç satır hatası: %5")
        .arg(best>=0?unified_i18n::text("seçilen: ")+result.source:unified_i18n::text("güvenli aday yok"),report.join(unified_i18n::text(" · ")))
        .arg(text.horizontalCurves().size()).arg(edges.horizontalCurves().size()).arg(baseline,0,'f',2);
    timings["assess_ms"]=double(profile.elapsed());
    timings["word_evidence"]=wordEvidence;
    if(qEnvironmentVariableIsSet("STOU_GEOMETRY_PROFILE"))result.report+="\nPROFILE "+QString::fromUtf8(QJsonDocument(timings).toJson(QJsonDocument::Compact));
    return result;
}
}
