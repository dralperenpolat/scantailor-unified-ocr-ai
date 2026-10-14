// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedAutoDewarp.h"
#include "ParamsDewarping.h"
#include "TaskStatus.h"
#include "acceleration/AcceleratableOperations.h"
#include "dewarping/DistortionModelBuilder.h"
#include "dewarping/TextLineTracer.h"
#include "dewarping/TopBottomEdgeTracer.h"
#include "dewarping/DewarpingImageTransform.h"
#include "imageproc/AffineImageTransform.h"
#include "imageproc/AffineTransformedImage.h"
#include "imageproc/Binarize.h"
#include "imageproc/ConnCompEraser.h"
#include "imageproc/GrayImage.h"
#include "imageproc/Morphology.h"
#include "imageproc/AffineTransform.h"
#include <QStringList>
#include <stdexcept>

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
    std::function<QPointF(QPointF const&)> const& currentMapper) {
    UnifiedDewarpSelection result;
    Evidence const evidence=sourceEvidence(image,transform,ops,status);
    if(evidence.sideways){result.report=QString::fromUtf8("Karşılaştırma: yan basılmış metinde otomatik eğrilik uygulanmadı");return result;}
    QTransform const inverse=transform.transform().inverted();
    // A direction is a vector: translations must cancel (not map(0,1) alone).
    Vec2d const down(inverse.map(QPointF(0,1))-inverse.map(QPointF(0,0)));
    DistortionModelBuilder text(down),edges(down);
    TextLineTracer::trace(AffineTransformedImage(image,transform),text,ops,status,dbg);
    auto bounds=text.verticalBounds();
    if(bounds.first.isNull()||bounds.second.isNull()){
        QRectF const crop=transform.transformedCropArea().boundingRect();
        bounds={inverse.map(QLineF(crop.topLeft(),crop.bottomLeft())),inverse.map(QLineF(crop.topRight(),crop.bottomRight()))};
    }
    edges.setVerticalBounds(bounds.first,bounds.second);
    TopBottomEdgeTracer::trace(image,bounds,edges,status,dbg);
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
    QStringList report;
    // Keep the existing mixed model on exact ties, avoiding arbitrary changes.
    for(auto const& entry:std::vector<std::pair<QString,DistortionModelBuilder const*>>{
        {QString::fromUtf8("Birleşik"),&mixed},{QString::fromUtf8("Metin"),&text},{QString::fromUtf8("Kenar"),&edges}}){
        status.throwIfCancelled();
        UnifiedDewarpCandidate candidate; candidate.source=entry.first;
        rejectedReason.clear();
        candidate.model=entry.second->tryBuildModel(nullptr,nullptr,acceptable);
        if(!candidate.model.isValid())candidate.metrics.reason=rejectedReason.isEmpty()?QString::fromUtf8("güvenli sınır çifti bulunamadı"):rejectedReason;
        result.candidates.push_back(std::move(candidate));
    }
    result.candidates.insert(result.candidates.end(),extra.begin(),extra.end());
    for(auto& candidate:result.candidates) {
        status.throwIfCancelled();
        if(candidate.model.isValid()&&candidate.metrics.reason.isEmpty()) {
            candidate.metrics=measure(candidate.model);
            try {
                DewarpingImageTransform warp(transform.origSize(),transform.origCropArea(),candidate.model.topCurve().polyline(),
                    candidate.model.bottomCurve().polyline(),fov,params.depthPerception(),params.correctCurves(),params.correctAngle());
                unifiedMeasureGrid(candidate.metrics,warp.transformedCropArea(),warp.backwardMapper());
                candidate.lineError=unifiedTextLineError(text.horizontalCurves(),warp.forwardMapper());
                if(candidate.metrics.reason.isEmpty()){
                    if(text.horizontalCurves().size()<4 || !unifiedFinite(candidate.lineError))
                        candidate.metrics.reason=QString::fromUtf8("kenarı doğrulayacak yeterli satır yok; elle kontrol gerekli");
                    else if(unifiedFinite(baseline) && candidate.lineError>baseline*1.15+.5)
                        candidate.metrics.reason=QString::fromUtf8("metin satırları mevcut görüntüden daha fazla bozuluyor");
                    else candidate.score=candidate.lineError+1.5*std::log(std::max(1.,candidate.metrics.localStretch))
                        +.5*std::log(std::max(1.,candidate.metrics.areaRatio))+4*(1-candidate.metrics.textCoverage);
                }
            } catch(std::runtime_error const&){candidate.metrics.reason=QString::fromUtf8("kararlı dönüşüm üretilemedi");}
        }
        report << (candidate.accepted()?QString::fromUtf8("%1: %2").arg(candidate.source).arg(candidate.score,0,'f',2)
            :QString::fromUtf8("%1: elendi (%2)").arg(candidate.source,candidate.metrics.reason));
    }
    int const best=unifiedChooseDewarpCandidate(result.candidates);
    if(best>=0){result.model=result.candidates[best].model;result.source=result.candidates[best].source;}
    result.report=QString::fromUtf8("Metin + kenar karşılaştırması · %1\n%2\nSatır: %3 · kenar: %4 · başlangıç satır hatası: %5")
        .arg(best>=0?QString::fromUtf8("seçilen: ")+result.source:QString::fromUtf8("güvenli aday yok"),report.join(QString::fromUtf8(" · ")))
        .arg(text.horizontalCurves().size()).arg(edges.horizontalCurves().size()).arg(baseline,0,'f',2);
    return result;
}
}
