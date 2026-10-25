// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>

namespace unified_runtime {
inline QString root() {
    auto value=qEnvironmentVariable("STOU_ROOT");
    if(!value.isEmpty())return value;
    QDir dir(QCoreApplication::applicationDirPath());
    for(int i=0;i<5;++i){if(dir.exists("local-tools.json"))return dir.absolutePath();dir.cdUp();}
    return QCoreApplication::applicationDirPath();
}
inline QString data(QString const& base) {
    auto value=qEnvironmentVariable("STOU_DATA");if(!value.isEmpty())return value;
    if(!QFileInfo::exists(base+"/release.json"))return base;
    auto saved=QSettings().value("unified/data_root").toString();
    if(QDir::isAbsolutePath(saved))return QDir::cleanPath(saved);
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/scantailor-ocr-unified";
}
inline QString resource(QString const& base,QString const& name) {
    auto candidate=data(base)+"/resources/"+name;
    if(QFileInfo::exists(candidate))return candidate;
    auto bundled=base+"/resources/"+name,shared=qEnvironmentVariable("STOU_ASSET_ROOT");
    return !shared.isEmpty()&&!QFileInfo::exists(bundled)?shared+"/resources/"+name:bundled;
}
inline QString venvPython(QString const& path) {
#ifdef Q_OS_WIN
    return path+"/Scripts/python.exe";
#else
    return path+"/bin/python3";
#endif
}
inline QString python(QString const& base) {
    auto selected=qEnvironmentVariable("STOU_PYTHON");if(!selected.isEmpty())return selected;
    for(auto const& p:QStringList{base+"/runtime/python.exe",base+"/runtime/bin/python3",venvPython(base+"/.venv")})
        if(QFileInfo::exists(p))return p;
    return QStandardPaths::findExecutable("python3");
}
inline QString paddlePython(QString const& base,QString const& device) {
    QString name=device.startsWith("gpu")?"paddle":"paddle_cpu";
    auto shared=qEnvironmentVariable("STOU_ASSET_ROOT");
    for(auto const& dir:QStringList{data(base),base,shared}){if(dir.isEmpty())continue;auto p=venvPython(dir+"/resources/runtimes/"+name);if(QFileInfo::exists(p))return p;}
    if(QFileInfo::exists(base+"/release.json"))return python(base);
    return venvPython((shared.isEmpty()?base:shared)+"/resources/runtimes/paddle");
}
inline QString cli() {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath()+"/scantailor-experimental-cli.exe";
#else
    return QCoreApplication::applicationDirPath()+"/scantailor-experimental-cli";
#endif
}
inline void initialize() {
    auto base=root();qputenv("STOU_ROOT",base.toUtf8());auto local=data(base);qputenv("STOU_DATA",local.toUtf8());QDir().mkpath(local);
    qputenv("PYTHONPATH",(base+"/src").toUtf8());qputenv("PYTHONUTF8","1");qputenv("PYTHONNOUSERSITE","1");qputenv("PYTHONDONTWRITEBYTECODE","1");
}
}
