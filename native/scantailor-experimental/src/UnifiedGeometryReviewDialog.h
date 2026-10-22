// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include "UnifiedGeometryReview.h"
#include "UnifiedReviewStore.h"
#include "UnifiedReviewRequeue.h"
#include "UnifiedWorkflow.h"
#include "BasicImageView.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QTimer>
#include <QTemporaryDir>
#include <QSignalBlocker>
#include <QUuid>
#include <future>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <numeric>

class UnifiedGeometryReviewDialog:public QDialog {
public:
    std::function<void(PageId const&)> navigate;
    std::function<void(std::set<PageId> const&)> reprocess;
    UnifiedReviewRequeue m_requeue;
    std::function<void()> completion;
    QString directory()const{return m_directory;}
    std::function<bool(unified_geometry::Sample const&,unified_geometry::Candidate const&,bool)> apply;
    std::function<bool(unified_geometry::Sample const&,unified_geometry::Candidate const&)> undo;
    std::function<std::vector<unified_geometry::Sample>()> refresh;
    std::function<bool(unified_geometry::Sample const&,unified_geometry::Candidate const*)> matches;
    UnifiedGeometryReviewDialog(std::vector<unified_geometry::Sample> samples,PageId selected,QString root,QWidget* parent,QString project={})
        :QDialog(parent),m_samples(std::move(samples)),m_root(std::move(root)) {
        setObjectName("unifiedGeometryReview");setWindowTitle(unified_i18n::text("Geometri · hızlı satır modeli / AI · 0.6.2"));resize(1320,850);
        m_directory=unified_review::directory(m_root,project,"geometry-v2");if(m_directory.isEmpty())m_directory=m_temp.path();
        for(auto const& s:m_samples)m_stamps.push_back(unified_review::sourceStamp(s.page));
        auto outer=new QVBoxLayout(this);auto controls=new QGridLayout;outer->addLayout(controls);
        m_pages=new QComboBox;m_pages->setObjectName("geometryPageChoice");for(size_t i=0;i<m_samples.size();++i){m_pages->addItem(label(m_samples[i]),int(i));if(m_samples[i].page.id()==selected)m_pages->setCurrentIndex(int(i));}controls->addWidget(m_pages,0,0,1,2);
        m_ai=new QCheckBox(unified_i18n::text("Gerekli sayfalarda AI desteği kullan"));m_ai->setChecked(true);controls->addWidget(m_ai,0,2);
        m_device=new QComboBox;m_device->addItem("CPU","cpu");m_device->addItem("GPU / CUDA","cuda");controls->addWidget(m_device,0,3);
        m_single=new QPushButton(unified_i18n::text("Seçili sayfayı çözümle"));m_single->setObjectName("geometryAnalyzeSelected");controls->addWidget(m_single,1,0);
        m_book=new QPushButton(unified_i18n::text("Açık kitabı çözümle"));m_book->setObjectName("geometryAnalyzeBook");controls->addWidget(m_book,1,1);
        m_stop=new QPushButton(unified_i18n::text("Durdur"));m_stop->setEnabled(false);controls->addWidget(m_stop,1,2);
        auto close=new QPushButton(unified_i18n::text("Kapat"));controls->addWidget(close,1,3);
        m_status=new QLabel(unified_i18n::text("Mevcut geometri ve adaylar aynı metin satırlarında ölçülür. Öneriyi uygulayana kadar proje değişmez."));m_status->setObjectName("geometryReviewStatus");m_status->setWordWrap(true);outer->addWidget(m_status);
        m_progress=new QProgressBar;m_progress->setRange(0,1);m_progress->setValue(0);outer->addWidget(m_progress);
        auto split=new QSplitter;outer->addWidget(split,1);m_list=new QTreeWidget;m_list->setObjectName("geometryResults");m_list->setHeaderLabels({unified_i18n::text("Sayfa"),unified_i18n::text("Durum")});m_list->setRootIsDecorated(false);m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);split->addWidget(m_list);
        auto images=new QSplitter;split->addWidget(images);for(int i=0;i<3;++i){auto host=new QWidget;auto box=new QVBoxLayout(host);box->setContentsMargins(0,0,0,0);box->addWidget(new QLabel(unified_i18n::text(i==0?"Kaynak / bölünmüş sayfa":i==1?"Mevcut geometri":"Adayın gerçek ScanTailor çıktısı")));m_images[i]=box;images->addWidget(host);}split->setSizes({240,1080});
        m_candidates=new QComboBox;m_candidates->setObjectName("geometryCandidates");outer->addWidget(m_candidates);
        m_detail=new QLabel;m_detail->setWordWrap(true);m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);outer->addWidget(m_detail);
        auto note=new QLabel(unified_i18n::text("UVDoc üst/alt eğri önerir; burada ve PDF’de ScanTailor aynı eğrileri işler. Katlanan, aşırı esneten, metni dışarıda bırakan aday uygulanamaz. Az metinli kapaklarda otomatik karar verilmez."));note->setWordWrap(true);outer->addWidget(note);
        m_manual=new QCheckBox(unified_i18n::text("Yalnız bu sayfadaki elle çizilmiş eğriyi değiştirmeye izin ver"));m_manual->setObjectName("geometryAllowManual");outer->addWidget(m_manual);
        auto actions=new QGridLayout;outer->addLayout(actions);
        m_apply=new QPushButton(unified_i18n::text("Bu adayı uygula"));m_apply->setObjectName("geometryApply");actions->addWidget(m_apply,0,0);
        auto selectedButton=new QPushButton(unified_i18n::text("Seçili satırların önerilerini uygula"));selectedButton->setObjectName("geometryApplySelected");actions->addWidget(selectedButton,0,1);
        auto all=new QPushButton(unified_i18n::text("Tüm uygun önerileri uygula"));all->setObjectName("geometryApplyAll");actions->addWidget(all,0,2);
        m_undo=new QPushButton(unified_i18n::text("Son uygulama grubunu geri al"));m_undo->setObjectName("geometryUndo");m_undo->setEnabled(false);actions->addWidget(m_undo,1,0);
        auto edit=new QPushButton(unified_i18n::text("Sayfayı eğri düzenleyicide aç"));actions->addWidget(edit,1,1,1,2);
        auto scope=new QLabel(unified_i18n::text("Toplu uygulama yalnız ölçümde iyileşen adayları kullanır. Elle çizilen veya sonradan değişen sayfalar korunur. Çoklu seçim: Ctrl / Shift."));scope->setWordWrap(true);actions->addWidget(scope,2,0,1,3);
        connect(close,&QPushButton::clicked,this,&QDialog::close);
        connect(m_single,&QPushButton::clicked,[this]{start({m_pages->currentData().toInt()});});
        connect(m_book,&QPushButton::clicked,[this]{std::vector<int> ids(m_pages->count());std::iota(ids.begin(),ids.end(),0);start(ids);});
        connect(m_stop,&QPushButton::clicked,[this]{m_cancel=true;m_status->setText(unified_i18n::text("Durduruluyor; bitmiş sonuçlar saklanacak."));});
        connect(m_list,&QTreeWidget::currentItemChanged,[this]{m_manual->setChecked(false);showRow();});
        connect(m_candidates,QOverload<int>::of(&QComboBox::currentIndexChanged),[this]{showCandidate();});
        connect(m_manual,&QCheckBox::toggled,[this]{updateApply();});
        connect(m_apply,&QPushButton::clicked,[this]{applyRows({row()},true);});
        connect(selectedButton,&QPushButton::clicked,[this]{std::vector<int> rows;for(auto item:m_list->selectedItems())rows.push_back(m_list->indexOfTopLevelItem(item));applyRows(rows,false);});
        connect(all,&QPushButton::clicked,[this]{std::vector<int> rows(m_rows.size());std::iota(rows.begin(),rows.end(),0);applyRows(rows,false);});
        connect(m_undo,&QPushButton::clicked,[this]{
            if(m_history.empty()||!undo)return;auto group=m_history.back();m_history.pop_back();int done=0;
            for(auto it=group.rbegin();it!=group.rend();++it){int i=it->row,c=it->candidate;
                if(!currentResult(i)||!m_applied.count(i)||m_applied.at(i)!=c)continue;
                auto const& r=m_rows[i];auto before=m_samples[r.index];
                if(it->previous>=0){before.params=r.candidates[it->previous].params;before.state=unified_geometry::state(before.params);}
                if(!undo(before,r.candidates[c]))continue;
                if(it->previous<0)m_applied.erase(i);else m_applied[i]=it->previous;
                m_list->topLevelItem(i)->setText(1,it->previous<0?unified_i18n::text("Geri alındı"):unified_i18n::text("Uygulandı · ")+r.candidates[it->previous].name);++done;
            }
            persist();m_undo->setEnabled(!m_history.empty());updateApply();m_status->setText(unified_i18n::text("%1 sayfa geri alındı; %2 sonradan değişmiş sayfa korundu.").arg(done).arg(group.size()-done));
        });
        connect(edit,&QPushButton::clicked,[this]{int i=row();if(i>=0&&navigate)navigate(m_samples[m_rows[i].index].page.id());});
        restore();updateApply();m_timer=new QTimer(this);connect(m_timer,&QTimer::timeout,[this]{poll();});m_timer->start(100);
    }
    ~UnifiedGeometryReviewDialog(){m_cancel=true;if(m_future.valid())m_future.wait();poll();persist();}
