// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedStageReviewBar.h"
#include "UnifiedAiReviewDialog.h"
#include "UnifiedGeometryReviewDialog.h"

// The stage and detailed window share one review session, including its
// persisted proposals and undo groups. Actions resolve PageId, never a row
// number from the thumbnail's current sort order.
class UnifiedStageReviewBridge {
    template<class D> static std::vector<int> rows(D* d,std::set<PageId> const& pages,bool all){
        std::set<PageId> seen;std::vector<int> ids;
        for(int i=int(d->m_rows.size())-1;i>=0;--i){auto p=d->m_samples[d->m_rows[i].index].page.id();if(!seen.insert(p).second)continue;if(all||pages.count(p))ids.push_back(i);}
        std::reverse(ids.begin(),ids.end());return ids;
    }
    template<class D> static int row(D* d,PageId const& p){auto r=rows(d,{p},false);return r.empty()?-1:r.back();}
    template<class D> static bool fresh(D* d,int i){return d->currentResult(i);}
    template<class D> static UnifiedStageReviewState common(D* d,PageId const& page){
        UnifiedStageReviewState s;s.page=page.isNull()?QString():QFileInfo(page.imageId().filePath()).fileName()+" · "+page.subPageAsString();s.running=d->m_running;s.undo=!d->m_history.empty();s.done=d->m_progress->value();s.total=d->m_progress->maximum();s.detail=s.running?d->m_status->text():QString::fromUtf8("Önce öneri hazırla. Beğenirsen uygula.");
        return s;
    }
public:
    template<class D> static QString analyze(D* d,std::set<PageId> const& pages,bool all,QString const& device){
        if(d->m_running)return QString::fromUtf8("Analiz devam ediyor.");
        // Include pages which became ready since the review window was created.
        if(d->refresh){auto freshSamples=d->refresh();for(auto const& sample:freshSamples){bool found=false;for(auto const& old:d->m_samples)if(old.page.id()==sample.page.id()){found=true;break;}if(!found){d->m_samples.push_back(sample);d->m_stamps.push_back(unified_review::sourceStamp(sample.page));d->m_pages->addItem(QFileInfo(sample.page.imageId().filePath()).fileName(),int(d->m_samples.size()-1));}}}
        std::set<PageId> seen;std::vector<int> ids;
        for(size_t i=0;i<d->m_samples.size();++i){auto p=d->m_samples[i].page.id();if((all||pages.count(p))&&seen.insert(p).second)ids.push_back(int(i));}
        d->m_device->setCurrentIndex(device.startsWith("gpu")?1:0);d->start(ids);
        return ids.empty()?QString::fromUtf8("Bu sayfaların önceki aşamaları hazır değil. Önce seçili aşamayı işle."):d->m_status->text();
    }
    static UnifiedStageReviewState state(UnifiedAiReviewDialog* d,PageId const& page,int=-1,std::set<PageId> const& targets={},bool all=true){
        auto s=common(d,page);int i=row(d,page);s.revision=i;
        for(int j:rows(d,targets,all))if(!d->m_applied.count(j)&&d->m_rows[j].error.isEmpty()&&!d->m_rows[j].proposal.rect.isEmpty()&&d->m_samples[d->m_rows[j].index].params.mode()!=MODE_MANUAL&&fresh(d,j)){s.bulkAvailable=true;break;}
        if(i<0)return s;auto const& r=d->m_rows[i];s.stale=!fresh(d,i);s.manual=d->m_samples[r.index].params.mode()==MODE_MANUAL;s.applied=!s.stale&&d->m_applied.count(i);s.available=!s.stale&&r.error.isEmpty()&&!r.proposal.rect.isEmpty();
        s.previewAvailable=!s.stale&&!r.imagePath.isEmpty();
        if(!r.proposal.rect.isEmpty()){s.choices<<QString::fromUtf8("AI içerik alanı");s.choice=0;s.selectedMethod=s.choices.front();}
        if(s.applied)s.appliedMethod=QString::fromUtf8("AI içerik alanı");
        if(!s.running)s.detail=s.stale?QString::fromUtf8("Geometri, sayfa veya içerik ayarı değişti. Yeniden öneri hazırla; mevcut kutu korundu."):s.applied?QString::fromUtf8("Uygulandı. Ctrl+S ile kaydet; PDF için Kaydet ve PDF’yi güncelle."):!r.error.isEmpty()?QString::fromUtf8("Analiz tamamlanamadı: ")+r.error:r.proposal.rect.isEmpty()?QString::fromUtf8("Öneri yok. ")+(r.proposal.reason.isEmpty()?QString::fromUtf8("Güvenilir içerik alanı bulunamadı; mevcut seçim korundu."):r.proposal.reason):QString::fromUtf8("Öneri hazır; henüz uygulanmadı. ")+r.proposal.reason;
        return s;
    }
    static UnifiedStageReviewState state(UnifiedGeometryReviewDialog* d,PageId const& page,int candidate=-1,std::set<PageId> const& targets={},bool all=true){
        auto s=common(d,page);int i=row(d,page);s.revision=i;
        for(int j:rows(d,targets,all)){auto const& r=d->m_rows[j];if(!d->m_applied.count(j)&&r.recommended>=0&&size_t(r.recommended)<r.candidates.size()&&r.candidates[r.recommended].accepted&&d->m_samples[r.index].params.mode()!=MODE_MANUAL&&fresh(d,j)){s.bulkAvailable=true;break;}}
        if(i<0)return s;auto const& r=d->m_rows[i];s.stale=!fresh(d,i);s.manual=d->m_samples[r.index].params.mode()==MODE_MANUAL;s.applied=!s.stale&&d->m_applied.count(i);
        for(size_t j=0;j<r.candidates.size();++j)s.choices<<r.candidates[j].name+(int(j)==r.recommended?QString::fromUtf8(" · önerilen"):QString())+(!r.candidates[j].accepted?QString::fromUtf8(" · uygun değil"):QString());
        s.choice=r.recommended>=0?r.recommended:0;int c=candidate>=0&&size_t(candidate)<r.candidates.size()?candidate:s.choice;
        bool valid=c>=0&&size_t(c)<r.candidates.size();
        s.available=!s.stale&&valid&&r.candidates[c].accepted;
        s.previewAvailable=!s.stale&&valid&&!r.candidates[c].path.isEmpty();
        if(valid)s.selectedMethod=r.candidates[c].name;
        if(s.applied){s.appliedMethod=r.candidates[d->m_applied.at(i)].name;s.manual=false;s.applied=c==d->m_applied.at(i);}
        if(!s.running)s.detail=s.stale?QString::fromUtf8("Geometri, yön veya bölme değişti. Eski öneri yeniden hesaplanmalı; mevcut ayarın korundu."):s.applied?QString::fromUtf8("Seçili aday bu sayfaya uygulandı. Eğrileri ana görüntüde elle de düzenleyebilirsin."):!s.appliedMethod.isEmpty()?QString::fromUtf8("Seçili aday farklı. ▶ Uygula bu sayfada önceki adayın yerine geçer; geri alabilirsin."):!r.error.isEmpty()?QString::fromUtf8("Analiz tamamlanamadı: ")+r.error:valid&&!r.candidates[c].accepted?QString::fromUtf8("Seçili aday uygun değil: ")+r.candidates[c].reason:r.recommended<0?QString::fromUtf8("Öneri yok: yeterli iyileşme doğrulanmadı. Uygun aday varsa tek sayfa için gözle inceleyebilirsin."):QString::fromUtf8("Öneri hazır; henüz uygulanmadı. Önce öneriyi göster.");
        return s;
    }
    static QImage preview(UnifiedAiReviewDialog* d,PageId const& page,int){int i=row(d,page);if(i<0||!fresh(d,i))return {};auto const& r=d->m_rows[i];return unified_ai::preview(r,d->m_samples[r.index],true,true);}
    static QImage preview(UnifiedGeometryReviewDialog* d,PageId const& page,int candidate){int i=row(d,page);if(i<0||!fresh(d,i))return {};auto const& r=d->m_rows[i];return candidate>=0&&size_t(candidate)<r.candidates.size()?QImage(r.candidates[candidate].path):QImage();}
    static QString apply(UnifiedAiReviewDialog* d,std::set<PageId> const& pages,bool all,bool single,bool manual,int){auto ids=rows(d,pages,all);d->applyRows(ids,single&&manual);return d->m_detail->text();}
    static QString apply(UnifiedGeometryReviewDialog* d,std::set<PageId> const& pages,bool all,bool single,bool manual,int candidate){auto ids=rows(d,pages,all);if(single&&!ids.empty()){d->m_list->setCurrentItem(d->m_list->topLevelItem(ids.front()));d->m_candidates->setCurrentIndex(candidate);d->m_manual->setChecked(manual);}d->applyRows(ids,single);return d->m_status->text();}
    static QString undo(UnifiedAiReviewDialog* d){d->m_undo->click();return d->m_detail->text();}
    static QString undo(UnifiedGeometryReviewDialog* d){d->m_undo->click();return d->m_status->text();}
    template<class D> static void stop(D* d){d->m_stop->click();}
};
