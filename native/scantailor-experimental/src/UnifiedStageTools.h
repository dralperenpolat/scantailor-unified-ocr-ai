// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedPanel.h"
#include "stages/output/OptionsWidget.h"
#include <QGroupBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSignalBlocker>

// The queue panel remains the source of truth. Per-stage widgets are recreated
// when a project changes; receiver contexts release every old connection.
inline void bindUnifiedOutputTools(UnifiedPanel* panel, output::OptionsWidget* options)
{
    options->setUnifiedPdfOptions(panel->appearance(),panel->cleanupEnabled());
    QObject::connect(options,&output::OptionsWidget::pdfAppearanceChanged,
                     panel,&UnifiedPanel::setAppearance);
    QObject::connect(options,&output::OptionsWidget::pdfCleanupChanged,
                     panel,&UnifiedPanel::setCleanupEnabled);
    QObject::connect(panel,&UnifiedPanel::pdfOutputSettingsChanged,
                     options,&output::OptionsWidget::setUnifiedPdfOptions);
    auto group=options->findChild<QGroupBox*>("unifiedOutputTools");
    if(group){
        options->layout()->setAlignment(group,Qt::AlignLeft);
        auto extra=new QGroupBox(QString::fromUtf8("Son işlemler · deneme"));extra->setObjectName("stageOutputFinish");auto layout=new QVBoxLayout(extra);group->layout()->addWidget(extra);
        auto lines=new QCheckBox(QString::fromUtf8("Kalan satır eğriliğini düzelt"));lines->setObjectName("stageFinishLines");layout->addWidget(lines);lines->setToolTip(QString::fromUtf8("Output’a kadar işlenmiş görüntüde küçük satır eğriliklerini düzeltir. Önceki geometri ayarlarını değiştirmez. Güvenilir iyileşme bulunmazsa sayfayı korur. OCR aynı düzeltilmiş görüntüden yapılır."));
        auto white=new QCheckBox(QString::fromUtf8("Kâğıdı beyazlat"));white->setObjectName("stageFinishWhite");layout->addWidget(white);white->setToolTip(QString::fromUtf8("Gri harf kenarlarını koruyarak zemini açar. Büyük görseller ve renkli bölgeler korunmaya çalışılır; soluk dipnotları önizlemede kontrol et."));
        auto fine=new QCheckBox(QString::fromUtf8("Küçük yazıyı netleştir"));fine->setObjectName("stageFinishFine");layout->addWidget(fine);fine->setToolTip(QString::fromUtf8("Dipnot ve üst simge büyüklüğündeki izlere sınırlı keskinleştirme uygular. Harf üretmez veya metni yeniden yazmaz."));
        auto sync=[=](QJsonObject const& v){QSignalBlocker a(lines),b(white),c(fine);lines->setChecked(v.value("lines").toBool());white->setChecked(v.value("white").toBool());fine->setChecked(v.value("fine").toBool());};sync(panel->outputFinish());QObject::connect(panel,&UnifiedPanel::outputFinishChanged,extra,sync);
        auto update=[=]{panel->setOutputFinish({{"lines",lines->isChecked()},{"white",white->isChecked()},{"fine",fine->isChecked()}});};for(auto b:{lines,white,fine})QObject::connect(b,&QCheckBox::toggled,panel,update);
        auto test=new QPushButton(QString::fromUtf8("Önce / sonra göster"));test->setObjectName("stageFinishPreview");test->setEnabled(false);layout->addWidget(test);
        auto scope=new QLabel(QString::fromUtf8("Önizleme: seçili sayfa.\nPDF: kitaptaki tüm sayfalar."));scope->setObjectName("stageFinishScope");scope->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);scope->setToolTip(QString::fromUtf8("Yeni PDF ve Kaydet ve PDF’yi güncelle işlemlerine uygulanır. Etkisini Önce / sonra göster ile kontrol edebilirsin."));layout->addWidget(scope);
        auto timing=new QComboBox;timing->setObjectName("stageOcrTiming");timing->addItem(QString::fromUtf8("Çıktıyla birlikte OCR yap"),"during");timing->addItem(QString::fromUtf8("Görüntüler bitince OCR yap"),"after");timing->addItem(QString::fromUtf8("OCR yapma"),"none");timing->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);timing->setMinimumContentsLength(12);timing->setCurrentIndex(qMax(0,timing->findData(panel->ocrTiming())));group->layout()->addWidget(new QLabel(QString::fromUtf8("OCR ne zaman?")));group->layout()->addWidget(timing);
        QObject::connect(timing,qOverload<int>(&QComboBox::currentIndexChanged),panel,[=]{panel->setOcrTiming(timing->currentData().toString());});QObject::connect(panel,&UnifiedPanel::ocrTimingChanged,timing,[=](QString const& mode){QSignalBlocker b(timing);timing->setCurrentIndex(timing->findData(mode));});
        auto note=new QLabel(QString::fromUtf8("Yeni PDF işlerine uygulanır. OCR kapalıyken aranabilir metin ve OCR korumalı otomatik temizlik eklenmez. Çalışan kuyruğu değiştirmez."));note->setWordWrap(true);group->layout()->addWidget(note);
        auto compact=new QPushButton(QString::fromUtf8("PDF’yi küçült…"));compact->setObjectName("stagePdfCompact");compact->setToolTip(QString::fromUtf8("Mevcut PDF’nin görünümünü ve metnini koruyarak kayıpsız sıkıştırır; yeni dosya kaydeder."));group->layout()->addWidget(compact);QObject::connect(compact,&QPushButton::clicked,panel,[=]{panel->openPdfTools("compact");});
    }
}
inline void bindUnifiedLayoutTools(UnifiedPanel* panel,QWidget* options){
    auto group=new QGroupBox(QString::fromUtf8("PDF sayfa yerleşimi"),options);group->setObjectName("unifiedLayoutTools");auto form=new QFormLayout(group);form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    auto paper=new QComboBox;paper->setObjectName("stagePdfPaper");paper->addItem(QString::fromUtf8("A4 · aynı ölçek"),"a4");paper->addItem(QString::fromUtf8("A4 · yazı yüksekliğini eşitle"),"a4_text");paper->addItem(QString::fromUtf8("A4’ü doldur · orantılı"),"a4_fill");paper->addItem(QString::fromUtf8("Experimental boyutu"),"native");form->addRow(paper);
    auto margin=new QDoubleSpinBox;margin->setObjectName("stagePdfMargin");margin->setRange(0,70);margin->setSuffix(" mm");form->addRow(QString::fromUtf8("Dış kenar payı"),margin);
    auto fit=new QCheckBox(QString::fromUtf8("Fazla beyaz kenarları çıkar"));fit->setObjectName("stagePdfContentFit");form->addRow(fit);
    auto text=new QDoubleSpinBox;text->setObjectName("stagePdfTextHeight");text->setRange(0,5);text->setDecimals(2);text->setSingleStep(.1);text->setSpecialValueText(QString::fromUtf8("Otomatik"));text->setSuffix(" mm");form->addRow(QString::fromUtf8("Gövde harfi yüksekliği"),text);
    auto scope=new QLabel(QString::fromUtf8("Sonraki PDF oluşturma: tüm kitaplar / tüm sayfalar. Çalışan kuyruğun ayarları değişmez. A4’ü doldurda yazı boyu sayfalar arasında değişebilir."));scope->setWordWrap(true);form->addRow(scope);
    if(auto box=qobject_cast<QVBoxLayout*>(options->layout()))box->insertWidget(0,group);
    auto sync=[=](QJsonObject const& v){QSignalBlocker a(paper),b(margin),c(fit),d(text);paper->setCurrentIndex(qMax(0,paper->findData(v.value("paper").toString())));margin->setValue(v.value("margin_mm").toDouble());fit->setChecked(v.value("content_fit").toBool());text->setValue(v.value("target_x_mm").toDouble());};sync(panel->pdfLayoutOptions());QObject::connect(panel,&UnifiedPanel::pdfLayoutSettingsChanged,group,sync);
    auto update=[=]{panel->setPdfLayoutOptions({{"paper",paper->currentData().toString()},{"margin_mm",margin->value()},{"content_fit",fit->isChecked()},{"target_x_mm",text->value()}});};
    QObject::connect(paper,qOverload<int>(&QComboBox::currentIndexChanged),group,update);QObject::connect(margin,qOverload<double>(&QDoubleSpinBox::valueChanged),group,update);QObject::connect(fit,&QCheckBox::toggled,group,update);QObject::connect(text,qOverload<double>(&QDoubleSpinBox::valueChanged),group,update);
}