private:
    friend class UnifiedStageReviewBridge;
    friend class UnifiedGeometryPersistenceTest;
    friend class UnifiedReviewStateTest;
    std::vector<unified_geometry::Sample> m_samples;std::vector<QString> m_stamps;QString m_root,m_directory,m_error;QTemporaryDir m_temp;
    std::vector<unified_geometry::Result> m_rows,m_pending;std::set<int> m_stale;std::map<int,int> m_applied;struct Change {int row,candidate,previous=-1;};std::vector<std::vector<Change>> m_history;
    std::mutex m_mutex;std::future<void> m_future;std::atomic<bool> m_cancel{false};bool m_running=false;int m_received=0,m_pollCount=0;
    QComboBox *m_pages,*m_device,*m_candidates;QCheckBox *m_ai,*m_manual;QPushButton *m_single,*m_book,*m_stop,*m_apply,*m_undo;QTreeWidget* m_list;QLabel *m_status,*m_detail;QProgressBar* m_progress;QTimer* m_timer;
    QVBoxLayout* m_images[3];BasicImageView* m_views[3]={nullptr,nullptr,nullptr};
    static QString label(unified_geometry::Sample const& s){return QString::number(s.ordinal)+" · "+QFileInfo(s.page.imageId().filePath()).fileName()+" · "+s.page.id().subPageAsString();}
    int row()const{return m_list->indexOfTopLevelItem(m_list->currentItem());}
    bool currentResult(int i){
        if(i<0||size_t(i)>=m_rows.size()||m_stale.count(i))return false;
        auto const& r=m_rows[i];auto const& s=m_samples[r.index];auto a=m_applied.find(i);
        if(m_stamps[r.index]==unified_review::sourceStamp(s.page)&&(!matches||matches(s,a==m_applied.end()?nullptr:&r.candidates[a->second])))return true;
        m_stale.insert(i);m_list->topLevelItem(i)->setText(1,unified_i18n::text("Sayfa değişti · yeniden hesaplanmalı"));return false;
    }
    void image(int i,QString const& path){delete m_views[i];m_views[i]=nullptr;QImage im(path);if(!im.isNull()){m_views[i]=new BasicImageView(std::make_shared<NonAcceleratedOperations>(),im);m_images[i]->addWidget(m_views[i]);}}
    void showRow(){int i=row();if(i<0)return;auto const& r=m_rows[i];image(0,r.source);image(1,r.current);{QSignalBlocker b(m_candidates);m_candidates->clear();for(size_t c=0;c<r.candidates.size();++c){auto const& candidate=r.candidates[c];m_candidates->addItem(candidate.name+(int(c)==r.recommended?unified_i18n::text(" · önerilen"):QString())+(!candidate.accepted?unified_i18n::text(" · uygun değil"):QString()),int(c));}int preferred=r.recommended; if(preferred<0)for(size_t c=0;c<r.candidates.size();++c)if(r.candidates[c].accepted&&(preferred<0||r.candidates[c].score<r.candidates[preferred].score))preferred=int(c);m_candidates->setCurrentIndex(preferred>=0?preferred:0);}showCandidate();}
    void showCandidate(){int i=row(),c=m_candidates->currentData().toInt();if(i<0)return;auto const& r=m_rows[i];QString reason=r.error;QString path;if(c>=0&&size_t(c)<r.candidates.size()){auto const& candidate=r.candidates[c];path=candidate.path;if(deskew::unifiedFinite(r.currentError)&&candidate.accepted)reason+=unified_i18n::text("%1 · satır sapması %2 → %3 /1000 · %4").arg(candidate.name).arg(r.currentError,0,'f',2).arg(candidate.error,0,'f',2).arg(candidate.reason);else reason+=candidate.name+" · "+candidate.reason;}image(2,path);m_detail->setText(reason);m_detail->setToolTip(r.report);m_manual->setEnabled(m_samples[r.index].params.mode()==MODE_MANUAL);updateApply();}
    void updateApply(){
        int i=row(),c=m_candidates->currentData().toInt();bool fresh=currentResult(i);
        bool yes=fresh&&c>=0&&size_t(c)<m_rows[i].candidates.size()&&m_rows[i].candidates[c].accepted&&(!m_applied.count(i)||m_applied.at(i)!=c);
        if(yes&&!m_applied.count(i)&&m_samples[m_rows[i].index].params.mode()==MODE_MANUAL&&!m_manual->isChecked())yes=false;
        m_apply->setEnabled(yes);
        if(i>=0&&!fresh)m_detail->setText(unified_i18n::text("Sayfa veya önceki aşamalar değişti. Eski öneri yeniden hesaplanmalı; mevcut eğrilerin korundu."));
    }
    void applyRows(std::vector<int> const& rows,bool single){
        if(!apply)return;std::vector<Change> done;int skip=0;
        for(int i:rows){
            if(i<0||size_t(i)>=m_rows.size())continue;
            auto const& r=m_rows[i];auto const& s=m_samples[r.index];int c=single?m_candidates->currentData().toInt():r.recommended;
            int previous=m_applied.count(i)?m_applied.at(i):-1;bool manual=single&&m_manual->isChecked();
            if(!currentResult(i)||c<0||size_t(c)>=r.candidates.size()||!r.candidates[c].accepted||(previous>=0&&(!single||previous==c))){++skip;continue;}
            auto before=s;
            // Explicitly choosing another candidate replaces only our unchanged
            // earlier result. New manual edits fail currentResult above.
            if(previous>=0){before.params=r.candidates[previous].params;before.state=unified_geometry::state(before.params);manual=true;}
            if((before.params.mode()==MODE_MANUAL&&!manual)||!apply(before,r.candidates[c],manual)){++skip;continue;}
            done.push_back({i,c,previous});m_applied[i]=c;m_list->topLevelItem(i)->setText(1,unified_i18n::text("Uygulandı · ")+r.candidates[c].name);
        }
        if(!done.empty())m_history.push_back(done);persist();m_undo->setEnabled(!m_history.empty());updateApply();
        m_status->setText(unified_i18n::text("%1 sayfa uygulandı; %2 sayfa korundu. Projeyi Ctrl+S ile kaydet; PDF için ‘Kaydet ve PDF’yi güncelle’yi kullan.").arg(done.size()).arg(skip));
    }
    void start(std::vector<int> ids){if(m_running||ids.empty())return;if(m_future.valid())m_future.get();
        if(refresh){auto fresh=refresh();std::vector<int> updated;for(int id:ids){auto page=m_samples[id].page.id();for(auto const& s:fresh)if(s.page.id()==page){updated.push_back(int(m_samples.size()));m_samples.push_back(s);m_stamps.push_back(unified_review::sourceStamp(s.page));break;}}ids=std::move(updated);if(ids.empty())return;}
        m_requeue.begin(ids);m_running=true;m_cancel=false;m_received=0;m_error.clear();m_progress->setRange(0,int(ids.size()));m_progress->setValue(0);m_single->setEnabled(false);m_book->setEnabled(false);m_device->setEnabled(false);m_ai->setEnabled(false);m_stop->setEnabled(true);
        auto dir=m_directory+"/run-"+QUuid::createUuid().toString(QUuid::WithoutBraces);auto device=m_device->currentData().toString();bool ai=m_ai->isChecked();m_status->setText(unified_i18n::text("Geometri karşılaştırılıyor. Her sayfa bitince görünür; pencere kapansa da çalışma ve kayıt sürer."));
        m_future=std::async(std::launch::async,[this,ids,dir,device,ai]{try{unified_geometry::run(m_samples,ids,m_root,device,ai,dir,[this]{return m_cancel.load();},[this](auto r){std::lock_guard<std::mutex> lock(m_mutex);m_pending.push_back(std::move(r));});}catch(std::exception const& e){std::lock_guard<std::mutex> lock(m_mutex);m_error=unified_i18n::text(e.what());}});
    }
    void poll(){
        if(m_running&&matches){m_requeue.inspect(m_samples,[this](auto const& sample){
            if(matches(sample,nullptr))return true;
            for(auto const& applied:m_applied){auto const& r=m_rows[applied.first];if(&m_samples[r.index]==&sample&&matches(sample,&r.candidates[applied.second]))return true;}
            return false;
        });if(m_requeue.size())m_status->setText(unified_workflow::text("Elle değişen %1 sayfa güncel ayarıyla yeniden sıraya alındı.","%1 manually edited pages queued again with current settings.").arg(m_requeue.size()));}
        std::vector<unified_geometry::Result> pending;{std::lock_guard<std::mutex> lock(m_mutex);pending.swap(m_pending);}for(auto& r:pending){auto text=!r.error.isEmpty()?unified_i18n::text("Hata"):r.recommended>=0?unified_i18n::text("Öneri hazır"):unified_i18n::text("Mevcut geometri korunsun");auto title=label(m_samples[r.index]);m_rows.push_back(std::move(r));new QTreeWidgetItem(m_list,{title,text});m_progress->setValue(++m_received);if(!m_list->currentItem())m_list->setCurrentItem(m_list->topLevelItem(0));}if(!pending.empty())persist();
        if(++m_pollCount%4==0&&isVisible())updateApply();
        if(m_running&&m_future.valid()&&m_future.wait_for(std::chrono::seconds(0))==std::future_status::ready){m_future.get();m_running=false;m_single->setEnabled(true);m_book->setEnabled(true);m_device->setEnabled(true);m_ai->setEnabled(true);m_stop->setEnabled(false);m_status->setText(m_error.isEmpty()?unified_i18n::text("Analiz %1. %2 sonuç kaydedildi; uygulanan satırlar korunuyor.").arg(m_cancel?unified_i18n::text("durduruldu"):unified_i18n::text("tamamlandı")).arg(m_received):m_error);auto changed=m_requeue.take();auto onComplete=std::move(completion);completion={};if(!m_cancel&&onComplete)onComplete();if(!m_cancel&&!changed.empty()&&reprocess)reprocess(changed);}}
    void persist(){if(m_rows.empty())return;QJsonArray rows,history;for(size_t i=0;i<m_rows.size();++i){auto const& r=m_rows[i];auto const& s=m_samples[r.index];QJsonArray candidates;for(auto const& c:r.candidates)candidates.append(QJsonObject{{"name",c.name},{"path",c.path},{"reason",c.reason},{"params",unified_geometry::state(c.params)},{"error",c.error},{"score",c.score},{"accepted",c.accepted}});rows.append(QJsonObject{{"key",unified_review::key(s.page)},{"stamp",m_stamps[r.index]},{"transform",s.transform.fingerprint()},{"params",s.state},{"source",r.source},{"current",r.current},{"error",r.error},{"report",r.report},{"seconds",r.seconds},{"currentError",r.currentError},{"recommended",r.recommended},{"candidates",candidates},{"applied",m_applied.count(int(i))?m_applied[int(i)]:-1},{"stale",bool(m_stale.count(int(i)))}});}for(auto const& group:m_history){QJsonArray g;for(auto p:group)g.append(QJsonArray{p.row,p.candidate,p.previous});history.append(g);}if(!unified_review::save(m_directory+"/session.json",{{"schema",1},{"rows",rows},{"history",history},{"selected",row()}}))m_status->setText(unified_i18n::text("İnceleme kaydı yazılamadı; sonuçlar yalnız bellekte."));}
    void restore(){auto saved=unified_review::load(m_directory+"/session.json");if(saved.value("schema").toInt()!=1)return;QMap<int,int> mapped;int old=-1;
        for(auto value:saved.value("rows").toArray()){++old;auto o=value.toObject();int index=-1;for(size_t j=0;j<m_samples.size();++j)if(unified_review::key(m_samples[j].page)==o.value("key").toString()){index=int(j);break;}if(index<0)continue;auto s=m_samples[index];auto el=unified_review::element(o.value("params").toString());if(el.isNull())continue;
            unified_geometry::Result r;r.index=int(m_samples.size());r.source=o.value("source").toString();r.current=o.value("current").toString();r.error=o.value("error").toString();r.report=o.value("report").toString();r.seconds=o.value("seconds").toDouble();r.currentError=o.value("currentError").toDouble();r.recommended=o.value("recommended").toInt(-1);
            for(auto val:o.value("candidates").toArray()){auto c=val.toObject();auto ce=unified_review::element(c.value("params").toString());if(ce.isNull())continue;r.candidates.push_back({c.value("name").toString(),c.value("path").toString(),c.value("reason").toString(),deskew::Params(ce),{},c.value("error").toDouble(),c.value("score").toDouble(),c.value("accepted").toBool()});}
            int a=o.value("applied").toInt(-1);bool same=s.transform.fingerprint()==o.value("transform").toString()&&m_stamps[index]==o.value("stamp").toString();bool applied=same&&a>=0&&size_t(a)<r.candidates.size()&&s.state==unified_geometry::state(r.candidates[a].params);bool fresh=same&&s.state==o.value("params").toString();int i=int(m_rows.size());
            if(o.value("stale").toBool()||(!fresh&&!applied))m_stale.insert(i);if(applied)m_applied[i]=a;s.params=deskew::Params(el);s.state=unified_geometry::state(s.params);m_samples.push_back(s);m_stamps.push_back(o.value("stamp").toString());m_rows.push_back(std::move(r));mapped[old]=i;new QTreeWidgetItem(m_list,{label(s),m_stale.count(i)?unified_i18n::text("Sayfa değişti · yeniden çözümle"):applied?unified_i18n::text("Uygulandı"):unified_i18n::text("Kaydedilmiş sonuç")});}
        for(auto value:saved.value("history").toArray()){std::vector<Change> group;for(auto v:value.toArray()){auto p=v.toArray();if(p.size()>=2&&mapped.contains(p[0].toInt())&&m_applied.count(mapped[p[0].toInt()])){int i=mapped[p[0].toInt()],c=p[1].toInt(-1),previous=p.size()>2?p[2].toInt(-1):-1;if(c>=0&&size_t(c)<m_rows[i].candidates.size()&&previous>=-1&&previous<int(m_rows[i].candidates.size()))group.push_back({i,c,previous});}}if(!group.empty())m_history.push_back(group);}m_undo->setEnabled(!m_history.empty());if(!m_rows.empty()){m_list->setCurrentItem(m_list->topLevelItem(qBound(0,mapped.value(saved.value("selected").toInt(),0),int(m_rows.size()-1))));m_status->setText(unified_i18n::text("%1 kayıtlı geometri sonucu geri yüklendi. %2 değişmiş sayfa korunuyor.").arg(m_rows.size()).arg(m_stale.size()));}}
};
