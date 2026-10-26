// Saving a draft and requesting a PDF are separate durable operations.
#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <stdexcept>
namespace unified_saved_edits {
inline QString path(QString const& queue,QString const& id){return queue+".saved/"+QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex())+".json";}
inline QJsonObject read(QString const& path){QFile f(path);return f.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(f.readAll()).object():QJsonObject{};}
inline void write(QString const& path,QJsonObject const& value){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);auto bytes=QJsonDocument(value).toJson();if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit())throw std::runtime_error("Kaydedilen düzenleme bağlantısı yazılamadı.");}
inline QString digest(QString const& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Kaydedilen düzenleme okunamadı.");return QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());}
inline void remember(QString const& queue,QJsonObject const& binding,QString const& snapshot){
    if(QFileInfo(binding.value("queue").toString()).absoluteFilePath()!=QFileInfo(queue).absoluteFilePath())throw std::runtime_error("Düzenleme başka bir kuyruğa ait.");
    QJsonObject record{{"id",QFileInfo(snapshot).baseName()},{"job_id",binding.value("job_id")},{"snapshot",snapshot},
        {"sha256",digest(snapshot)},{"preserve_margins",binding.value("preserve_margins").toBool(true)},{"active",true}};
    write(path(queue,binding.value("job_id").toString()),record);
}
inline void acknowledge(QString const& queue,QString const& job){auto p=path(queue,job);auto record=read(p);if(record.isEmpty())return;record.insert("active",false);write(p,record);}
inline int enqueue(QString const& queue,QJsonObject const& options){
    int count=0;
    for(auto const& file:QDir(queue+".saved").entryInfoList({"*.json"},QDir::Files)){
        auto record=read(file.absoluteFilePath());if(!record.value("active").toBool())continue;
        if(digest(record.value("snapshot").toString())!=record.value("sha256").toString())throw std::runtime_error("Kaydedilen düzenleme değişmiş; kitabı yeniden kaydedin.");
        auto request=record;request.remove("active");request.insert("export_options",options);request.insert("run_while_paused",true);
        write(queue+".edits/"+record.value("id").toString()+".request.json",request);
        record.insert("active",false);write(file.absoluteFilePath(),record);++count;
    }
    return count;
}
}
