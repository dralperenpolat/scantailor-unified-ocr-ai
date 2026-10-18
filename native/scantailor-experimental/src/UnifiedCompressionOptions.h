// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QJsonObject>
#include <QSignalBlocker>
#include <functional>

// The PDF workspace and saved defaults use one set of compression values.
class UnifiedCompressionOptions:public QWidget {
public:
    std::function<void()> changed;
    explicit UnifiedCompressionOptions(QWidget* parent=nullptr):QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto form=new QFormLayout;layout->addLayout(form);
        m_profile=new QComboBox;m_profile->setObjectName("compressionProfile");
        for(auto item:QList<QPair<QString,QString>>{
                {tr("Kayıpsız"),"lossless"},{tr("Dengeli"),"balanced"},{tr("Küçük dosya"),"small"},
                {tr("Hedef boyut"),"target"},{tr("Özel"),"custom"}})m_profile->addItem(item.first,item.second);
        form->addRow(tr("Sıkıştırma"),m_profile);
        m_target=new QDoubleSpinBox;m_target->setObjectName("compressionTargetMiB");
        m_target->setRange(.001,1000000);m_target->setDecimals(3);m_target->setValue(25);m_target->setSuffix(" MiB");
        m_targetLabel=new QLabel(tr("Hedef dosya boyutu"));form->addRow(m_targetLabel,m_target);
        m_note=new QLabel;m_note->setWordWrap(true);m_note->setObjectName("compressionExplanation");layout->addWidget(m_note);
        m_expand=new QToolButton;m_expand->setText(tr("Gelişmiş sıkıştırma ayarları"));m_expand->setCheckable(true);
        m_expand->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);m_expand->setArrowType(Qt::RightArrow);layout->addWidget(m_expand);
        m_advanced=new QWidget;auto advanced=new QFormLayout(m_advanced);advanced->setContentsMargins(0,0,0,0);layout->addWidget(m_advanced);
        m_dpi=new QSpinBox;m_dpi->setObjectName("compressionDpi");m_dpi->setRange(72,2400);m_dpi->setValue(300);m_dpi->setSuffix(" DPI");advanced->addRow(tr("Görüntü çözünürlüğü üst sınırı"),m_dpi);
        m_quality=new QSpinBox;m_quality->setObjectName("compressionQuality");m_quality->setRange(40,100);m_quality->setValue(92);advanced->addRow(tr("JPEG kalitesi"),m_quality);
        m_minDpi=new QSpinBox;m_minDpi->setObjectName("compressionMinDpi");m_minDpi->setRange(72,2400);m_minDpi->setValue(150);m_minDpi->setSuffix(" DPI");advanced->addRow(tr("Hedef aramasında en düşük çözünürlük"),m_minDpi);
        m_minQuality=new QSpinBox;m_minQuality->setObjectName("compressionMinQuality");m_minQuality->setRange(40,100);m_minQuality->setValue(72);advanced->addRow(tr("Hedef aramasında en düşük JPEG kalitesi"),m_minQuality);
        m_color=new QComboBox;m_color->setObjectName("compressionColor");m_color->addItem(tr("Renkleri koru"),"preserve");m_color->addItem(tr("Gri tonlara dönüştür"),"gray");advanced->addRow(tr("Renk"),m_color);
        m_protect=new QCheckBox(tr("Küçük yazıların çözünürlüğünü koru"));m_protect->setObjectName("compressionProtectText");m_protect->setChecked(true);advanced->addRow(m_protect);
        m_repack=new QCheckBox(tr("Yoğun kayıpsız sıkıştırma · daha yavaş"));m_repack->setObjectName("compressionRepack");advanced->addRow(m_repack);
        connect(m_expand,&QToolButton::toggled,this,[this](bool expanded){m_advanced->setVisible(expanded);m_expand->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);});
        connect(m_profile,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refresh();if(changed)changed();});
        refresh();m_advanced->hide();
    }
    QString profile()const{return m_profile->currentData().toString();}
    void collapseAdvanced(){m_expand->setChecked(false);}
    QJsonObject options()const {
        if(profile()=="lossless")return m_repack->isChecked()?QJsonObject{{"recompress_flate",true}}:QJsonObject();
        QJsonObject value{{"protect_text",m_protect->isChecked()}};
        if(profile()=="custom"||profile()=="target"){
            value.insert("dpi",m_dpi->value());value.insert("jpeg_quality",m_quality->value());
            value.insert("color",m_color->currentData().toString());
        }
        if(profile()=="target"){
            value.insert("target_mib",m_target->value());value.insert("min_dpi",m_minDpi->value());
            value.insert("min_quality",m_minQuality->value());
            value.insert("recompress_flate",m_repack->isChecked());
        }
        return value;
    }
    void setOptions(QString const& profile,QJsonObject const& value){
        QSignalBlocker block(m_profile);auto index=m_profile->findData(profile);m_profile->setCurrentIndex(index>=0?index:0);
        m_target->setValue(value.value("target_mib").toDouble(25));m_dpi->setValue(value.value("dpi").toInt(300));
        m_quality->setValue(value.value("jpeg_quality").toInt(92));m_minDpi->setValue(value.value("min_dpi").toInt(150));
        m_minQuality->setValue(value.value("min_quality").toInt(72));m_protect->setChecked(value.value("protect_text").toBool(true));
        m_repack->setChecked(value.value("recompress_flate").toBool(false));
        index=m_color->findData(value.value("color").toString("preserve"));m_color->setCurrentIndex(index>=0?index:0);refresh();
    }
private:
    QComboBox *m_profile,*m_color;QDoubleSpinBox* m_target;
    QSpinBox *m_dpi,*m_quality,*m_minDpi,*m_minQuality;QCheckBox *m_protect,*m_repack;
    QLabel *m_note,*m_targetLabel;QToolButton* m_expand;QWidget* m_advanced;
    static QString tr(char const* text){return unified_i18n::text(text);}
    void refresh(){
        auto id=profile();bool target=id=="target",custom=id=="custom"||target,lossless=id=="lossless";
        m_target->setVisible(target);m_targetLabel->setVisible(target);
        m_dpi->setEnabled(custom);m_quality->setEnabled(custom);m_color->setEnabled(custom);
        m_minDpi->setEnabled(target);m_minQuality->setEnabled(target);m_protect->setEnabled(!lossless);
        m_expand->setVisible(true);if(lossless)m_expand->setChecked(false);else if(custom)m_expand->setChecked(true);
        auto form=qobject_cast<QFormLayout*>(m_advanced->layout());
        for(auto field:QList<QWidget*>{m_dpi,m_quality,m_color,m_minDpi,m_minQuality}){
            bool visible=!lossless&&((field!=m_minDpi&&field!=m_minQuality)||target);
            field->setVisible(visible);if(auto label=form->labelForField(field))label->setVisible(visible);
        }
        m_protect->setVisible(!lossless);m_repack->setVisible(lossless||target);
        if(lossless)m_note->setText(tr("Görüntü pikselleri korunur. Zaten sıkıştırılmış bir PDF daha fazla küçülmeyebilir."));
        else if(target)m_note->setText(tr("Kalite sınırları içinde hedefe yaklaşır. Hedef sağlanamazsa elde edilen gerçek boyut gösterilir."));
        else m_note->setText(tr("Görüntü değişebilir. Küçük yazılar korunur; kaydetmeden önce örnek sayfaları karşılaştırın."));
    }
};
