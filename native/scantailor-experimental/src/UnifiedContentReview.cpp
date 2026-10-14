// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedI18n.h"
#include "UnifiedContentReview.h"
#include "ProjectPages.h"
#include "PageSequence.h"
#include "StageSequence.h"
#include "CompositeCacheDrivenTask.h"
#include "ContentBox.h"
#include "ImageLoader.h"
#include "filter_dc/ContentBoxCollector.h"
#include "stages/fix_orientation/Filter.h"
#include "stages/fix_orientation/CacheDrivenTask.h"
#include "stages/page_split/Filter.h"
#include "stages/page_split/CacheDrivenTask.h"
#include "stages/deskew/Filter.h"
#include "stages/deskew/CacheDrivenTask.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/Settings.h"
#include "stages/select_content/CacheDrivenTask.h"
#include "stages/page_layout/Filter.h"
#include "stages/page_layout/CacheDrivenTask.h"
#include "imageproc/AffineImageTransform.h"
#include "imageproc/Binarize.h"
#include "imageproc/BinaryImage.h"
#include "imageproc/GrayImage.h"
#include "imageproc/ConnCompEraser.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QJsonArray>
#include <QPainter>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace unified_content {
namespace {
double median(std::vector<double> v) {
    if(v.empty())return 0;
    auto n=v.size()/2;std::nth_element(v.begin(),v.begin()+n,v.end());
    double value=v[n];
    if(!(v.size()%2))value=(value+*std::max_element(v.begin(),v.begin()+n))/2;
    return value;
}
double mad(std::vector<double> const& values,double center) {
    std::vector<double> d;for(double v:values)d.push_back(std::abs(v-center));return median(d);
}
QRectF normalized(QRectF const& r,QRectF const& crop) {
    return QRectF((r.x()-crop.x())/crop.width(),(r.y()-crop.y())/crop.height(),r.width()/crop.width(),r.height()/crop.height());
}
QRectF denormalized(QRectF const& r,QRectF const& crop) {
    return QRectF(crop.x()+r.x()*crop.width(),crop.y()+r.y()*crop.height(),r.width()*crop.width(),r.height()*crop.height());
}
struct Collector : ContentBoxCollector {
    std::shared_ptr<imageproc::AbstractImageTransform const> transform;
    QRectF rect;
    void process(imageproc::AbstractImageTransform const& t,ContentBox const& box) override {
        transform=std::shared_ptr<imageproc::AbstractImageTransform const>(t.clone().release());rect=box.toTransformedRect(t);
    }
};
struct Model {QRectF envelope,typical;int peers=0;bool manual=false;};
Model model(Sample const& target,std::vector<Sample> const& samples) {
    std::vector<Sample const*> peers,manual;
    double aspect=target.crop.width()/target.crop.height();
    for(auto const& s:samples) {
        if(s.page.id()==target.page.id()||s.page.id().subPage()!=target.page.id().subPage()||s.rect.isEmpty())continue;
        // Different capture orientations / layouts must not vote together.
        if(std::abs(std::log((s.crop.width()/s.crop.height())/aspect))>.14)continue;
        if(s.normalized.width()<.22||s.normalized.height()<.35||s.normalized.width()>1.05||s.normalized.height()>1.05)continue;
        peers.push_back(&s);if(s.params.mode()==MODE_MANUAL)manual.push_back(&s);
    }
    bool useManual=manual.size()>=5;if(useManual)peers=manual;
    size_t const minimum=useManual?5:7;
    if(peers.size()<minimum)return {};
    // Limit to a local capture session while retaining enough votes.
    std::stable_sort(peers.begin(),peers.end(),[&](auto a,auto b){return std::abs(a->ordinal-target.ordinal)<std::abs(b->ordinal-target.ordinal);});
    if(peers.size()>60)peers.resize(60);
    std::vector<double> widths,heights;
    for(auto p:peers){widths.push_back(p->normalized.width());heights.push_back(p->normalized.height());}
    double w=median(widths),h=median(heights);
    // Densest width cluster, rather than an average enlarged by outliers.
    if(!useManual) {
        int best=0;double mode=w;
        for(double x:widths){int count=0;for(double y:widths)if(std::abs(x-y)<.045)++count;if(count>best){best=count;mode=x;}}
        if(best<int(peers.size()*.50))return {};
        w=mode;
    }
    peers.erase(std::remove_if(peers.begin(),peers.end(),[&](auto p){return std::abs(p->normalized.width()-w)>.12||p->normalized.height()<h*.70;}),peers.end());
    if(peers.size()<minimum)return {};
    std::vector<double> edges[4];
    for(auto p:peers){auto r=p->normalized;edges[0].push_back(r.left());edges[1].push_back(r.top());edges[2].push_back(r.right());edges[3].push_back(r.bottom());}
    double e[4],center[4];
    for(int i=0;i<4;++i){double c=median(edges[i]),spread=mad(edges[i],c);center[i]=c;if(spread>.065)return {};double tolerance=std::max(.022,3.5*1.4826*spread);e[i]=c+(i<2?-tolerance:tolerance);}
    return {QRectF(QPointF(e[0],e[1]),QPointF(e[2],e[3])),QRectF(QPointF(center[0],center[1]),QPointF(center[2],center[3])),int(peers.size()),useManual};
}
Proposal examine(Sample const& sample,Model const& learned,int index) {
    Proposal p;p.index=index;p.peers=learned.peers;p.manualReferences=learned.manual;
    QRectF n=sample.normalized,guard=learned.envelope;
    // Translation alone is not an oversized content box. Short chapter / title
    // pages are never expanded to the modal page height.
    bool wide=n.width()>guard.width()+.035,tall=n.height()>guard.height()+.035;
    bool trim[4]={wide&&n.left()<guard.left()-.025,tall&&n.top()<guard.top()-.025,
                  wide&&n.right()>guard.right()+.025,tall&&n.bottom()>guard.bottom()+.025};
    if(!trim[0]&&!trim[1]&&!trim[2]&&!trim[3]){p.index=-1;return p;}
    // The wide statistical envelope detects anomalies; the median edge starts
    // a pixel-checked proposal. Using the wide envelope as the final cut would
    // leave large blank strips and fingers on the page.
    QRectF candidate=sample.rect;auto expected=denormalized(learned.typical.adjusted(-.01,-.01,.01,.01),sample.crop);
    if(trim[0])candidate.setLeft(expected.left());if(trim[1])candidate.setTop(expected.top());
    if(trim[2])candidate.setRight(expected.right());if(trim[3])candidate.setBottom(expected.bottom());
    if(candidate.width()<sample.rect.width()*.55||candidate.height()<sample.rect.height()*.55){p.index=-1;return p;}
    auto transform=sample.transform->clone();
    double scale=std::min(1.,1200./std::max(sample.crop.width(),sample.crop.height()));
    transform->scale(scale,scale);
    QRect target=transform->transformedCropArea().boundingRect().toAlignedRect();
    if(target.width()>1600||target.height()>1600||target.isEmpty()){p.reason=unified_i18n::text("Geometri boyutu olağandışı; elle incele.");return p;}
    QImage source=ImageLoader::load(sample.page.imageId());
    if(source.isNull()){p.reason=unified_i18n::text("Kaynak görüntü okunamadı.");return p;}
    auto ops=std::make_shared<NonAcceleratedOperations>();
    QImage image=transform->materialize(source,target,Qt::white,ops);
    auto toPixel=[&](QRectF r){return QRectF(r.x()*scale-target.x(),r.y()*scale-target.y(),r.width()*scale,r.height()*scale);};
    QRectF old=toPixel(sample.rect),cut=toPixel(candidate);
    auto binary=imageproc::binarizeSauvola(imageproc::GrayImage(image),15);
    imageproc::ConnCompEraser eraser(binary,imageproc::CONN8);
    std::vector<imageproc::ConnComp> components;std::vector<double> heights;
    for(auto cc=eraser.nextConnComp();!cc.isNull();cc=eraser.nextConnComp()){
        if(cc.pixCount()<3)continue;components.push_back(cc);
        if(cut.contains(QRectF(cc.rect()))&&cc.height()>=4&&cc.height()<40&&cc.width()>=2&&cc.width()<cc.height()*2.5&&cc.pixCount()>=6)heights.push_back(cc.height());
    }
    if(heights.size()<60){p.reason=unified_i18n::text("Az metin / özel sayfa: yalnız inceleme.");p.rect=QRectF();}
    else {
        double glyph=median(heights);int outsideGlyphs=0;bool graphic=false;
        // A cut may never bisect a glyph, page number or footnote marker.
        // Absorb connected small print close to the proposed edge, including
        // punctuation, before classifying the removed strip.
        auto textBand=[&](imageproc::ConnComp const& cc){
            double density=double(cc.pixCount())/(cc.width()*cc.height());
            // Faint / typewritten words can form one long component. A width
            // cutoff that only admits individual glyphs silently loses them.
            return cc.height()>=std::max(3.,glyph*.4)&&cc.height()<=glyph*3&&density>.06&&density<.92;
        };
        for(int pass=0;pass<2;++pass)for(auto const& cc:components){
            QRectF r(cc.rect());
            if(!old.intersects(r)||cc.height()>glyph*3||(cc.width()>glyph*12&&!textBand(cc)))continue;
            if(cut.adjusted(-glyph,-glyph,glyph,glyph).intersects(r))cut=cut.united(r.adjusted(-3,-3,3,3)).intersected(old);
        }
        // Keep a character-scale safety margin for faint edge strokes and
        // subpixel sampling; a statistical template is never a razor edge.
        double const padding=std::max(4.,glyph*1.5);
        cut=cut.adjusted(-padding,-padding,padding,padding).intersected(old);
        for(auto const& cc:components){
            QRectF r(cc.rect());if(!old.intersects(r)||cut.contains(r))continue;
            // Dense solid shadows and single thin rules are not text evidence.
            double density=double(cc.pixCount())/(cc.width()*cc.height());
            if(textBand(cc)||(cc.height()>=glyph*.5&&cc.height()<=glyph*2.3&&cc.width()<=glyph*5&&cc.width()>=1&&density>.08&&density<.92))++outsideGlyphs;
            if(cc.width()>glyph*12&&cc.height()>glyph*4&&density>.03&&density<.75)graphic=true;
        }
        p.rect=QRectF((cut.x()+target.x())/scale,(cut.y()+target.y())/scale,cut.width()/scale,cut.height()/scale).intersected(sample.rect);
        double removed=1.-p.rect.width()*p.rect.height()/(sample.rect.width()*sample.rect.height());
        // Uncertain printed matter is always presented for a human decision.
        // Even one surviving glyph prevents unattended application.
        p.safe=outsideGlyphs==0&&!graphic&&removed>.01&&removed<.40;
        p.reason=outsideGlyphs?unified_i18n::text("Taşma şüpheli; dışarıda %1 harf / numara benzeri iz var. Önizleyerek karar ver.").arg(outsideGlyphs):
            graphic?unified_i18n::text("Taşmada görsel / gölge var; önizleyerek karar ver."):
            unified_i18n::text("Tekrarlanan düzene göre fazla kenar; kesilecek alanda metin izi bulunmadı.");
        if(removed<.01){p.index=-1;return p;}
    }
    QPainter painter(&image);painter.setPen(QPen(QColor(215,50,40),2));painter.drawRect(old);
    if(!p.rect.isEmpty()){painter.setPen(QPen(QColor(0,150,80),2,Qt::DashLine));painter.drawRect(toPixel(p.rect));}painter.end();
    p.preview=image.scaled(800,1000,Qt::KeepAspectRatio,Qt::SmoothTransformation);return p;
}
QJsonArray rectJson(QRectF const& r){return {r.x(),r.y(),r.width(),r.height()};}
}
std::vector<Sample> collect(ProjectPages const& pages,StageSequence& stages) {
    auto content=stages.selectContentFilter()->createCacheDrivenTask({});
    auto skew=stages.deskewFilter()->createCacheDrivenTask(content);
    auto split=stages.pageSplitFilter()->createCacheDrivenTask(skew);
    auto task=stages.fixOrientationFilter()->createCacheDrivenTask(split);
    auto sequence=pages.toPageSequence(PAGE_VIEW);std::vector<Sample> samples;
    for(size_t i=0;i<sequence.numPages();++i){auto page=sequence.pageAt(i);Collector c;task->process(page,imageproc::AffineImageTransform(page.metadata().size()),&c);
        auto params=stages.selectContentFilter()->getSettings()->getPageParams(page.id());
        if(!params||!c.transform)continue;auto crop=c.transform->transformedCropArea().boundingRect();if(crop.isEmpty())continue;
        samples.push_back({page,int(i+1),*params,c.transform,crop,c.rect,normalized(c.rect,crop)});
    }return samples;
}
Result review(std::vector<Sample> samples,std::function<bool(int,int)> const& progress,std::vector<Sample> const* references) {
    Result out;out.samples=std::move(samples);auto const& refs=references?*references:out.samples;
    for(size_t i=0;i<out.samples.size();++i){
        if(progress&&!progress(int(i),int(out.samples.size()))){out.cancelled=true;break;}
        auto const& s=out.samples[i];if(s.params.mode()==MODE_MANUAL){++out.manual;continue;}
        if(s.rect.isEmpty())continue;auto learned=model(s,refs);if(!learned.peers){++out.unavailable;continue;}++out.compared;
        try{auto p=examine(s,learned,int(i));if(p.index>=0)out.proposals.push_back(std::move(p));}
        catch(std::exception const& e){Proposal p;p.index=int(i);p.reason=unified_i18n::text(e.what());out.proposals.push_back(std::move(p));}
    }
    if(progress)progress(int(out.samples.size()),int(out.samples.size()));return out;
}
bool apply(Proposal const& p,Sample const& s,StageSequence& stages) {
    auto settings=stages.selectContentFilter()->getSettings();auto current=settings->getPageParams(s.page.id());
    // A result may be stale because the user has edited or rewarped this page.
    if(!current||current->mode()==MODE_MANUAL||!current->dependencies().matches(s.params.dependencies())||p.rect.isEmpty())return false;
    QRectF r=current->contentBox().toTransformedRect(*s.transform);
    if(std::abs(r.x()-s.rect.x())+std::abs(r.y()-s.rect.y())+std::abs(r.width()-s.rect.width())+std::abs(r.height()-s.rect.height())>.05)return false;
    if(!s.rect.adjusted(-.1,-.1,.1,.1).contains(p.rect))return false;
    current->setContentBox(ContentBox(*s.transform,p.rect));current->setContentSizePx(p.rect.size());current->setMode(MODE_MANUAL);
    settings->setPageParams(s.page.id(),*current);stages.pageLayoutFilter()->setContentBox(s.page.id(),p.rect);return true;
}
QJsonObject referenceFor(Sample const& s,std::vector<Sample> const& refs) {
    auto m=model(s,refs);
    if(!m.peers)return {};
    return {{"envelope",rectJson(denormalized(m.envelope,s.crop))},{"peers",m.peers},{"manual",m.manual}};
}
namespace {
bool sameGeometry(Sample const& s,StageSequence& stages) {
    auto content=stages.selectContentFilter()->createCacheDrivenTask({});
    auto skew=stages.deskewFilter()->createCacheDrivenTask(content);
    auto split=stages.pageSplitFilter()->createCacheDrivenTask(skew);
    auto task=stages.fixOrientationFilter()->createCacheDrivenTask(split);
    Collector c;task->process(s.page,imageproc::AffineImageTransform(s.page.metadata().size()),&c);
    return c.transform&&c.transform->fingerprint()==s.transform->fingerprint();
}
bool sameRect(QRectF a,QRectF b){return std::abs(a.x()-b.x())+std::abs(a.y()-b.y())+std::abs(a.width()-b.width())+std::abs(a.height()-b.height())<.05;}
}
bool matchesCurrent(Sample const& s,StageSequence& stages,QRectF const* applied) {
    if(!s.transform||!sameGeometry(s,stages))return false;
    auto now=stages.selectContentFilter()->getSettings()->getPageParams(s.page.id());
    return now&&now->mode()==(applied?MODE_MANUAL:s.params.mode())
        &&now->dependencies().matches(s.params.dependencies())
        &&sameRect(now->contentBox().toTransformedRect(*s.transform),applied?*applied:s.rect);
}
bool applyAi(Proposal const& p,Sample const& s,StageSequence& stages,bool allowManual) {
    if(p.rect.isEmpty()||!s.crop.adjusted(-.1,-.1,.1,.1).contains(p.rect)||!sameGeometry(s,stages))return false;
    auto settings=stages.selectContentFilter()->getSettings();auto now=settings->getPageParams(s.page.id());
    if(!now||now->mode()!=s.params.mode()||(now->mode()==MODE_MANUAL&&!allowManual)||!now->dependencies().matches(s.params.dependencies()))return false;
    if(!sameRect(now->contentBox().toTransformedRect(*s.transform),s.rect))return false;
    now->setContentBox(ContentBox(*s.transform,p.rect));now->setContentSizePx(p.rect.size());now->setMode(MODE_MANUAL);
    settings->setPageParams(s.page.id(),*now);stages.pageLayoutFilter()->setContentBox(s.page.id(),p.rect);return true;
}
bool undoAi(Sample const& s,QRectF const& applied,StageSequence& stages) {
    if(!sameGeometry(s,stages))return false;
    auto settings=stages.selectContentFilter()->getSettings();auto now=settings->getPageParams(s.page.id());
    if(!now||now->mode()!=MODE_MANUAL||!now->dependencies().matches(s.params.dependencies())||!sameRect(now->contentBox().toTransformedRect(*s.transform),applied))return false;
    settings->setPageParams(s.page.id(),s.params);stages.pageLayoutFilter()->setContentBox(s.page.id(),s.rect);return true;
}
QJsonObject Result::toJson() const {
    QJsonArray rows;for(auto const& p:proposals){auto const& s=samples.at(p.index);rows.append(QJsonObject{
        {"page",s.ordinal},{"file",s.page.imageId().filePath()},{"subpage",s.page.id().subPageAsString()},
        {"before",rectJson(s.rect)},{"proposed",rectJson(p.rect)},{"safe",p.safe},{"references",p.peers},{"manual_references",p.manualReferences},{"reason",p.reason}});}
    return {{"algorithm","book-content-consensus-v1"},{"manual_preserved",manual},{"compared",compared},{"insufficient_reference",unavailable},{"cancelled",cancelled},{"proposals",rows}};
}
} // namespace unified_content
