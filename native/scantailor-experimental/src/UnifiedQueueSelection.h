// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SmartFilenameOrdering.h"
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <algorithm>

// This only reads the selected job. Never open or save the editor's project
// merely to browse the queue: the editor may contain unsaved manual changes.
inline QJsonArray unifiedQueueImages(QJsonObject const& job, QJsonArray const& live = {}, QString const& preferredPath = {})
{
    QJsonArray images;
    QSet<QString> seen;
    QMap<int, QJsonObject> processed;
    auto descriptor = [&](QString path, QString const& label, QString const& kind, int page) {
        QFileInfo file(path);
        return QJsonObject{{"path", file.absoluteFilePath()}, {"label", label}, {"kind", kind}, {"page", page},
            {"revision", QString::number(file.size()) + ":" + QString::number(file.lastModified().toMSecsSinceEpoch())}};
    };
    auto add = [&](QString path, QString const& label, QString const& kind, int page) {
        if (path.isEmpty() || path.endsWith(".pdf", Qt::CaseInsensitive)) return;
        path = QFileInfo(path).absoluteFilePath();
        if (seen.contains(path) || !QFileInfo::exists(path)) return;
        seen.insert(path);
        images.append(descriptor(path, label, kind, page));
    };
    auto addProcessed = [&](QString const& path, QString const& label, int page, int priority, QString const& pdf=QString(), int pdfPage=0) {
        if (page < 1 || path.isEmpty() || path.endsWith(".pdf", Qt::CaseInsensitive) || !QFileInfo::exists(path)) return;
        auto image = descriptor(path, label, "processed", page);
        if(!pdf.isEmpty()&&QFileInfo::exists(pdf)){image.insert("pdf",pdf);image.insert("pdf_page",pdfPage);}
        if (!preferredPath.isEmpty() && QFileInfo(preferredPath).absoluteFilePath() == image.value("path").toString()) priority = 100;
        if (processed.contains(page) && processed.value(page).value("priority").toInt() > priority) return;
        image.insert("priority", priority); processed.insert(page, image);
    };
    for (auto value : job.value("pages").toArray()) {
        auto page = value.toObject();
        bool layout=QFileInfo(page.value("preview").toString()).fileName().startsWith("layout-")&&QFileInfo::exists(job.value("pdf").toString());
        addProcessed(page.value("preview").toString(), QString::fromUtf8("PDF görünümü · sayfa %1").arg(page.value("page").toInt()), page.value("page").toInt(), 4,
            layout?job.value("pdf").toString():page.value("page_pdf").toString(),layout?page.value("page").toInt()-1:0);
    }
    for (auto value : live) {
        auto page = value.toObject();
        QString stage = page.value("stage").toString();
        int number = page.value("page").toInt() + (stage == "OCR / PDF" ? 1 : 0);
        int priority = page.contains("page_pdf") ? 4 : stage == "OCR / PDF" ? 3 : 2;
        addProcessed(page.value("preview").toString(), stage + QString::fromUtf8(" · sayfa %1").arg(number), number, priority,page.value("page_pdf").toString(),0);
    }
    // Native outputs also exist before OCR, and must survive an application
    // restart. The manifest supplies the authoritative split-page order.
    QJsonArray outputs = job.value("outputs").toArray();
    for (auto key : {"source_project", "project"}) {
        QString p = job.value(key).toString(); if (p.isEmpty()) continue;
        QFile manifest(p + ".outputs.json");
        if (manifest.open(QIODevice::ReadOnly)) {
            auto pages = QJsonDocument::fromJson(manifest.readAll()).object().value("pages").toArray();
            if (!pages.isEmpty()) outputs = pages;
        }
    }
    for (int i = 0; i < outputs.size(); ++i)
        addProcessed(outputs.at(i).toString(), QString::fromUtf8("İşlenmiş çıktı · sayfa %1").arg(i + 1), i + 1, 1);
    for (auto const& image : processed) { images.append(image); seen.insert(image.value("path").toString()); }
    QString project = job.value("source_project").toString();
    if (project.isEmpty()) project = job.value("project").toString();
    QFile file(project);
    QDomDocument doc;
    if (!project.isEmpty() && file.open(QIODevice::ReadOnly) && doc.setContent(&file)) {
        QDir base(QFileInfo(project).absolutePath());
        QMap<QString, QString> dirs;
        auto root = doc.documentElement();
        for (auto e = root.firstChildElement("directories").firstChildElement("directory"); !e.isNull(); e = e.nextSiblingElement("directory"))
            dirs.insert(e.attribute("id"), base.absoluteFilePath(e.attribute("path")));
        for (auto e = root.firstChildElement("files").firstChildElement("file"); !e.isNull(); e = e.nextSiblingElement("file")) {
            if (!dirs.contains(e.attribute("dirId"))) continue;
            auto name = e.attribute("name");
            add(QDir(dirs.value(e.attribute("dirId"))).filePath(name), QString::fromUtf8("Kaynak fotoğraf · ") + name, "source", 0);
        }
    }
    QString folder = job.value("folder").toString();
    if (!folder.isEmpty()) {
        auto files = QDir(folder).entryInfoList({"*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.bmp"}, QDir::Files);
        std::sort(files.begin(), files.end(), SmartFilenameOrdering());
        for (auto const& source : files) add(source.absoluteFilePath(), QString::fromUtf8("Kaynak fotoğraf · ") + source.fileName(), "source", 0);
    }
    return images;
}
