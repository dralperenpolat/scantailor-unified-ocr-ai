// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedAutoDewarp.h"
#include "imageproc/AffineImageTransform.h"
#include <Eigen/Dense>
#include <numeric>

namespace deskew {
// Jointly estimate a smooth ruled sheet from text baselines. Each line has its
// own vertical position; shared coefficients describe curl and perspective.
// Every fourth line is held out, so the quality check can detect overfitting.
inline UnifiedDewarpCandidate unifiedFitTextSheet(
    std::deque<std::vector<QPointF>> const& input,
    std::pair<QLineF,QLineF> const& verticalBounds,
    imageproc::AffineImageTransform const& transform,int degree=3) {
    UnifiedDewarpCandidate result;result.source=degree==1?unified_i18n::text("Perspektif satır modeli"):(degree==3?unified_i18n::text("Ortak satır modeli"):unified_i18n::text("Esnek satır modeli"));
    result.metrics.reason=unified_i18n::text("Modeli doğrulayacak yeterli satır desteği yok");
    auto paper=transform.transformedCropArea().boundingRect();if(paper.isEmpty())return result;
    struct Line{std::vector<double> x,y,w;double v=0;};std::vector<Line> lines;
    int count=0;
    for(auto const& curve:input){
        ++count;if(count%4==0||curve.size()<4)continue;
        auto first=transform.transform().map(curve.front()),last=transform.transform().map(curve.back());
        if(last.x()-first.x()<paper.width()*.15)continue;
        Line line;size_t n=std::min(size_t(32),curve.size());
        for(size_t i=0;i<n;++i){auto p=transform.transform().map(curve[i*(curve.size()-1)/(n-1)]);line.x.push_back(2*(p.x()-paper.center().x())/paper.width());line.y.push_back((p.y()-paper.top())/paper.height());line.w.push_back(1.);}
        line.v=std::accumulate(line.y.begin(),line.y.end(),0.)/n;lines.push_back(std::move(line));
    }
    if(lines.size()<5)return result;
    double low=1,high=0;for(auto const& line:lines){low=std::min(low,line.v);high=std::max(high,line.v);}if(high-low<.25)return result;
    int dim=degree*2;Eigen::VectorXd coefficients=Eigen::VectorXd::Zero(dim);
    auto features=[&](double x,double v){Eigen::VectorXd f(dim);double term=x;for(int j=0;j<degree;++j){f[2*j]=term;f[2*j+1]=term*v;term*=x;}return f;};
    for(int iteration=0;iteration<10;++iteration){
        Eigen::MatrixXd lhs=Eigen::MatrixXd::Zero(dim,dim);Eigen::VectorXd rhs=Eigen::VectorXd::Zero(dim);
        for(auto const& line:lines){
            double weight=0,meanY=0;Eigen::VectorXd mean=Eigen::VectorXd::Zero(dim);
            for(size_t i=0;i<line.x.size();++i){mean+=line.w[i]*features(line.x[i],line.v);meanY+=line.w[i]*line.y[i];weight+=line.w[i];}
            if(weight<1)continue;
            mean/=weight;meanY/=weight;
            for(size_t i=0;i<line.x.size();++i){auto f=(features(line.x[i],line.v)-mean).eval();double w=line.w[i]/line.x.size();lhs.noalias()+=w*f*f.transpose();rhs.noalias()+=w*f*(line.y[i]-meanY);}
        }
        for(int i=0;i<dim;++i)lhs(i,i)+=lines.size()*1e-5*(1+i/2)*(1+i/2);
        coefficients=lhs.ldlt().solve(rhs);
        for(int i=0;i<dim;++i)if(!unifiedFinite(coefficients[i])){result.metrics.reason=unified_i18n::text("Satır modeli kararlı biçimde çözülemedi");return result;}
        for(auto& line:lines){
            double position=0,weight=0;
            for(size_t i=0;i<line.x.size();++i){double a=0,b=0,term=line.x[i];for(int j=0;j<degree;++j){a+=coefficients[2*j]*term;b+=coefficients[2*j+1]*term;term*=line.x[i];}
                if(1+b<.5||1+b>2){result.metrics.reason=unified_i18n::text("Satır modeli aşırı dikey esneme gerektiriyor");return result;}
                position+=line.w[i]*(line.y[i]-a)/(1+b);weight+=line.w[i];}
            if(weight>0)line.v=position/weight;
            for(size_t i=0;i<line.x.size();++i){double error=std::abs(line.y[i]-line.v-features(line.x[i],line.v).dot(coefficients));line.w[i]=std::min(1.,.0015/std::max(error,1e-9));}
        }
    }
    low=1;high=0;for(auto const& line:lines){low=std::min(low,line.v);high=std::max(high,line.v);}
    low=std::max(0.,low-.035);high=std::min(1.,high+.035);
    auto yAt=[&](double x,double v){return paper.top()+paper.height()*(v+features(2*(x-paper.center().x())/paper.width(),v).dot(coefficients));};
    auto left=transform.transform().map(verticalBounds.first),right=transform.transform().map(verticalBounds.second);
    if(left.center().x()>right.center().x())std::swap(left,right);
    auto boundaryX=[&](QLineF const& side,double v){double x=side.center().x();if(std::abs(side.dy())<1e-6)return x;for(int i=0;i<5;++i)x=side.x1()+(yAt(x,v)-side.y1())*side.dx()/side.dy();return x;};
    auto curve=[&](double v){std::vector<QPointF> points;double x0=boundaryX(left,v),x1=boundaryX(right,v);if(x1-x0<paper.width()*.25)return points;
        for(int i=0;i<=64;++i){double x=x0+(x1-x0)*i/64.;points.push_back(transform.transform().inverted().map(QPointF(x,yAt(x,v))));}return points;};
    auto top=curve(low),bottom=curve(high);if(top.empty()||bottom.empty()){result.metrics.reason=unified_i18n::text("Satır modeli sayfanın çok dar bir kısmını kapsıyor");return result;}
    result.model.setTopCurve(dewarping::Curve(top));result.model.setBottomCurve(dewarping::Curve(bottom));
    result.depth=1;result.curves=2;result.angle=2;result.metrics.reason.clear();return result;
}
}
