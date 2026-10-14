// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedI18n.h"
#include "UnifiedRuntime.h"
#include "UnifiedAiReview.h"
#include "ImageLoader.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QMap>
#include <stdexcept>

namespace unified_ai {
namespace {
QRectF box(QJsonValue const& value){auto a=value.toArray();if(a.size()!=4)return {};return QRectF(QPointF(a[0].toDouble(),a[1].toDouble()),QPointF(a[2].toDouble(),a[3].toDouble())).normalized();}
QRectF pixelRect(QRectF r,PageResult const& p){return {r.x()*p.scale-p.pixelCrop.x(),r.y()*p.scale-p.pixelCrop.y(),r.width()*p.scale,r.height()*p.scale};}
struct Process {
    QProcess child;QByteArray buffer,errors;
    ~Process(){if(child.state()!=QProcess::NotRunning){child.terminate();if(!child.waitForFinished(700)){child.kill();child.waitForFinished(700);}}}
    QJsonObject request(QJsonObject const& input,std::function<bool()> const& cancel){
        child.write(QJsonDocument(input).toJson(QJsonDocument::Compact)+"\n");child.waitForBytesWritten(1000);
        QElapsedTimer clock;clock.start();
        while(clock.elapsed()<120000){
            if(cancel())throw std::runtime_error("Analiz durduruldu.");
            child.waitForReadyRead(100);buffer+=child.readAllStandardOutput();errors+=child.readAllStandardError();
            if(errors.size()>8192)errors=errors.right(8192);
            int end;while((end=buffer.indexOf('\n'))>=0){auto line=buffer.left(end);buffer.remove(0,end+1);
                if(!line.startsWith("STOUAI "))continue;
                auto result=QJsonDocument::fromJson(line.mid(7)).object();if(result.value("id")==input.value("id"))return result;
            }
            if(child.state()==QProcess::NotRunning)throw std::runtime_error((unified_i18n::text("Paddle işlemi kapandı: ")+unified_i18n::text(errors.right(1800))).toStdString());
            if(buffer.size()>4*1024*1024)throw std::runtime_error("Paddle yanıtı sınırı aştı.");
        }
        throw std::runtime_error("Paddle analizi 120 saniyede yanıt vermedi. Mevcut sayfa değişmedi.");
    }
};
}
void run(std::vector<unified_content::Sample> const& samples,std::vector<int> const& indices,Options const& opt,QString const& directory,
         std::function<bool()> const& cancelled,std::function<void(PageResult)> const& completed){
    auto python=unified_runtime::paddlePython(opt.root,opt.device);
    auto script=opt.root+"/src/scantailor_ocr_unified/paddle_visual_runtime.py";
    if(!QFileInfo::exists(python)||!QFileInfo::exists(script))throw std::runtime_error("Paddle görsel analiz bileşeni bulunamadı. OCR motoru sekmesinden Paddle paketini kurun.");
    Process process;auto env=QProcessEnvironment::systemEnvironment();env.insert("PYTHONUTF8","1");env.insert("PYTHONIOENCODING","utf-8");env.insert("OMP_NUM_THREADS","2");env.insert("MKL_NUM_THREADS","2");
    process.child.setProcessEnvironment(env);process.child.setWorkingDirectory(opt.root);
    process.child.start(python,{"-u",script});if(!process.child.waitForStarted(10000))throw std::runtime_error(process.child.errorString().toStdString());
    QDir().mkpath(directory);
    for(int index:indices){
        if(cancelled())break;if(index<0||size_t(index)>=samples.size())continue;
        auto const& s=samples[index];PageResult out;out.index=index;out.proposal.index=index;
        try{
            out.scale=std::min(1.,1600./std::max(s.crop.width(),s.crop.height()));
            auto transform=s.transform->clone();transform->scale(out.scale,out.scale);
            out.pixelCrop=transform->transformedCropArea().boundingRect().toAlignedRect();
            if(out.pixelCrop.isEmpty()||out.pixelCrop.width()>1800||out.pixelCrop.height()>1800)throw std::runtime_error("Sayfa geometrisi olağandışı. Önce geometriyi kontrol edin.");
            QImage source=ImageLoader::load(s.page.imageId());if(source.isNull())throw std::runtime_error("Kaynak fotoğraf okunamadı.");
            QImage image=transform->materialize(source,out.pixelCrop,Qt::white,std::make_shared<NonAcceleratedOperations>());source=QImage();
            out.imagePath=directory+QString("/page-%1.png").arg(index);if(!image.save(out.imagePath))throw std::runtime_error("Analiz önizlemesi kaydedilemedi.");
            auto reference=unified_content::referenceFor(s,samples);
            if(reference.contains("envelope")){auto a=reference.value("envelope").toArray();auto r=pixelRect(QRectF(a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()),out);reference.insert("envelope",QJsonArray{r.left(),r.top(),r.right(),r.bottom()});}
            out.analysis=process.request({{"id",index},{"image",out.imagePath},{"device",opt.device},{"layout",opt.layout},{"reference",reference}},cancelled);
            if(!out.analysis.value("ok").toBool())throw std::runtime_error(out.analysis.value("error").toString().toStdString());
            auto rect=box(out.analysis.value("candidate"));
            if(!rect.isEmpty())out.proposal.rect=QRectF((rect.x()+out.pixelCrop.x())/out.scale,(rect.y()+out.pixelCrop.y())/out.scale,rect.width()/out.scale,rect.height()/out.scale).intersected(s.crop);
            out.proposal.safe=false;QStringList warnings;for(auto v:out.analysis.value("warnings").toArray())warnings<<unified_i18n::display(v.toString());out.proposal.reason=warnings.join(' ');
            if(out.proposal.rect.isEmpty()&&out.proposal.reason.isEmpty())out.proposal.reason=unified_i18n::text("Güvenilir içerik alanı bulunamadı; mevcut seçim korundu.");
        }catch(std::exception const& e){out.error=unified_i18n::text(e.what());}
        completed(std::move(out));if(cancelled())break;
    }
}
QString label(QString const& value){
    static QMap<QString,QString> labels={{"text",unified_i18n::text("Metin")},{"doc_title",unified_i18n::text("Kitap başlığı")},{"paragraph_title",unified_i18n::text("Bölüm başlığı")},{"number",unified_i18n::text("Numara")},{"footnote",unified_i18n::text("Dipnot")},{"header",unified_i18n::text("Üst bilgi")},{"footer",unified_i18n::text("Alt bilgi")},{"table",unified_i18n::text("Tablo")},{"chart",unified_i18n::text("Grafik")},{"content",unified_i18n::text("İçindekiler")},{"reference",unified_i18n::text("Kaynakça")},{"image",unified_i18n::text("Görsel")},{"abstract",unified_i18n::text("Özet")},{"aside_text",unified_i18n::text("Kenar metni")}};
    return labels.value(value,value);
}
QImage preview(PageResult const& out,unified_content::Sample const& sample,bool text,bool layout){
    QImage image(out.imagePath);if(image.isNull())return image;QPainter p(&image);
    if(text)for(auto v:out.analysis.value("boxes").toArray()){auto o=v.toObject();p.setPen(QPen(o.value("excluded").toBool()?QColor(240,125,0):QColor(0,160,210),2));p.drawRect(box(o.value("box")));}
    if(layout){p.setFont(QFont("Arial",11));for(auto v:out.analysis.value("regions").toArray()){auto o=v.toObject();auto r=box(o.value("box"));p.setPen(QPen(QColor(155,40,175),1));p.drawRect(r);p.drawText(r.topLeft()+QPointF(2,14),label(o.value("label").toString()));}}
    p.setPen(QPen(QColor(210,50,45),3));p.drawRect(pixelRect(sample.rect,out));
    if(!out.proposal.rect.isEmpty()){p.setPen(QPen(QColor(0,155,65),3,Qt::DashLine));p.drawRect(pixelRect(out.proposal.rect,out));}
    return image;
}
}
