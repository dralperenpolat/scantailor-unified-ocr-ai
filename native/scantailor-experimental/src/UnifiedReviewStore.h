// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedRuntime.h"
#include "PageInfo.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDomDocument>
namespace unified_review {
inline QString key(PageInfo const& p){return QFileInfo(p.imageId().filePath()).absoluteFilePath()+"|"+QString::number(p.imageId().page())+"|"+p.id().subPageAsString();}
inline QString sourceStamp(PageInfo const& p){QFileInfo f(p.imageId().filePath());return QString::number(f.size())+"|"+QString::number(f.lastModified().toMSecsSinceEpoch());}
inline QString directory(QString const& root,QString const& project,QString const& kind){
    if(project.isEmpty())return {};auto hash=QCryptographicHash::hash(project.toUtf8(),QCryptographicHash::Sha256).toHex();
    auto path=unified_runtime::data(root)+"/resources/reviews/"+QString::fromLatin1(hash)+"/"+kind;return QDir().mkpath(path)?path:QString();
}
inline bool save(QString const& path,QJsonObject const& data){QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return false;auto bytes=QJsonDocument(data).toJson(QJsonDocument::Compact);return f.write(bytes)==bytes.size()&&f.commit();}
inline QJsonObject load(QString const& path){QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>32*1024*1024)return {};return QJsonDocument::fromJson(f.readAll()).object();}
inline QJsonArray rect(QRectF const& r){return {r.x(),r.y(),r.width(),r.height()};}
inline QRectF rect(QJsonValue const& v){auto a=v.toArray();if(a.size()!=4)return {};return {a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};}
inline bool sameRect(QRectF a,QRectF b){return std::abs(a.x()-b.x())+std::abs(a.y()-b.y())+std::abs(a.width()-b.width())+std::abs(a.height()-b.height())<.05;}
template<class P> QString xml(P const& p){QDomDocument d;d.appendChild(p.toXml(d,"params"));return d.toString(-1);}
inline QDomElement element(QString const& xml){QDomDocument d;d.setContent(xml);return d.documentElement();}
}
