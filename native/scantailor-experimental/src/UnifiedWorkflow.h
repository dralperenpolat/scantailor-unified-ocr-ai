// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedI18n.h"
#include <QCoreApplication>
#include <QVector>
namespace unified_workflow {
inline QString duration(qint64 milliseconds){return QString::number(milliseconds/1000.,'f',1)+" s";}
inline QString text(char const* tr, char const* en) {
    auto lang=QCoreApplication::instance()->property("unifiedUiLanguage").toString();
    if(lang.isEmpty())lang=unified_i18n::selectedLanguage();
    return QString::fromUtf8(lang=="tr"?tr:en);
}
struct Step { QString id, title, description; int filter; bool playable; };
inline QVector<Step> steps() {
    return {
        {"input",text("0 · Girdi","0 · Input"),text("Resim, klasör, üst klasör veya PDF ekle.","Add images, a folder, a parent folder or a PDF."),-1,false},
        {"orientation",text("1 · Yönü düzelt","1 · Fix orientation"),text("Sayfaları 90 derece döndür. ▶ kitabı bu aşamaya kadar işler.","Rotate pages in 90-degree steps. ▶ processes the book through this stage."),0,true},
        {"split",text("2 · Sayfaları böl","2 · Split pages"),text("Çift sayfa fotoğrafını sol ve sağ sayfaya ayır.","Separate a spread into left and right pages."),1,true},
        {"geometry",text("3 · Geometrik düzeltme","3 · Geometric correction"),text("Eğim, perspektif ve eğriliği normal araçlarla düzelt.","Correct skew, perspective and curvature with the standard tools."),2,true},
        {"ai_geometry",text("4 · AI geometrik düzeltme","4 · AI geometry"),text("▶ AI, metin ve kenarı karşılaştırır; ölçümde iyileşeni uygular. Elle ayarlar korunur.","▶ compares AI, text and edges, then applies measured improvements. Manual settings are preserved."),2,true},
        {"content",text("5 · İçerik seçimi","5 · Select content"),text("Korunacak metin ve resim alanını seç. AI ekranıyla aynı ayar kullanılır.","Select the text and image area to retain. This shares settings with the AI screen."),3,true},
        {"ai_content",text("6 · AI içerik seçimi","6 · AI content selection"),text("▶ içerik önerilerini hazırlar ve uygun olanları uygular. Elle seçimler korunur.","▶ prepares and applies valid content proposals. Manual selections are preserved."),3,true},
        {"margins",text("7 · Kenarlar ve yerleşim","7 · Margins and layout"),text("Sayfa kenarlarını ve hizalamayı düzenle.","Set page margins and alignment."),4,true},
        {"output",text("8 · Çıktı","8 · Output"),text("▶ tüm sayfaların güncel görüntülerini üretir. PDF ve OCR sonraki aşamalardır.","▶ renders current images for all pages. PDF and OCR are subsequent stages."),5,true},
        {"cleanup",text("9 · Parmak ve leke temizliği","9 · Fingers and stains"),text("Çıktı görüntülerinde kenar lekelerini incele; metni koruyarak uygula.","Review edge stains in output images and apply cleanup while protecting text."),-1,true},
        {"pdf",text("10 · PDF oluştur","10 · Create PDF"),text("Sayfa boyutu, sığdırma ve yazı büyüklüğünü ayarlayıp görüntü PDF'si oluştur.","Set page size, fitting and text size, then create an image PDF."),-1,true},
        {"ocr",text("11 · OCR / Aranabilir PDF","11 · OCR / Searchable PDF"),text("Son PDF'ye aranabilir metin ekle. Görüntü korunur; OCR en son çalışır.","Add searchable text to the final PDF. Appearance is preserved; OCR runs last."),-1,true},
        {"storage",text("12 · Önbellek ve son temizlik","12 · Cache and final cleanup"),text("Boyutları incele; saklanacak kategorileri ve temizlik zamanını seç.","Inspect sizes; choose which categories to keep and when to clean."),-1,true},
        {"settings",text("Ayarlar","Settings"),text("PDF, OCR, çıktı klasörü, işlemci, GPU ve başlangıç ayarları.","PDF, OCR, output folder, CPU, GPU and startup preferences."),-1,false},
        {"help",text("Yardım ve kullanım kılavuzu","Help and user guide"),text("Düğmeler, görseller, aşamalar, hız ve doğruluk açıklamaları.","Illustrated buttons, stages, speed and accuracy explained."),-1,false}
    };
}
inline int row(QString const& id) {auto list=steps();for(int i=0;i<list.size();++i)if(list[i].id==id)return i;return -1;}
}
