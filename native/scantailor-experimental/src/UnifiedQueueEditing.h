// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedQueueSelection.h"
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>

inline QJsonObject unifiedEditTarget(QJsonObject const& job, QString const& path, QJsonArray const& live = {})
{
    QString project=job.value("project").toString();
    if(!QFileInfo::exists(project))project=job.value("source_project").toString();
    QFile file(project);QDomDocument doc;
    if(project.isEmpty()||!file.open(QIODevice::ReadOnly)||!doc.setContent(&file))return {};
    file.close();
    int number=1;bool matched=path.isEmpty()||path.endsWith(".pdf",Qt::CaseInsensitive);
    for(auto value:unifiedQueueImages(job,live,path)){
        auto image=value.toObject();
        if(QFileInfo(image.value("path").toString()).absoluteFilePath()!=QFileInfo(path).absoluteFilePath())continue;
        number=image.value("page").toInt();matched=true;break;
    }
    if(!matched)return {};
    auto root=doc.documentElement();QDir base(QFileInfo(project).absolutePath());
    QMap<QString,QString> dirs,files,images;QMap<QString,int> frames;
    for(auto e=root.firstChildElement("directories").firstChildElement("directory");!e.isNull();e=e.nextSiblingElement("directory"))dirs[e.attribute("id")]=base.absoluteFilePath(e.attribute("path"));
    for(auto e=root.firstChildElement("files").firstChildElement("file");!e.isNull();e=e.nextSiblingElement("file"))files[e.attribute("id")]=QDir(dirs.value(e.attribute("dirId"))).absoluteFilePath(e.attribute("name"));
    for(auto e=root.firstChildElement("images").firstChildElement("image");!e.isNull();e=e.nextSiblingElement("image")){images[e.attribute("id")]=files.value(e.attribute("fileId"));frames[e.attribute("id")]=e.attribute("fileImage").toInt();}
    int index=0;
    for(auto page=root.firstChildElement("pages").firstChildElement("page");!page.isNull();page=page.nextSiblingElement("page"),++index){
        QString source=images.value(page.attribute("imageId"));
        if((number>0&&index==number-1)||(number==0&&QFileInfo(source).absoluteFilePath()==QFileInfo(path).absoluteFilePath()))
            return {{"job_id",job.value("id")},{"title",job.value("title")},{"project",QFileInfo(project).absoluteFilePath()},
                    {"source",source},{"image_page",frames.value(page.attribute("imageId"))},{"subpage",page.attribute("subPage")},{"page",index+1}};
    }
    return {};
}

inline QString unifiedCreateEditCopy(QJsonObject const& target, QString const& drafts, QString const& queue)
{
    QFile source(target.value("project").toString());QDomDocument doc;
    if(!source.open(QIODevice::ReadOnly)||!doc.setContent(&source))throw std::runtime_error("Düzenlenecek proje okunamadı.");
    source.close();
    QString directory=QDir(drafts).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString output=directory+"/out",project=directory+"/Duzenleme.ScanTailor";
    if(!QDir().mkpath(output))throw std::runtime_error("Düzenleme klasörü oluşturulamadı.");
    auto root=doc.documentElement();root.setAttribute("outputDirectory",output);
    QDir old(QFileInfo(source).absolutePath());
    for(auto d=root.firstChildElement("directories").firstChildElement("directory");!d.isNull();d=d.nextSiblingElement("directory"))d.setAttribute("path",old.absoluteFilePath(d.attribute("path")));
    for(auto p=root.firstChildElement("filters").firstChildElement("output").firstChildElement("page");!p.isNull();p=p.nextSiblingElement("page")){
        for(auto e=p.firstChildElement();!e.isNull();){auto next=e.nextSiblingElement();if(e.tagName()=="output-params"||e.tagName()=="outputParams")p.removeChild(e);e=next;}
    }
    QSaveFile file(project);
    if(!file.open(QIODevice::WriteOnly)||file.write(doc.toByteArray())<0||!file.commit())throw std::runtime_error("Düzenleme kopyası kaydedilemedi.");
    auto binding=target;binding.insert("queue",queue);
    QSaveFile meta(project+".unified-edit.json");
    if(!meta.open(QIODevice::WriteOnly)||meta.write(QJsonDocument(binding).toJson())<0||!meta.commit())throw std::runtime_error("Kitap bağlantısı kaydedilemedi.");
    return project;
}
