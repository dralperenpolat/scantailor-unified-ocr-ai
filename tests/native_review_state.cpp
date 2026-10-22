// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic integration test: live project edits, not mocked freshness flags.
#include "UnifiedStageReviewBridge.h"
#include "ProjectPages.h"
#include "ImageInfo.h"
#include "PageSequence.h"
#include "PageSelectionAccessor.h"
#include "StageSequence.h"
#include "PageSelectionProvider.h"
#include "CommandLine.h"
#include "stages/fix_orientation/Filter.h"
#include "stages/fix_orientation/Settings.h"
#include "stages/page_split/Filter.h"
#include "stages/page_split/Settings.h"
#include "stages/deskew/Filter.h"
#include "stages/deskew/Settings.h"
#include "stages/select_content/Filter.h"
#include "stages/select_content/Settings.h"
#include <QApplication>
#include <QScrollArea>
#include <iostream>

void check(bool yes,char const* why){if(!yes)throw std::runtime_error(why);}
class UnifiedReviewStateTest {
public:
static void run(QString captures){
    QTemporaryDir dir;QSize size(800,1200);QImage im(size,QImage::Format_RGB32);im.fill(Qt::white);
    std::vector<ImageInfo> images;
    for(int n=0;n<4;++n){QString path=dir.path()+QString("/page%1.png").arg(n);check(im.save(path),"fixture image");images.emplace_back(ImageId(path),ImageMetadata(size),1,false,false);}
    IntrusivePtr<ProjectPages> pages(new ProjectPages(images,Qt::LeftToRight));
    StageSequence stages(pages,PageSelectionAccessor(IntrusivePtr<PageSelectionProvider>{}));
    auto seq=pages->toPageSequence(PAGE_VIEW);
    imageproc::AffineImageTransform t(size);QRectF rect(90,110,620,960);
    for(size_t n=0;n<seq.numPages();++n){auto p=seq.pageAt(n);
        page_split::Settings::UpdateAction action;action.setLayoutType(page_split::SINGLE_PAGE_UNCUT);
        action.setParams(page_split::Params(page_split::PageLayout(QRectF(QPointF(),size)),page_split::Dependencies(size,OrthogonalRotation(),page_split::SINGLE_PAGE_UNCUT),MODE_AUTO));
        stages.pageSplitFilter()->getSettings()->updatePage(p.imageId(),action);
        deskew::Params geometry(deskew::Dependencies(t.origCropArea(),OrthogonalRotation()));geometry.setDistortionType(deskew::DistortionType::ROTATION);geometry.getParamsRotation().setCompensationAngleDeg(0);
        stages.deskewFilter()->getSettings()->setPageParams(p.id(),geometry);
        select_content::Params content(ContentBox(t,rect),rect.size(),select_content::Dependencies(t.fingerprint()),n==3?MODE_MANUAL:MODE_AUTO);
        stages.selectContentFilter()->getSettings()->setPageParams(p.id(),content);
    }
    auto samples=unified_content::collect(*pages,stages);check(samples.size()==4,"valid native pipeline fixture");
    {
        UnifiedAiReviewDialog queued(samples,samples[0].page.id(),dir.path(),4,nullptr);
        queued.matches=[&](auto const& s,auto const* applied){return unified_content::matchesCurrent(s,stages,applied);};
        std::set<PageId> repeated;queued.reprocess=[&](auto const& ids){repeated=ids;};
        std::promise<void> worker;queued.m_future=worker.get_future();queued.m_running=true;queued.m_requeue.begin({0,1,2,3});
        auto changed=samples[0].params;changed.setMode(MODE_MANUAL);changed.setContentBox(ContentBox(t,rect.adjusted(5,5,-5,-5)));
        stages.selectContentFilter()->getSettings()->setPageParams(samples[0].page.id(),changed);
        auto geometry=*stages.deskewFilter()->getSettings()->getPageParams(samples[2].page.id());auto original=geometry;geometry.getParamsRotation().setCompensationAngleDeg(2);
        stages.deskewFilter()->getSettings()->setPageParams(samples[2].page.id(),geometry);
        queued.poll();queued.poll();check(queued.m_requeue.size()==2,"content run tracks live manual edits once per page");
        worker.set_value();queued.poll();
        check(repeated==std::set<PageId>{samples[0].page.id(),samples[2].page.id()},"content requeues exactly pages changed during worker run");
        check(unified_review::xml(*stages.selectContentFilter()->getSettings()->getPageParams(samples[0].page.id()))==unified_review::xml(changed),"queue keeps manual content edit");
        stages.selectContentFilter()->getSettings()->setPageParams(samples[0].page.id(),samples[0].params);
        stages.deskewFilter()->getSettings()->setPageParams(samples[2].page.id(),original);
    }
    UnifiedAiReviewDialog content(samples,samples[0].page.id(),dir.path(),4,nullptr);
    content.matches=[&](auto const& s,QRectF const* applied){return unified_content::matchesCurrent(s,stages,applied);};
    content.apply=[&](auto const& p,auto const& s,bool manual){return unified_content::applyAi(p,s,stages,manual);};
    content.undo=[&](auto const& s,auto const& r){return unified_content::undoAi(s,r,stages);};
    for(int n=0;n<4;++n){unified_ai::PageResult r;r.index=n;r.proposal.index=n;r.imagePath=images[n].id().filePath();if(n!=2)r.proposal.rect=rect.adjusted(-10,-10,10,10);content.m_pending.push_back(r);}content.poll();
    auto page=samples[0].page.id();
    auto state=UnifiedStageReviewBridge::state(&content,page);
    check(state.available&&!state.applied,"new proposal available, not applied");
    UnifiedStageReviewBridge::apply(&content,{page},false,true,false,0);
    state=UnifiedStageReviewBridge::state(&content,page);
    check(state.applied&&!state.appliedMethod.isEmpty(),"applied state validated against live content");
    auto geometry=stages.deskewFilter()->getSettings()->getPageParams(page);geometry->getParamsRotation().setCompensationAngleDeg(3);
    stages.deskewFilter()->getSettings()->setPageParams(page,*geometry);
    state=UnifiedStageReviewBridge::state(&content,page);
    check(state.stale&&!state.applied&&!state.available&&!state.previewAvailable,"geometry edit immediately invalidates applied content");
    check(content.m_list->topLevelItem(0)->text(1).contains(QString::fromUtf8("yeniden")),"detail list invalidated without reopening");
    auto old=stages.selectContentFilter()->getSettings()->getPageParams(page);QString saved=unified_review::xml(*old);
    UnifiedStageReviewBridge::undo(&content);
    check(unified_review::xml(*stages.selectContentFilter()->getSettings()->getPageParams(page))==saved,"stale undo cannot overwrite edits");
    state=UnifiedStageReviewBridge::state(&content,samples[2].page.id(),-1,{samples[2].page.id()},false);
    check(!state.available&&!state.bulkAvailable&&state.detail.contains(QString::fromUtf8("Öneri yok")),"empty proposal is not Ready; scope excludes other pages");
    UnifiedStageReviewBridge::apply(&content,{},true,false,false,0);
    check(content.m_applied.count(1)&&!content.m_applied.count(2)&&!content.m_applied.count(3),"bulk protects empty, stale and manual content");

    auto gs=unified_geometry::collect(*pages,stages);check(gs.size()==4,"geometry fixture");
    {
        UnifiedGeometryReviewDialog queued(gs,gs[0].page.id(),dir.path(),nullptr);
        queued.matches=[&](auto const& s,auto const* candidate){return unified_geometry::matchesCurrent(s,stages,candidate);};
        queued.apply=[&](auto const& s,auto const& candidate,bool manual){return unified_geometry::apply(s,candidate,stages,manual);};
        std::set<PageId> repeated;queued.reprocess=[&](auto const& ids){repeated=ids;};
        unified_geometry::Result candidate;candidate.index=1;candidate.recommended=0;auto fixed=gs[1].params;fixed.getParamsRotation().setCompensationAngleDeg(1);fixed.getParamsRotation().setMode(MODE_MANUAL);
        candidate.source=images[1].id().filePath();candidate.current=candidate.source;
        candidate.candidates.push_back({"Metin",candidate.source, {},fixed,{},0,0,true});queued.m_pending.push_back(candidate);queued.poll();
        UnifiedStageReviewBridge::optimize(&queued,{gs[1].page.id()},false,"cpu");
        check(!queued.m_running&&queued.m_applied.size()==1,"optimize reuses and applies a fresh useful result without running AI again");
        std::promise<void> worker;queued.m_future=worker.get_future();queued.m_running=true;queued.m_requeue.begin({0,1,2,3});
        auto manual=gs[0].params;manual.getParamsRotation().setCompensationAngleDeg(4);manual.getParamsRotation().setMode(MODE_MANUAL);
        stages.deskewFilter()->getSettings()->setPageParams(gs[0].page.id(),manual);
        queued.poll();check(queued.m_requeue.size()==1,"geometry tracks manual edits but not its own applied result");
        worker.set_value();queued.poll();check(repeated==std::set<PageId>{gs[0].page.id()},"geometry requeues only edited identity");
        check(unified_geometry::state(*stages.deskewFilter()->getSettings()->getPageParams(gs[0].page.id()))==unified_geometry::state(manual),"geometry queue preserves manual curve");
        for(int i:{0,1})stages.deskewFilter()->getSettings()->setPageParams(gs[i].page.id(),gs[i].params);
    }
    UnifiedGeometryReviewDialog geo(gs,gs[1].page.id(),dir.path(),nullptr);
    geo.matches=[&](auto const& s,auto const* c){return unified_geometry::matchesCurrent(s,stages,c);};
    geo.apply=[&](auto const& s,auto const& c,bool manual){return unified_geometry::apply(s,c,stages,manual);};
    geo.undo=[&](auto const& s,auto const& c){return unified_geometry::undo(s,c,stages);};
    unified_geometry::Result r;r.index=1;r.recommended=0;
    for(int n=0;n<2;++n){auto p=gs[1].params;p.getParamsRotation().setCompensationAngleDeg(n+1);p.getParamsRotation().setMode(MODE_MANUAL);r.candidates.push_back({n==0?"Metin":"Kenar",images[1].id().filePath(),{},p,{},0,0,true});}
    geo.m_pending.push_back(r);geo.poll();page=gs[1].page.id();
    UnifiedStageReviewBar bar(true,nullptr);
    bar.action=[&](auto a){if(a==UnifiedStageReviewBar::Changed)bar.showState(UnifiedStageReviewBridge::state(&geo,page,bar.candidate()));if(a==UnifiedStageReviewBar::Apply)UnifiedStageReviewBridge::apply(&geo,{page},false,true,false,bar.candidate());};
    bar.showState(UnifiedStageReviewBridge::state(&geo,page));
    bar.findChild<QComboBox*>("stageReviewCandidate")->setCurrentIndex(1);bar.trigger(UnifiedStageReviewBar::Apply);
    check(geo.m_applied.at(0)==1,"play applies selected candidate, not the recommended candidate");
    bar.showState(UnifiedStageReviewBridge::state(&geo,page,1));
    bar.findChild<QComboBox*>("stageReviewCandidate")->setCurrentIndex(0);
    state=UnifiedStageReviewBridge::state(&geo,page,0);
    check(state.selectedMethod=="Metin"&&state.appliedMethod=="Kenar","selection and actual applied method distinguished");
    check(bar.findChild<QLabel*>("stageReviewApplied")->text().contains("Kenar"),"changing candidate refreshes status immediately");
    check(bar.findChild<QPushButton*>("stageReviewApply")->isEnabled(),"play enables newly selected candidate");
    bar.trigger(UnifiedStageReviewBar::Apply);check(geo.m_applied.at(0)==0,"new play replaces previous candidate");
    {
        // Reopen the persisted session with current project parameters. Both
        // replacement and initial application remain separately undoable.
        auto saved=*stages.deskewFilter()->getSettings()->getPageParams(page);
        UnifiedGeometryReviewDialog reopened(unified_geometry::collect(*pages,stages),page,dir.path(),nullptr);
        check(QFile::copy(geo.m_directory+"/session.json",reopened.m_directory+"/session.json"),"saved session fixture");
        reopened.restore();reopened.matches=geo.matches;reopened.undo=geo.undo;
        check(reopened.m_applied.at(0)==0&&reopened.m_history.size()==2,"reopen preserves replacement history");
        UnifiedStageReviewBridge::undo(&reopened);check(reopened.m_applied.at(0)==1,"reopened undo restores former candidate");
        UnifiedStageReviewBridge::undo(&reopened);check(reopened.m_applied.empty(),"reopened second undo restores original geometry");
        stages.deskewFilter()->getSettings()->setPageParams(page,saved);
    }
    UnifiedStageReviewBridge::undo(&geo);check(geo.m_applied.at(0)==1,"undo restores previous candidate");
    auto applied=stages.deskewFilter()->getSettings()->getPageParams(page);applied->unifiedWarning="diagnostic only";stages.deskewFilter()->getSettings()->setPageParams(page,*applied);
    check(UnifiedStageReviewBridge::state(&geo,page,1).applied,"diagnostic update does not invalidate geometry");
    QDir().mkpath(captures);QScrollArea scroll;scroll.resize(370,950);scroll.setWidgetResizable(true);scroll.setWidget(&bar);scroll.show();QApplication::processEvents();check(scroll.grab().save(captures+"/stage-play-controls.png"),"UI capture");scroll.takeWidget();
    OrthogonalRotation rotation;rotation.nextClockwiseDirection();stages.fixOrientationFilter()->getSettings()->applyRotation(page.imageId(),rotation);
    state=UnifiedStageReviewBridge::state(&geo,page);
    check(state.stale&&!state.applied&&!state.available,"upstream orientation invalidates geometry");
    check(!unified_geometry::undo(gs[1],r.candidates[1],stages),"geometry undo protects changed upstream orientation");
    std::cout<<"PASS live geometry/content invalidation, scoped bulk, no proposal, selected play, applied identity and undo guards\n";
}
};
int main(int argc,char** argv){QApplication app(argc,argv);app.setOrganizationName("ScanTailor Tests");app.setApplicationName("review-state");CommandLine::set(CommandLine(QStringList{},true));try{UnifiedReviewStateTest::run(argc>1?QString::fromLocal8Bit(argv[1]):QDir::tempPath());return 0;}catch(std::exception const& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
