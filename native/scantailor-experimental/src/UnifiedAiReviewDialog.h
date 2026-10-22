// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include "UnifiedAiReview.h"
#include "UnifiedReviewStore.h"
#include "UnifiedReviewRequeue.h"
#include "UnifiedWorkflow.h"
#include "BasicImageView.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QTimer>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>
#include <QJsonArray>
#include <future>
#include <atomic>
#include <mutex>
#include <set>
#include <numeric>

class UnifiedAiReviewDialog : public QDialog {
public:
    std::function<void(PageId const&)> navigate;
    std::function<void(std::set<PageId> const&)> reprocess;
    UnifiedReviewRequeue m_requeue;
    std::function<void()> completion;
    QString directory()const{return m_directory;}
    std::function<bool(unified_content::Proposal const&,unified_content::Sample const&,bool)> apply;
    std::function<bool(unified_content::Sample const&,QRectF const&)> undo;
    std::function<std::vector<unified_content::Sample>()> refresh;
    std::function<bool(unified_content::Sample const&,QRectF const*)> matches;
    UnifiedAiReviewDialog(std::vector<unified_content::Sample> samples,PageId const& selected,QString root,int pageCount,QWidget* parent,QString project={})
        :QDialog(parent),m_samples(std::move(samples)),m_root(std::move(root)) {
        setObjectName("unifiedAiReview");m_directory=unified_review::directory(m_root,project,"content-v1");if(m_directory.isEmpty())m_directory=m_temp.path();
        for(auto const& s:m_samples)m_stamps.push_back(unified_review::sourceStamp(s.page));
        setWindowTitle(unified_i18n::text("Yapay zekâ ile içerik ve sayfa düzeni · 0.6.2"));resize(1180,800);
        auto outer=new QVBoxLayout(this);
        auto controls=new QGridLayout;outer->addLayout(controls);
        m_pages=new QComboBox;m_pages->setObjectName("aiPageChoice");m_pages->setMinimumWidth(150);m_pages->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        for(size_t i=0;i<m_samples.size();++i){auto const& s=m_samples[i];m_pages->addItem(pageLabel(s),int(i));if(s.page.id()==selected)m_pages->setCurrentIndex(int(i));}
        controls->addWidget(m_pages,0,0,1,3);
        m_device=new QComboBox;m_device->addItem("CPU","cpu");m_device->addItem("GPU / CUDA","gpu:0");m_device->setCurrentIndex(QSettings().value("unified/ocr_device").toString().startsWith("gpu")?1:0);controls->addWidget(m_device,0,3);
        m_single=new QPushButton(unified_i18n::text("Seçili sayfayı çözümle"));m_single->setObjectName("aiAnalyzeSelected");controls->addWidget(m_single,1,0);
        m_book=new QPushButton(unified_i18n::text("Açık kitabı çözümle"));m_book->setObjectName("aiAnalyzeBook");controls->addWidget(m_book,1,1);
        m_stop=new QPushButton(unified_i18n::text("Durdur"));m_stop->setEnabled(false);controls->addWidget(m_stop,1,2);
        auto close=new QPushButton(unified_i18n::text("Kapat"));controls->addWidget(close,1,3);
        m_status=new QLabel(unified_i18n::text("%1 / %2 sayfanın geometri ve içerik ayarları hazır. Öneriler siz uygulayana kadar projeyi değiştirmez.").arg(m_samples.size()).arg(pageCount));m_status->setObjectName("aiReviewStatus");m_status->setWordWrap(true);outer->addWidget(m_status);
        m_progress=new QProgressBar;m_progress->setRange(0,1);m_progress->setValue(0);outer->addWidget(m_progress);
        auto split=new QSplitter;outer->addWidget(split,1);
        m_list=new QTreeWidget;m_list->setObjectName("aiReviewResults");m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);m_list->setHeaderLabels({unified_i18n::text("Sayfa"),unified_i18n::text("Durum")});m_list->setRootIsDecorated(false);m_list->setColumnWidth(0,180);split->addWidget(m_list);
        auto host=new QWidget;m_viewLayout=new QVBoxLayout(host);m_viewLayout->setContentsMargins(0,0,0,0);split->addWidget(host);split->setSizes({330,800});
        auto overlays=new QHBoxLayout;outer->addLayout(overlays);
        m_text=new QCheckBox(unified_i18n::text("Metin bölgeleri"));m_text->setChecked(true);overlays->addWidget(m_text);
        m_layout=new QCheckBox(unified_i18n::text("Başlık / dipnot / tablo tahminleri"));m_layout->setChecked(true);overlays->addWidget(m_layout);
        auto fit=new QPushButton(unified_i18n::text("Görüntüyü sığdır"));overlays->addWidget(fit);overlays->addStretch();
        auto legend=new QLabel(unified_i18n::text("Kırmızı: kayıtlı içerik · Yeşil: öneri · Turuncu: dışarıda kalan metin. Yakınlaştırma: tekerlek. Bölge adları model tahminidir."));legend->setWordWrap(true);outer->addWidget(legend);
        m_detail=new QLabel;m_detail->setObjectName("aiReviewDetail");m_detail->setWordWrap(true);outer->addWidget(m_detail);
        m_manual=new QCheckBox(unified_i18n::text("Seçili sayfadaki elle çizilmiş içerik kutusunu değiştirmeye izin ver"));m_manual->setObjectName("aiAllowManual");outer->addWidget(m_manual);
        auto actions=new QGridLayout;outer->addLayout(actions);
        m_apply=new QPushButton(unified_i18n::text("Bu öneriyi uygula"));m_apply->setObjectName("aiApplyProposal");m_apply->setEnabled(false);actions->addWidget(m_apply,0,0);
        auto applySelected=new QPushButton(unified_i18n::text("Seçili satırları toplu uygula"));applySelected->setObjectName("aiApplySelected");actions->addWidget(applySelected,0,1);
        auto applyAll=new QPushButton(unified_i18n::text("Hazır önerilerin tümünü uygula"));applyAll->setObjectName("aiApplyAll");actions->addWidget(applyAll,0,2);
        auto bulkNote=new QLabel(unified_i18n::text("Çoklu seçim: Ctrl / Shift. Toplu uygulama elle çizilen kutuları ve hatalı sonuçları atlar; tek adımda geri alınabilir."));bulkNote->setWordWrap(true);actions->addWidget(bulkNote,1,0,1,3);
        m_undo=new QPushButton(unified_i18n::text("Son uygulamayı geri al"));m_undo->setObjectName("aiUndoProposal");m_undo->setEnabled(false);actions->addWidget(m_undo,2,0);
        auto edit=new QPushButton(unified_i18n::text("Bu sayfayı ana ekranda düzenle"));actions->addWidget(edit,2,1,1,2);
        connect(m_single,&QPushButton::clicked,[this]{start({m_pages->currentData().toInt()});});
        connect(m_book,&QPushButton::clicked,[this]{std::vector<int> ids(m_pages->count());std::iota(ids.begin(),ids.end(),0);start(ids);});
        connect(m_stop,&QPushButton::clicked,[this]{m_cancel=true;m_status->setText(unified_i18n::text("Analiz durduruluyor; hazır sonuçlar kalacak."));});
        connect(close,&QPushButton::clicked,this,&QDialog::close);
        connect(m_list,&QTreeWidget::currentItemChanged,[this]{m_manual->setChecked(false);showCurrent();});
        connect(m_text,&QCheckBox::toggled,[this]{showCurrent();});connect(m_layout,&QCheckBox::toggled,[this]{showCurrent();});
        connect(m_manual,&QCheckBox::toggled,[this]{updateApply();});
        connect(fit,&QPushButton::clicked,[this]{if(m_view){m_view->setZoomLevel(1.);m_view->moveTowardsIdealPosition(1e20);}});
        connect(m_apply,&QPushButton::clicked,[this]{applyRows({current()},m_manual->isChecked());});
        connect(applySelected,&QPushButton::clicked,[this]{std::vector<int> rows;for(auto item:m_list->selectedItems())rows.push_back(m_list->indexOfTopLevelItem(item));applyRows(rows,false);});
        connect(applyAll,&QPushButton::clicked,[this]{std::vector<int> rows(m_rows.size());std::iota(rows.begin(),rows.end(),0);applyRows(rows,false);});
        connect(m_undo,&QPushButton::clicked,[this]{
            if(m_history.empty()||!undo)return;auto rows=m_history.back();m_history.pop_back();int restored=0,changed=0;
            for(auto it=rows.rbegin();it!=rows.rend();++it){int i=*it;auto const& r=m_rows[i];
                if(!currentResult(i)||!undo(m_samples[r.index],r.proposal.rect)){++changed;continue;}
                ++restored;m_applied.erase(i);m_list->topLevelItem(i)->setText(1,unified_i18n::text("Geri alındı"));}
            persist();
            m_detail->setText(unified_i18n::text("%1 öneri geri alındı; %2 sayfa daha sonra değiştiği için korundu.").arg(restored).arg(changed));m_undo->setEnabled(!m_history.empty());updateApply();});
        connect(edit,&QPushButton::clicked,[this]{int i=current();if(i>=0&&navigate)navigate(m_samples[m_rows[i].index].page.id());});
        restore();m_timer=new QTimer(this);connect(m_timer,&QTimer::timeout,[this]{poll();});m_timer->start(100);
    }
    ~UnifiedAiReviewDialog(){m_cancel=true;if(m_future.valid())m_future.wait();poll();persist();}
