// A saved prepared page must remain prepared when the project is reopened.
#include "dewarping/Curve.h"
#include "dewarping/DewarpingImageTransform.h"
#include "dewarping/DepthPerception.h"
#include "UnifiedContentReview.h"
#include "UnifiedGeometryReview.h"
#include "ProjectReader.h"
#include "ProjectPages.h"
#include "StageSequence.h"
#include "PageSelectionAccessor.h"
#include "PageSelectionProvider.h"
#include "CommandLine.h"
#include "stages/select_content/UnifiedSavedContent.h"
#include <QApplication>
#include <QDomDocument>
#include <QFile>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("Scan Tailor Tests");app.setApplicationName("project-cache");
    CommandLine::set(CommandLine(QStringList{},true));
    try {
        if(argc>1) {
            QFile file(QString::fromLocal8Bit(argv[1]));QDomDocument doc;
            if(!file.open(QIODevice::ReadOnly)||!doc.setContent(&file))throw std::runtime_error("project unreadable");
            ProjectReader reader(doc,file.fileName());
            StageSequence stages(reader.pages(),PageSelectionAccessor(IntrusivePtr<PageSelectionProvider>{}));
            reader.readFilterSettings(stages.filters());
            std::cout<<"PROJECT geometry="<<unified_geometry::collect(*reader.pages(),stages).size()
                <<" content="<<unified_content::collect(*reader.pages(),stages).size()<<std::endl;
        }
        std::vector<QPointF> top,bottom;
        for(int i=0;i<=64;++i) {
            double x=321.123456789+i*21.6123456789;
            top.emplace_back(x,212.234567891+.00003456789*x*x);
            bottom.emplace_back(x,1789.987654321+.00002234567*x*x);
        }
        dewarping::Curve a(top),b(bottom);QDomDocument doc;
        dewarping::Curve savedA(a.toXml(doc,"top")),savedB(b.toXml(doc,"bottom"));
        auto warp=[](dewarping::Curve const& t,dewarping::Curve const& b) {
            return dewarping::DewarpingImageTransform(QSize(2200,2600),QPolygonF(QRectF(0,0,2200,2600)),
                t.polyline(),b.polyline(),0.,dewarping::DepthPerception(),dewarping::DepthPerception(2),dewarping::DepthPerception(2),t.legacyPrecision()||b.legacyPrecision());
        };
        auto before=warp(a,b),after=warp(savedA,savedB);
        if(before.fingerprint()!=after.fingerprint())throw std::runtime_error("saving/reopening unchanged curves invalidated prepared content");
        auto twice=dewarping::Curve(savedA.toXml(doc,"top"));
        if(twice.polyline()!=a.polyline())throw std::runtime_error("curve coordinates did not survive two saves exactly");
        auto legacy=a.toXml(doc,"curve");legacy.firstChildElement("polyline").removeAttribute("float64");
        auto old=dewarping::Curve(legacy);
        if(old.polyline().size()!=top.size()||old.polyline()[0]!=QPointF(float(top[0].x()),float(top[0].y())))
            throw std::runtime_error("legacy float curve compatibility failed");
        auto legacyB=b.toXml(doc,"curve");legacyB.firstChildElement("polyline").removeAttribute("float64");
        auto oldB=dewarping::Curve(legacyB);auto oldWarp=warp(old,oldB);
        auto bounds=before.transformedCropArea().boundingRect();auto rect=bounds.adjusted(bounds.width()*.2,bounds.height()*.2,-bounds.width()*.2,-bounds.height()*.2);
        select_content::Params content(ContentBox(before,rect),rect.size(),select_content::Dependencies(before.fingerprint()),MODE_AUTO);
        if(!select_content::unifiedSavedContentMatches(content,oldWarp))throw std::runtime_error("old precision alone hides the stored content selection");
        auto resaved=dewarping::Curve(old.toXml(doc,"curve"));
        if(!select_content::unifiedSavedContentMatches(content,warp(resaved,oldB)))throw std::runtime_error("resaving an unvisited legacy page loses its stored content");
        auto moved=old.polyline();for(size_t i=0;i<moved.size();++i)moved[i]+=QPointF(0,20*std::sin(3.141592653589793*i/(moved.size()-1)));
        auto changedLegacy=dewarping::Curve(moved).toXml(doc,"curve");changedLegacy.firstChildElement("polyline").removeAttribute("float64");
        if(select_content::unifiedSavedContentMatches(content,warp(dewarping::Curve(changedLegacy),oldB)))throw std::runtime_error("real saved curve edit was mistaken for rounding");
        auto force=deskew::Params(deskew::Dependencies());force.unifiedForceCurves=true;force.setDistortionType(deskew::DistortionType::WARP);
        if(!deskew::Params(force.toXml(doc,"params")).unifiedForceCurves)throw std::runtime_error("force curves choice lost while model is pending");
        top[32]+=QPointF(0,1);
        if(warp(dewarping::Curve(top),b).fingerprint()==before.fingerprint())throw std::runtime_error("real curve edit failed to invalidate content");
        std::cout<<"PASS exact curve round-trip and real edit invalidation\n";return 0;
    } catch(std::exception const& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
