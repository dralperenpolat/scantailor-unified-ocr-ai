// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include "UnifiedContentReview.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QTimer>
#include <QCloseEvent>
#include <QFileInfo>
#include <future>
#include <atomic>
#include <set>
#include <numeric>

class UnifiedContentReviewDialog : public QDialog {
public:
    std::function<void(PageId const&)> navigate;
    std::function<bool(unified_content::Proposal const&,unified_content::Sample const&)> apply;
    std::function<bool(unified_content::Sample const&,QRectF const&)> undo;
    UnifiedContentReviewDialog(std::vector<unified_content::Sample> samples,int pageCount,QWidget* parent):QDialog(parent) {
        setObjectName("unifiedBookReview");setWindowTitle(unified_i18n::text("Kitap düzenine göre kenarları düzelt"));resize(1100,750);
        auto layout=new QVBoxLayout(this);m_status=new QLabel;m_status->setWordWrap(true);layout->addWidget(m_status);
        m_progress=new QProgressBar;m_progress->setRange(0,int(samples.size()));layout->addWidget(m_progress);
        auto split=new QSplitter;layout->addWidget(split,1);
        m_list=new QTreeWidget;m_list->setHeaderLabels({unified_i18n::text("Sayfa / durum"),unified_i18n::text("Gerekçe")});m_list->setRootIsDecorated(false);m_list->setColumnWidth(0,230);m_list->header()->setStretchLastSection(true);split->addWidget(m_list);
        m_list->setObjectName("bookReviewResults");m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        auto scroll=new QScrollArea;scroll->setWidgetResizable(true);m_preview=new QLabel;m_preview->setAlignment(Qt::AlignCenter);scroll->setWidget(m_preview);split->addWidget(scroll);split->setSizes({450,650});
        auto legend=new QLabel(unified_i18n::text("Kırmızı: mevcut seçim · Yeşil kesikli: öneri. Sol / sağ sayfalar ve farklı çekim oranları ayrı karşılaştırılır.\nÇoklu seçim: Ctrl / Shift. Elle seçimler korunur. Şüpheli öneriler yalnız tek tek veya aşağıdaki izni açtığında seçili gruba uygulanır; ‘Tüm güvenli’ bunları her zaman atlar."));legend->setWordWrap(true);layout->addWidget(legend);
        m_allowSuspect=new QCheckBox(unified_i18n::text("Seçtiğim şüpheli önerileri de inceledim; toplu uygulanabilir"));m_allowSuspect->setObjectName("bookAllowSuspect");layout->addWidget(m_allowSuspect);
        auto bar=new QGridLayout;layout->addLayout(bar);
        m_one=new QPushButton(unified_i18n::text("Bu öneriyi uygula"));m_safe=new QPushButton(unified_i18n::text("Tüm güvenli önerileri uygula"));m_undo=new QPushButton(unified_i18n::text("Son grubu geri al"));auto edit=new QPushButton(unified_i18n::text("Ana ekranda düzenle"));auto close=new QPushButton(unified_i18n::text("Kapat"));
        auto selected=new QPushButton(unified_i18n::text("Seçili önerileri toplu uygula"));selected->setObjectName("bookApplySelected");m_one->setObjectName("bookApplyOne");m_safe->setObjectName("bookApplyAll");m_undo->setObjectName("bookUndo");
        bar->addWidget(m_one,0,0);bar->addWidget(selected,0,1);bar->addWidget(m_safe,0,2);bar->addWidget(m_undo,1,0);bar->addWidget(edit,1,1);bar->addWidget(close,1,2);
        m_one->setEnabled(false);m_safe->setEnabled(false);m_undo->setEnabled(false);
        connect(close,&QPushButton::clicked,this,&QDialog::close);
        connect(edit,&QPushButton::clicked,[this]{int i=current();if(i>=0&&navigate){navigate(m_result.samples[m_result.proposals[i].index].page.id());hide();}});
        connect(m_list,&QTreeWidget::currentItemChanged,[this]{showCurrent();});
        connect(m_one,&QPushButton::clicked,[this]{applyRows({current()},true);});
        connect(selected,&QPushButton::clicked,[this]{std::vector<int> rows;for(auto item:m_list->selectedItems())rows.push_back(m_list->indexOfTopLevelItem(item));applyRows(rows,m_allowSuspect->isChecked());});
        connect(m_safe,&QPushButton::clicked,[this]{std::vector<int> rows(m_result.proposals.size());std::iota(rows.begin(),rows.end(),0);applyRows(rows,false);});
        connect(m_undo,&QPushButton::clicked,[this]{if(m_history.empty()||!undo)return;auto group=m_history.back();m_history.pop_back();int restored=0;for(auto it=group.rbegin();it!=group.rend();++it){auto const& p=m_result.proposals[*it];if(undo(m_result.samples[p.index],p.rect)){m_done.erase(*it);m_list->topLevelItem(*it)->setText(0,label(*it)+unified_i18n::text(" · Geri alındı"));++restored;}}m_undo->setEnabled(!m_history.empty());showCurrent();m_status->setText(unified_i18n::text("%1 öneri geri alındı; %2 sonradan değişmiş sayfa korundu.").arg(restored).arg(group.size()-restored));});
        m_missing=pageCount-int(samples.size());
        m_status->setText(unified_i18n::text("Kitaptaki seçimler karşılaştırılıyor; yalnız şüpheli sayfaların görüntüsü okunuyor…"));
        m_future=std::async(std::launch::async,[this,samples=std::move(samples)]() mutable {
            return unified_content::review(std::move(samples),[this](int n,int){m_processed=n;return !m_cancel.load();});
        });
        auto timer=new QTimer(this);connect(timer,&QTimer::timeout,[this,timer]{
            m_progress->setValue(m_processed.load());
            if(m_future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            timer->stop();try{m_result=m_future.get();}catch(std::exception const& e){m_status->setText(unified_i18n::text(e.what()));return;}
            m_status->setText(unified_i18n::text("%1 şüpheli seçim · %2 elle seçim korundu · %3 otomatik sayfa karşılaştırıldı · %4 sayfada yeterli benzer örnek yok · %5 sayfanın önceki aşamaları hazır değil.").arg(m_result.proposals.size()).arg(m_result.manual).arg(m_result.compared).arg(m_result.unavailable).arg(m_missing));
            for(size_t i=0;i<m_result.proposals.size();++i){auto const& p=m_result.proposals[i];auto item=new QTreeWidgetItem(m_list,{label(int(i)),p.reason+unified_i18n::text(" (%1 %2 örnek)").arg(p.peers).arg(p.manualReferences?unified_i18n::text("elle düzeltilmiş"):unified_i18n::text("benzer"))});item->setToolTip(1,item->text(1));}
            m_safe->setEnabled(std::any_of(m_result.proposals.begin(),m_result.proposals.end(),[](auto const& p){return p.safe;}));
            if(m_list->topLevelItemCount())m_list->setCurrentItem(m_list->topLevelItem(0));
        });timer->start(100);
    }
    ~UnifiedContentReviewDialog(){m_cancel=true;if(m_future.valid())m_future.wait();}
protected:
    void closeEvent(QCloseEvent* e) override {QDialog::closeEvent(e);}
private:
    friend class UnifiedBookBulkTest;
    unified_content::Result m_result;
    std::future<unified_content::Result> m_future;
    std::atomic<int> m_processed{0};std::atomic<bool> m_cancel{false};
    int m_missing=0;std::vector<std::vector<int>> m_history;std::set<int> m_done;QCheckBox* m_allowSuspect;
    QLabel *m_status,*m_preview;QTreeWidget* m_list;QProgressBar* m_progress;QPushButton *m_one,*m_safe,*m_undo;
    int current() const {return m_list->indexOfTopLevelItem(m_list->currentItem());}
    QString label(int i)const {auto const& s=m_result.samples[m_result.proposals[i].index];return QString::number(s.ordinal)+" · "+QFileInfo(s.page.imageId().filePath()).fileName()+" · "+s.page.id().subPageAsString();}
    void showCurrent(){int i=current();if(i<0)return;auto const& p=m_result.proposals[i];m_preview->setPixmap(QPixmap::fromImage(p.preview));m_one->setEnabled(!m_done.count(i)&&!p.rect.isEmpty());}
    void applyRows(std::vector<int> const& rows,bool allowSuspect){if(!apply)return;std::vector<int> group;int skipped=0;for(int i:rows){if(i<0||size_t(i)>=m_result.proposals.size())continue;auto const& p=m_result.proposals[i];if(m_done.count(i)||p.rect.isEmpty()||(!p.safe&&!allowSuspect)||!apply(p,m_result.samples[p.index])){++skipped;continue;}m_done.insert(i);group.push_back(i);m_list->topLevelItem(i)->setText(0,label(i)+unified_i18n::text(" · Uygulandı"));}if(!group.empty())m_history.push_back(group);m_undo->setEnabled(!m_history.empty());showCurrent();m_status->setText(unified_i18n::text("%1 öneri uygulandı; %2 şüpheli, değiştirilmiş veya uygulanmış sayfa korundu. Son grup tek adımda geri alınabilir. PDF için Kaydet ve PDF’yi güncelle’yi kullan.").arg(group.size()).arg(skipped));}
};