private:
    friend class UnifiedStageReviewBridge;
    friend class UnifiedAiPersistenceTest;
    friend class UnifiedReviewStateTest;
    QString m_directory;std::vector<QString> m_stamps;std::set<int> m_stale;
    std::vector<unified_content::Sample> m_samples;QString m_root;QTemporaryDir m_temp;
    std::vector<unified_ai::PageResult> m_rows,m_pending;std::mutex m_mutex;
    std::future<void> m_future;std::atomic<bool> m_cancel{false};QString m_error;
    std::set<int> m_applied;std::vector<std::vector<int>> m_history;
    QComboBox *m_pages,*m_device;QPushButton *m_single,*m_book,*m_stop,*m_apply,*m_undo;
    QCheckBox *m_text,*m_layout,*m_manual;QTreeWidget* m_list;QLabel *m_status,*m_detail;
    QProgressBar* m_progress;QVBoxLayout* m_viewLayout;BasicImageView* m_view=nullptr;QTimer* m_timer;
    bool m_running=false;int m_received=0,m_pollCount=0;
    static QString pageLabel(unified_content::Sample const& s){return QString::number(s.ordinal)+" · "+QFileInfo(s.page.imageId().filePath()).fileName()+" · "+s.page.id().subPageAsString();}
    int current()const{return m_list->indexOfTopLevelItem(m_list->currentItem());}
    bool currentResult(int i){
        if(i<0||size_t(i)>=m_rows.size()||m_stale.count(i))return false;
        auto const& r=m_rows[i];auto const& s=m_samples[r.index];
        if(m_stamps[r.index]==unified_review::sourceStamp(s.page)&&(!matches||matches(s,m_applied.count(i)?&r.proposal.rect:nullptr)))return true;
        m_stale.insert(i);m_list->topLevelItem(i)->setText(1,unified_i18n::text("Sayfa değişti · yeniden hesaplanmalı"));return false;
    }
    void applyRows(std::vector<int> const& rows,bool allowSingleManual){
        if(!apply)return;std::vector<int> done;int skipped=0;
        for(int i:rows){if(i<0||size_t(i)>=m_rows.size())continue;auto const& r=m_rows[i];auto const& s=m_samples[r.index];
            bool manualAllowed=allowSingleManual&&rows.size()==1;
            if(!currentResult(i)||m_applied.count(i)||!r.error.isEmpty()||r.proposal.rect.isEmpty()||(s.params.mode()==MODE_MANUAL&&!manualAllowed)||!apply(r.proposal,s,manualAllowed)){++skipped;continue;}
            done.push_back(i);m_applied.insert(i);m_list->topLevelItem(i)->setText(1,unified_i18n::text("Uygulandı"));}
        if(!done.empty())m_history.push_back(done);
        persist();
        m_undo->setEnabled(!m_history.empty());updateApply();
        m_detail->setText(unified_i18n::text("%1 öneri uygulandı; %2 korunan, geçersiz veya daha önce uygulanmış öneri atlandı. PDF için ‘Kaydet ve PDF’yi güncelle’yi kullan.").arg(done.size()).arg(skipped));
    }
    void updateApply(){int i=current();bool fresh=currentResult(i);bool allowed=fresh&&!m_applied.count(i)&&!m_rows[i].proposal.rect.isEmpty()&&m_rows[i].error.isEmpty();if(allowed&&m_samples[m_rows[i].index].params.mode()==MODE_MANUAL&&!m_manual->isChecked())allowed=false;m_apply->setEnabled(allowed);if(i>=0&&!fresh)m_detail->setText(unified_i18n::text("Sayfa veya önceki aşamalar değişti. Eski öneri yeniden hesaplanmalı; mevcut seçimin korundu."));}
    void showCurrent(){int i=current();if(i<0)return;auto const& r=m_rows[i];auto const& s=m_samples[r.index];delete m_view;m_view=nullptr;
        if(!r.imagePath.isEmpty()){auto im=unified_ai::preview(r,s,m_text->isChecked(),m_layout->isChecked());m_view=new BasicImageView(std::make_shared<NonAcceleratedOperations>(),im);m_viewLayout->addWidget(m_view);}
        m_manual->setEnabled(s.params.mode()==MODE_MANUAL);QStringList classes;for(auto v:r.analysis.value("regions").toArray()){auto label=unified_ai::label(v.toObject().value("label").toString());if(!classes.contains(label))classes<<label;}
        m_detail->setText(r.error.isEmpty()?unified_i18n::text("%1 · %2 sn · %3\n%4").arg(r.analysis.value("device").toString()).arg(r.analysis.value("seconds").toDouble(),0,'f',2).arg(classes.join(", ")).arg(r.proposal.reason):r.error);updateApply();}
    void start(std::vector<int> ids){
        if(m_running||ids.empty()||m_directory.isEmpty())return;
        if(m_future.valid())m_future.get();
        if(refresh){auto fresh=refresh();std::vector<int> updated;for(int id:ids){auto page=m_samples[id].page.id();for(auto const& s:fresh)if(s.page.id()==page){updated.push_back(int(m_samples.size()));m_samples.push_back(s);m_stamps.push_back(unified_review::sourceStamp(s.page));break;}}ids=std::move(updated);if(ids.empty()){m_status->setText(unified_i18n::text("Sayfa geometrisi değişti. Önce içerik aşamasını yeniden işle."));return;}}
        m_requeue.begin(ids);m_cancel=false;m_error.clear();m_received=0;m_progress->setRange(0,int(ids.size()));m_progress->setValue(0);m_running=true;
        m_single->setEnabled(false);m_book->setEnabled(false);m_device->setEnabled(false);m_stop->setEnabled(true);
        m_status->setText(unified_i18n::text("Paddle hazırlanıyor. Her sayfa bitince aşağıya eklenecek; model ilk açılışta daha uzun sürebilir."));
        unified_ai::Options opts{m_root,m_device->currentData().toString(),true};auto directory=m_directory+"/run-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_future=std::async(std::launch::async,[this,ids,opts,directory]{try{unified_ai::run(m_samples,ids,opts,directory,[this]{return m_cancel.load();},[this](unified_ai::PageResult r){std::lock_guard<std::mutex> lock(m_mutex);m_pending.push_back(std::move(r));});}catch(std::exception const& e){std::lock_guard<std::mutex> lock(m_mutex);m_error=unified_i18n::text(e.what());}});
    }
    void poll(){
        if(m_running&&matches)m_requeue.inspect(m_samples,[this](auto const& sample){
            if(matches(sample,nullptr))return true;
            for(int applied:m_applied){auto const& r=m_rows[applied];if(&m_samples[r.index]==&sample&&matches(sample,&r.proposal.rect))return true;}
            return false;
        });
        std::vector<unified_ai::PageResult> pending;{std::lock_guard<std::mutex> lock(m_mutex);pending.swap(m_pending);}
        for(auto& r:pending){auto const& s=m_samples[r.index];new QTreeWidgetItem(m_list,{pageLabel(s),!r.error.isEmpty()?unified_i18n::text("Hata"):r.proposal.rect.isEmpty()?unified_i18n::text("Öneri yok"):s.params.mode()==MODE_MANUAL?unified_i18n::text("Elle seçim korundu"):unified_i18n::text("Öneri hazır")});m_rows.push_back(std::move(r));++m_received;m_progress->setValue(m_received);if(!m_list->currentItem())m_list->setCurrentItem(m_list->topLevelItem(0));}
        if(++m_pollCount%4==0&&isVisible())updateApply();
        if(!pending.empty()){persist();m_status->setText(unified_i18n::text("%1 / %2 sayfa hazır. Sonuçlar kaydediliyor; pencereyi kapatıp açabilirsin.").arg(m_received).arg(m_progress->maximum()));}
        if(m_running&&m_future.valid()&&m_future.wait_for(std::chrono::seconds(0))==std::future_status::ready){m_future.get();m_running=false;m_single->setEnabled(true);m_book->setEnabled(true);m_device->setEnabled(true);m_stop->setEnabled(false);std::lock_guard<std::mutex> lock(m_mutex);m_status->setText(m_error.isEmpty()?(m_cancel?unified_i18n::text("Analiz durduruldu; tamamlanan sonuçlar kaydedildi."):unified_i18n::text("Analiz tamamlandı. Tek / çoklu / tümünü uygula. Sonuçlar yeniden açılınca geri gelir; proje değişiklikleri için Ctrl+S.")):m_error);auto changed=m_requeue.take();auto onComplete=std::move(completion);completion={};if(!m_cancel&&onComplete)onComplete();if(!m_cancel&&!changed.empty()&&reprocess)reprocess(changed);}
        if(m_running&&m_requeue.size())m_status->setText(unified_workflow::text("Elle değişen %1 sayfa güncel ayarıyla yeniden sıraya alındı.","%1 manually edited pages queued again with current settings.").arg(m_requeue.size()));
    }
    void persist(){
        if(m_rows.empty())return;QJsonArray rows,history;
        for(size_t i=0;i<m_rows.size();++i){auto const& r=m_rows[i];auto const& s=m_samples[r.index];rows.append(QJsonObject{{"key",unified_review::key(s.page)},{"stamp",m_stamps[r.index]},{"geometry",s.transform->fingerprint()},{"params",unified_review::xml(s.params)},{"image",r.imagePath},{"error",r.error},{"analysis",r.analysis},{"rect",unified_review::rect(r.proposal.rect)},{"reason",r.proposal.reason},{"pixelCrop",unified_review::rect(r.pixelCrop)},{"scale",r.scale},{"applied",bool(m_applied.count(int(i)))},{"stale",bool(m_stale.count(int(i)))}});}
        for(auto const& group:m_history){QJsonArray g;for(int i:group)g.append(i);history.append(g);}
        if(!unified_review::save(m_directory+"/session.json",{{"schema",1},{"rows",rows},{"history",history},{"selected",current()}}))m_status->setText(unified_i18n::text("İnceleme kaydı yazılamadı; sonuçlar şu anda yalnızca bellekte."));
    }
    void restore(){
        auto saved=unified_review::load(m_directory+"/session.json");if(saved.value("schema").toInt()!=1)return;
        QMap<int,int> mapped;int old=-1;
        for(auto value:saved.value("rows").toArray()){++old;auto o=value.toObject();int index=-1;for(size_t j=0;j<m_samples.size();++j)if(unified_review::key(m_samples[j].page)==o.value("key").toString()){index=int(j);break;}if(index<0)continue;
            auto s=m_samples[index];auto el=unified_review::element(o.value("params").toString());if(el.isNull())continue;
            select_content::Params original(el);unified_ai::PageResult r;r.index=index;r.imagePath=o.value("image").toString();r.error=o.value("error").toString();r.analysis=o.value("analysis").toObject();r.proposal.index=index;r.proposal.rect=unified_review::rect(o.value("rect"));r.proposal.reason=o.value("reason").toString();r.pixelCrop=unified_review::rect(o.value("pixelCrop")).toAlignedRect();r.scale=o.value("scale").toDouble(1);
            bool sameGeometry=s.transform->fingerprint()==o.value("geometry").toString()&&m_stamps[index]==o.value("stamp").toString();
            bool applied=sameGeometry&&o.value("applied").toBool()&&s.params.mode()==MODE_MANUAL&&unified_review::sameRect(s.rect,r.proposal.rect);
            bool fresh=sameGeometry&&unified_review::xml(s.params)==unified_review::xml(original);
            int row=int(m_rows.size());if(o.value("stale").toBool()||(!fresh&&!applied))m_stale.insert(row);
            s.params=original;s.rect=original.contentBox().toTransformedRect(*s.transform);r.index=int(m_samples.size());r.proposal.index=r.index;m_samples.push_back(s);m_stamps.push_back(o.value("stamp").toString());
            if(applied)m_applied.insert(row);m_rows.push_back(std::move(r));mapped.insert(old,row);
            new QTreeWidgetItem(m_list,{pageLabel(s),m_stale.count(row)?unified_i18n::text("Sayfa değişti · yeniden çözümle"):applied?unified_i18n::text("Uygulandı"):unified_i18n::text("Kaydedilmiş öneri")});
        }
        for(auto value:saved.value("history").toArray()){std::vector<int> group;for(auto v:value.toArray())if(mapped.contains(v.toInt())&&m_applied.count(mapped[v.toInt()]))group.push_back(mapped[v.toInt()]);if(!group.empty())m_history.push_back(group);}
        m_undo->setEnabled(!m_history.empty());if(!m_rows.empty()){m_list->setCurrentItem(m_list->topLevelItem(qBound(0,mapped.value(saved.value("selected").toInt(),0),int(m_rows.size()-1))));m_status->setText(unified_i18n::text("%1 kayıtlı sonuç geri yüklendi. %2 değişmiş sayfa korunuyor; gerekirse yeniden çözümle.").arg(m_rows.size()).arg(m_stale.size()));}
    }
};
