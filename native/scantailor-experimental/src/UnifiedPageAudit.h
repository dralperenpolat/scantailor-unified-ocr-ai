// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include <QWidget>
#include <QDomDocument>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QHeaderView>
#include <QScrollBar>
#include <QFileInfo>
#include <QMap>
#include <functional>

// A read-only view of the live native settings, never an independent copy of
// editable settings. The native Apply-to dialogs remain authoritative.
class UnifiedPageAudit : public QWidget {
public:
    std::function<void(int,int)> navigate;
    explicit UnifiedPageAudit(QWidget* parent=nullptr):QWidget(parent) {
        auto layout=new QVBoxLayout(this);auto bar=new QHBoxLayout;layout->addLayout(bar);
        m_search=new QLineEdit;m_search->setPlaceholderText(unified_i18n::text("Dosya, ayar veya hata nedeni ara…"));bar->addWidget(m_search);
        m_filter=new QComboBox;m_filter->addItems({unified_i18n::text("Tüm sayfalar"),unified_i18n::text("Kontrol gereken geometri"),unified_i18n::text("Eğrilik düzeltmesi"),unified_i18n::text("Elle içerik seçimi"),unified_i18n::text("Ölçeklendirme uygulananlar")});bar->addWidget(m_filter);
        m_count=new QLabel;bar->addWidget(m_count);
        m_tree=new QTreeWidget;m_tree->setRootIsDecorated(false);m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tree->setHeaderLabels({unified_i18n::text("Sayfa"),unified_i18n::text("Bölme"),unified_i18n::text("Geometri / karar"),unified_i18n::text("İçerik alanı"),unified_i18n::text("Hizalama"),unified_i18n::text("Boyut eşleme"),unified_i18n::text("Çıktı / temizleme")});
        m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);m_tree->header()->setStretchLastSection(true);
        for(int i=0;i<7;++i)m_tree->setColumnWidth(i,i==2?270:i==0?220:180);
        layout->addWidget(m_tree);
        auto note=new QLabel(unified_i18n::text("Etkin ayarlar canlı gösterilir. Bir hücreye çift tıklayın: ilgili sayfa ve aşama açılır.\nYeşil hücre: tablo açıkken ayar değişti. Açılmamış aşamalarda ‘Hesaplanmadı’ görünür.\n‘Apply to…’ kapsam seçer; yalnız ayar panelini açıp kapatmak işleme uygulamaz. A4/PDF yerleşimi OCR kuyruğunda ayrıca gösterilir."));note->setWordWrap(true);layout->addWidget(note);
        connect(m_search,&QLineEdit::textChanged,[this]{filter();});connect(m_filter,qOverload<int>(&QComboBox::currentIndexChanged),[this]{filter();});
        connect(m_tree,&QTreeWidget::itemDoubleClicked,[this](QTreeWidgetItem* item,int column){if(navigate)navigate(item->data(0,Qt::UserRole).toInt(),column<=1?1:column==2?2:column==3?3:column<=5?4:5);});
    }
    void update(QDomDocument const& doc) {
        auto signature=doc.toByteArray(-1);if(signature==m_signature)return;m_signature=signature;
        int selected=m_tree->currentItem()?m_tree->currentItem()->data(0,Qt::UserRole).toInt():-1;
        int scroll=m_tree->verticalScrollBar()->value();
        auto root=doc.documentElement(),filters=root.firstChildElement("filters");
        QMap<QString,QString> names,images;
        for(auto e=root.firstChildElement("files").firstChildElement("file");!e.isNull();e=e.nextSiblingElement("file"))names[e.attribute("id")]=e.attribute("name");
        for(auto e=root.firstChildElement("images").firstChildElement("image");!e.isNull();e=e.nextSiblingElement("image"))images[e.attribute("id")]=names.value(e.attribute("fileId"));
        auto find=[](QDomElement p,char const* tag,QString const& id){for(auto e=p.firstChildElement(tag);!e.isNull();e=e.nextSiblingElement(tag))if(e.attribute("id")==id)return e;return QDomElement();};
        auto mode=[](QDomElement const& p){return p.isNull()?unified_i18n::text("Hesaplanmadı"):p.attribute("mode")=="manual"?unified_i18n::text("Elle"):unified_i18n::text("Otomatik");};
        auto trValue=[](QString v){static QMap<QString,QString> tr{{"rotation",unified_i18n::text("Eğiklik")},{"warp",unified_i18n::text("Eğrilik")},{"perspective",unified_i18n::text("Perspektif")},{"none",unified_i18n::text("Kapalı")},{"margins",unified_i18n::text("Kenarları genişlet")},{"scale",unified_i18n::text("Orantılı ölçekle")},{"affine",unified_i18n::text("En/boy ayrı ölçek")},{"disabled",unified_i18n::text("Eşleme kapalı")},{"top",unified_i18n::text("Üst")},{"bottom",unified_i18n::text("Alt")},{"left",unified_i18n::text("Sol")},{"right",unified_i18n::text("Sağ")},{"hcenter",unified_i18n::text("Ortala")},{"vcenter",unified_i18n::text("Ortala")},{"two-pages",unified_i18n::text("Çift sayfa")},{"single-page",unified_i18n::text("Tek sayfa")},{"bw",unified_i18n::text("Siyah-beyaz")},{"colorOrGray",unified_i18n::text("Renk / gri")},{"mixed",unified_i18n::text("Karma")}};return tr.value(v,v);};
        int index=0;
        for(auto page=root.firstChildElement("pages").firstChildElement("page");!page.isNull();page=page.nextSiblingElement("page"),++index) {
            QString id=page.attribute("id"),imageId=page.attribute("imageId"),half=page.attribute("subPage");
            auto split=find(filters.firstChildElement("page-split"),"image",imageId).firstChildElement("params");
            auto geo=find(filters.firstChildElement("deskew"),"page",id).firstChildElement("params");
            auto content=find(filters.firstChildElement("select-content"),"page",id).firstChildElement("params");
            auto layout=find(filters.firstChildElement("page-layout"),"page",id).firstChildElement("params");
            auto output=find(filters.firstChildElement("output"),"page",id);
            auto color=output.firstChildElement("params").firstChildElement("color-params");
            auto size=content.firstChildElement("content-size-px"),align=layout.firstChildElement("alignment"),match=layout.firstChildElement("matchSizeMode");
            QString warning=geo.attribute("unifiedWarning"),type=geo.attribute("distortionType");
            QString geometry=geo.isNull()?mode(geo):trValue(type)+" · "+mode(geo.firstChildElement(type));
            if(!warning.isEmpty())geometry+=unified_i18n::text(" · KONTROL: ")+warning;
            QString box=mode(content);if(!size.isNull())box+=QString(" · %1×%2 px").arg(size.attribute("width").toDouble(),0,'f',0).arg(size.attribute("height").toDouble(),0,'f',0);
            QString sizing=layout.isNull()?mode(layout):trValue(match.attribute("mode"));
            if(!match.isNull())sizing+=QString(" · %1 / %2").arg(match.attribute("subScaleWidth","1"),match.attribute("subScaleHeight","1"));
            QString final=color.isNull()?mode(color):trValue(color.attribute("colorMode"))+" · "+color.firstChildElement("bw").attribute("thresholdMethod");
            int zones=output.firstChildElement("fill-zones").elementsByTagName("zone").count();final+=unified_i18n::text(" · %1 temizleme alanı").arg(zones);
            QStringList cells{QString::number(index+1)+" · "+images.value(imageId)+(half=="left"?unified_i18n::text(" · sol"):half=="right"?unified_i18n::text(" · sağ"):""),mode(split)+" · "+trValue(split.firstChildElement("pages").attribute("type")),geometry,box,layout.isNull()?mode(layout):trValue(align.attribute("vert"))+" / "+trValue(align.attribute("hor")),sizing,final};
            auto item=m_tree->topLevelItem(index);if(!item)item=new QTreeWidgetItem(m_tree);
            for(int c=0;c<cells.size();++c)if(item->text(c)!=cells[c])item->setText(c,cells[c]);
            item->setData(0,Qt::UserRole,index);
            item->setData(0,Qt::UserRole+1,!warning.isEmpty());item->setData(0,Qt::UserRole+2,type=="warp");item->setData(0,Qt::UserRole+3,content.attribute("mode")=="manual");item->setData(0,Qt::UserRole+4,match.attribute("mode")=="scale"||match.attribute("mode")=="affine");
            QString key=images.value(imageId)+"/"+half;
            for(int c=0;c<cells.size();++c){QString cellKey=key+"/"+QString::number(c);if(m_old.contains(cellKey)&&m_old.value(cellKey)!=cells[c])item->setBackground(c,QColor(210,242,215));m_old[cellKey]=cells[c];item->setToolTip(c,cells[c]);}
            item->setToolTip(2,geometry+"\n"+geo.attribute("unifiedGeometry"));
            if(!warning.isEmpty())item->setForeground(2,QColor(155,70,0));
            if(index==selected)m_tree->setCurrentItem(item);
        }
        while(m_tree->topLevelItemCount()>index)delete m_tree->takeTopLevelItem(index);
        m_tree->verticalScrollBar()->setValue(scroll);filter();
    }
private:
    QTreeWidget* m_tree;QLineEdit* m_search;QComboBox* m_filter;QLabel* m_count;
    QByteArray m_signature;QMap<QString,QString> m_old;
    void filter(){int visible=0;for(int i=0;i<m_tree->topLevelItemCount();++i){auto p=m_tree->topLevelItem(i);QString text;for(int c=0;c<7;++c)text+=p->text(c)+" ";bool show=text.contains(m_search->text(),Qt::CaseInsensitive)&&(m_filter->currentIndex()==0||p->data(0,Qt::UserRole+m_filter->currentIndex()).toBool());p->setHidden(!show);if(show)++visible;}m_count->setText(unified_i18n::text("%1 / %2 sayfa").arg(visible).arg(m_tree->topLevelItemCount()));}
};
