// SPDX-License-Identifier: GPL-3.0-or-later
// Known, independently generated baselines test the actual native transform.
#include "stages/deskew/UnifiedAutoDewarp.h"
#include "stages/deskew/UnifiedTextLineTracer.h"
#include "stages/deskew/ParamsDewarping.h"
#include "dewarping/DistortionModelBuilder.h"
#include "dewarping/DetectVerticalBounds.h"
#include "dewarping/DewarpingImageTransform.h"
#include "acceleration/NonAcceleratedOperations.h"
#include "imageproc/AffineImageTransform.h"
#include "imageproc/GrayImage.h"
#include "TaskStatus.h"
#include <QApplication>
#include <QPainter>
#include <QDir>
#include <iostream>
#include <stdexcept>

struct Status : TaskStatus {
    bool stopped=false;
    void cancel() override { stopped=true; }
    bool isCancelled() const override { return stopped; }
    void throwIfCancelled() const override { if(stopped) throw std::runtime_error("cancelled"); }
};
void check(bool value, char const* reason) { if(!value) throw std::runtime_error(reason); }

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    auto ops=std::make_shared<NonAcceleratedOperations>(); Status status;
    std::list<std::vector<QPointF>> slantedMargins;
    for(int i=0;i<10;++i)slantedMargins.push_back({QPointF(i*40,i*50),QPointF(i*40+500,i*50)});
    auto invalidBounds=dewarping::detectVerticalBounds(slantedMargins,10.);
    check(invalidBounds.first.isNull()&&invalidBounds.second.isNull(),"near-horizontal text margins passed the vertical angle limit");
    QDir().mkpath(argc>1?argv[1]:".");
    for(int scenario=0;scenario<4;++scenario) {
        bool const columns=scenario==3;
        QImage flat(1100,1500,QImage::Format_RGB32);flat.fill(Qt::white);
        QPainter painter(&flat);QFont font("Arial");font.setPixelSize(16);painter.setFont(font);painter.setPen(Qt::black);
        std::deque<std::vector<QPointF>> truth;
        auto sourcePoint=[&](double x,double y) {
            double u=(x-550.)/1000.,v=y/1500.;
            double bend=scenario==0?0:scenario==1?110*u*u+30*u*u*u:scenario==2?55*u-110*u*v:130*u*u-65*u+60*u*v;
            return QPointF(x,y+bend);
        };
        for(int row=0;row<38;++row) for(int column=0;column<(columns?2:1);++column) {
            double y=150+row*31,x0=columns?90+column*500:90,x1=columns?490+column*500:1000;
            painter.drawText(QPointF(x0,y),columns?"Document text with separate columns, 123":"Document baseline with consistent type size, margins and punctuation, 123. A sample line of text.");
            std::vector<QPointF> line;
            for(int i=0;i<=40;++i) line.push_back(sourcePoint(x0+(x1-x0)*i/40.,y));
            truth.push_back(std::move(line));
        }
        painter.end();
        QImage warped(flat.size(),QImage::Format_RGB32);warped.fill(Qt::white);
        // Invert the analytic synthetic camera map, independently of the fitter.
        for(int y=0;y<warped.height();++y) for(int x=0;x<warped.width();++x) {
            double a=sourcePoint(x,0).y(),b=sourcePoint(x,1500).y()-a;
            double sy=(y-a)*1500./b;int iy=int(std::floor(sy));double t=sy-iy;
            if(iy<0||iy+1>=flat.height())continue;
            int value=int((1-t)*qGray(flat.pixel(x,iy))+t*qGray(flat.pixel(x,iy+1)));
            warped.setPixel(x,y,qRgb(value,value,value));
        }
        imageproc::AffineImageTransform input(warped.size());
        dewarping::DistortionModelBuilder words(Vec2d(0,1));
        check(deskew::unifiedTraceWords(imageproc::GrayImage(warped),input,ops,status,words),"word trace did not find synthetic text");
        if(columns) for(auto const& line:words.horizontalCurves()) check(line.back().x()-line.front().x()<500,"column gutter was crossed");
        auto result=deskew::unifiedSelectAutoDewarp(imageproc::GrayImage(warped),input,deskew::ParamsDewarping(),0,ops,status);
        check(result.model.isValid(),"no valid model for known sheet");
        dewarping::DewarpingImageTransform transform(input.origSize(),input.origCropArea(),result.model.topCurve().polyline(),
            result.model.bottomCurve().polyline(),0,dewarping::DepthPerception(result.depth),dewarping::DepthPerception(result.curves),dewarping::DepthPerception(result.angle));
        double before=deskew::unifiedTextLineError(truth,[](auto p){return p;},true);
        double after=deskew::unifiedTextLineError(truth,transform.forwardMapper());
        std::cout<<"scenario="<<scenario<<" before="<<before<<" after="<<after<<" lines="<<result.lineCount<<std::endl;
        warped.save(QString::fromLocal8Bit(argc>1?argv[1]:".")+QString("/synthetic-%1.png").arg(scenario));
        check(after<3.5,"known baselines are not straight after native correction");
        if(scenario>0) check(after<before*.35,"correction did not recover the synthetic camera distortion");
    }
    QImage blank(1000,1500,QImage::Format_RGB32);blank.fill(Qt::white);
    imageproc::AffineImageTransform input(blank.size());
    QImage sparse=blank;QPainter title(&sparse);QFont titleFont("Arial");titleFont.setPixelSize(20);title.setFont(titleFont);
    for(int row=0;row<6;++row)title.drawText(QPointF(250,200+row*130),"A short title without page-wide evidence");
    title.end();
    auto cover=deskew::unifiedSelectAutoDewarp(imageproc::GrayImage(sparse),input,deskew::ParamsDewarping(),0,ops,status);
    check(!cover.model.isValid(),"sparse title page received an unverified automatic warp");
    auto empty=deskew::unifiedSelectAutoDewarp(imageproc::GrayImage(blank),input,deskew::ParamsDewarping(),0,ops,status);
    check(!empty.model.isValid(),"blank page must not get an invented bend");
    status.cancel();bool stopped=false;
    try { deskew::unifiedSelectAutoDewarp(imageproc::GrayImage(blank),input,deskew::ParamsDewarping(),0,ops,status); }
    catch(std::runtime_error const&) { stopped=true; }
    check(stopped,"cancellation was ignored");
    std::cout<<"fast geometry ground-truth checks passed"<<std::endl;
    return 0;
}
