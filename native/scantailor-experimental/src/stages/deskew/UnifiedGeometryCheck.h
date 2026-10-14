// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QPolygonF>
#include <vector>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <cstring>
#include <cstdint>

namespace deskew {
// This project uses -ffast-math. Its finite-only assumption can compile
// std::isfinite() to true, precisely where invalid model inputs need guarding.
inline bool unifiedFinite(double value) {
    static_assert(sizeof(double)==sizeof(std::uint64_t)&&std::numeric_limits<double>::is_iec559,"IEEE double required");
    std::uint64_t bits;std::memcpy(&bits,&value,sizeof(bits));
    return (bits&UINT64_C(0x7ff0000000000000))!=UINT64_C(0x7ff0000000000000);
}
struct UnifiedGeometryMetrics {
    double lengthRatio=1, gapRatio=1, localStretch=1, areaRatio=1;
    double glyphAspect=0, textCoverage=1;
    QString reason;
    QString summary() const {
        return QString::fromUtf8("Üst/alt uzunluk: %1× · yükseklik farkı: %2× · yerel esneme: %3× · alan farkı: %4× · harf en/boy: %5 · model içinde metin: %6%")
            .arg(lengthRatio,0,'f',2).arg(gapRatio,0,'f',2).arg(localStretch,0,'f',2).arg(areaRatio,0,'f',2).arg(glyphAspect,0,'f',2).arg(textCoverage*100,0,'f',0);
    }
};
inline double unifiedLength(std::vector<QPointF> const& c) {
    double result=0;for(size_t i=1;i<c.size();++i)result+=std::hypot(c[i].x()-c[i-1].x(),c[i].y()-c[i-1].y());return result;
}
inline double unifiedYAt(std::vector<QPointF> const& c,double x) {
    for(size_t i=1;i<c.size();++i)if(c[i].x()>=x) {
        double d=c[i].x()-c[i-1].x();return d>1e-9?c[i-1].y()+(c[i].y()-c[i-1].y())*(x-c[i-1].x())/d:c[i].y();
    }
    return c.back().y();
}
// Conservative quality gate, not a proof of good geometry. Ratios are scale
// invariant. Moderate genuine perspective is allowed; severe disparity is
// flagged together with a reason. Manual geometry is only warned about.
inline UnifiedGeometryMetrics unifiedMeasureCurves(std::vector<QPointF> const& top,
        std::vector<QPointF> const& bottom,QRectF const& paper) {
    UnifiedGeometryMetrics m;
    auto fail=[&](char const* reason){m.reason=QString::fromUtf8(reason);return m;};
    if(top.size()<2||bottom.size()<2||paper.isEmpty())return fail("Eğrilik modeli eksik");
    for(auto const* curve:{&top,&bottom}) {
        QPointF prev=curve->front();
        for(auto const& p:*curve) {
            if(!unifiedFinite(p.x())||!unifiedFinite(p.y()))return fail("Geçersiz model koordinatı");
            if(!paper.adjusted(-paper.width()*.15,-paper.height()*.15,paper.width()*.15,paper.height()*.15).contains(p))
                return fail("Eğrilik modeli sayfanın dışına taşıyor");
            if(p.x()<prev.x()-1e-5)return fail("Eğrilik çizgisi kendi üzerine katlanıyor");
            prev=p;
        }
    }
    double a=unifiedLength(top),b=unifiedLength(bottom);
    if(std::min(a,b)<1e-6)return fail("Sınır çizgisinin uzunluğu sıfır");
    m.lengthRatio=std::max(a,b)/std::min(a,b);
    if(m.lengthRatio>1.85)return fail("Üst ve alt çizginin uzunluğu aşırı farklı");
    const double x0=std::max(top.front().x(),bottom.front().x());
    const double x1=std::min(top.back().x(),bottom.back().x());
    if(x1-x0<paper.width()*.30)return fail("Model sayfanın çok dar bir kısmını kapsıyor");
    double minGap=std::numeric_limits<double>::infinity(),maxGap=0;
    // Check all vertices as well as uniform samples, including interior crossings.
    std::vector<double> xs;
    for(int i=0;i<=64;++i)xs.push_back(x0+(x1-x0)*i/64.);
    for(auto const* c:{&top,&bottom})for(auto const& p:*c)if(p.x()>=x0&&p.x()<=x1)xs.push_back(p.x());
    for(double x:xs){double gap=unifiedYAt(bottom,x)-unifiedYAt(top,x);minGap=std::min(minGap,gap);maxGap=std::max(maxGap,gap);}
    if(minGap<=0)return fail("Üst ve alt çizgiler içeride kesişiyor");
    m.gapRatio=maxGap/minGap;
    if(maxGap<paper.height()*.23)return fail("Model yalnız birkaç satırı kapsıyor");
    if(m.gapRatio>3.5)return fail("Sayfanın bir bölümünde aşırı dikey esneme oluşuyor");
    return m;
}
// The fitted curves can be mutually plausible yet describe only the bottom
// third of a page (or its desk). Extrapolating that model across real text then
// curls a nearly flat page. Check independent source glyphs before dewarping.
// A header or page number alone must never trigger this gate.
inline void unifiedMeasureTextCoverage(UnifiedGeometryMetrics& m,
        std::vector<QPointF> const& top,std::vector<QPointF> const& bottom,
        std::vector<QPointF> const& glyphs,double padding) {
    if(!m.reason.isEmpty()||top.size()<2||bottom.size()<2)return;
    double x0=std::max(top.front().x(),bottom.front().x());
    double x1=std::min(top.back().x(),bottom.back().x());
    int tested=0,inside=0;
    for(auto const& p:glyphs){
        if(p.x()<x0||p.x()>x1)continue;
        ++tested;
        if(p.y()>=unifiedYAt(top,p.x())-padding && p.y()<=unifiedYAt(bottom,p.x())+padding)++inside;
    }
    if(tested<150)return;
    m.textCoverage=double(inside)/tested;
    if(m.textCoverage<.8 && tested-inside>=80)
        m.reason=QString::fromUtf8("Eğrilik modeli metnin önemli bölümünü dışarıda bırakıyor");
}
// Evaluate the actual inverse transform on a 15×15 interior grid. A positive
// Jacobian is necessary for a non-folding transform. Singular values detect
// narrow, stretched strips which boundary lengths alone cannot reveal.
inline void unifiedMeasureGrid(UnifiedGeometryMetrics& m,QPolygonF const& domain,
        std::function<QPointF(QPointF const&)> const& inverse) {
    if(!m.reason.isEmpty())return;
    QRectF bounds=domain.boundingRect();
    if(bounds.isEmpty()){m.reason=QString::fromUtf8("Düzeltme alanı boş");return;}
    double step=std::max(1e-4,std::min(bounds.width(),bounds.height())*.0005);
    std::vector<double> areas,ratios;
    for(int y=1;y<16;++y)for(int x=1;x<16;++x) {
        QPointF p(bounds.left()+bounds.width()*x/16.,bounds.top()+bounds.height()*y/16.);
        if(!domain.containsPoint(p,Qt::OddEvenFill))continue;
        QPointF v=inverse(p),dx=(inverse(p+QPointF(step,0))-v)/step,dy=(inverse(p+QPointF(0,step))-v)/step;
        double det=dx.x()*dy.y()-dy.x()*dx.y();
        if(!unifiedFinite(det)||det<=1e-8){m.reason=QString::fromUtf8("Dönüşüm ızgarası katlanıyor veya çöküyor");return;}
        double trace=dx.x()*dx.x()+dx.y()*dx.y()+dy.x()*dy.x()+dy.y()*dy.y();
        double disc=std::sqrt(std::max(0.,trace*trace-4*det*det));
        double large=(trace+disc)*.5,small=det*det/std::max(large,1e-12);
        areas.push_back(det);ratios.push_back(std::sqrt(large/std::max(small,1e-12)));
    }
    if(areas.size()<20){m.reason=QString::fromUtf8("Dönüşüm ızgarası yeterli alanı kapsamıyor");return;}
    std::sort(areas.begin(),areas.end());std::sort(ratios.begin(),ratios.end());
    m.areaRatio=areas.back()/areas.front();m.localStretch=ratios.back();
    if(m.localStretch>6)m.reason=QString::fromUtf8("Dönüşüm yazıyı bir yönde aşırı esnetiyor");
    else if(m.areaRatio>18)m.reason=QString::fromUtf8("Dönüşümde yerel büyütme aşırı değişiyor");
}
inline QString unifiedCheckCurves(std::vector<QPointF> const& top,std::vector<QPointF> const& bottom,QRectF const& paper) {
    return unifiedMeasureCurves(top,bottom,paper).reason;
}
}
