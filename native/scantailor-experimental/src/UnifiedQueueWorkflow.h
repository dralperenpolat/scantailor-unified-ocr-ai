// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QHash>

// Worker stage names are persisted identifiers; do not translate before mapping.
inline QString unifiedQueueWorkflowStep(QString const& stage){
    static const QHash<QString,QString> native{
        {"Fix Orientation","orientation"},{"Split Pages","split"},
        {"Geometric Distortions","geometry"},{"Select Content","content"},
        {"Margins","margins"},{"Output","output"},
        {"Hazırlanıyor","input"},{"Düzenlemeye hazır","margins"},
        {"Kitap içerik denetimi","content"},{"Geometri önbellekten","geometry"},
        {"Görüntü hazırlanıyor","output"},{"Görüntü hazır · OCR bekliyor","pdf"},
        {"Görüntülü PDF hazır","pdf"},{"Sayfa yerleşimi düzeltiliyor","margins"},
        {"Beyaz kenarlar ölçülüyor","margins"}
    };
    if(native.contains(stage))return native.value(stage);
    if(stage.contains("OCR"))return "ocr";
    if(stage.contains("PDF"))return "pdf";
    if(stage.startsWith("Geometri"))return "geometry";
    return {};
}

inline QHash<QString,QString> unifiedQueueWorkflowStatuses(QJsonObject const& job){
    QHash<QString,QString> statuses;
    if(!job.value("visual_pdf").toString().isEmpty()||!job.value("pdf").toString().isEmpty())statuses.insert("pdf",QString::fromUtf8("✓"));
    auto verification=job.value("verification").toObject();
    if(!job.value("pdf").toString().isEmpty()&&verification.contains("ocr_timing")&&verification.value("ocr_timing").toString()!="none")statuses.insert("ocr",QString::fromUtf8("✓"));
    auto active=unifiedQueueWorkflowStep(job.value("status").toString());
    if(!active.isEmpty()){
        auto live=job.value("live_progress").toObject();
        auto progress=live.value("total").toInt()>0?QString("%1/%2").arg(live.value("page").toInt()).arg(live.value("total").toInt()):QString::fromUtf8("…");
        statuses.insert(active,progress);
    }
    return statuses;
}
