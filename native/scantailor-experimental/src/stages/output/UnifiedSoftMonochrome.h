// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace output {
// Same tonal mapping as the PDF bridge's soft_monochrome(), retaining gray
// stroke edges. No new geometry, hard thresholding or invented detail.
inline QImage unifiedSoftMonochrome(QImage const& source) {
    if (source.isNull()) return {};
    QImage gray=source.convertToFormat(QImage::Format_Grayscale8);
    std::array<quint64,256> histogram{};
    for (int y=0;y<gray.height();++y) {
        auto row=gray.constScanLine(y);
        for (int x=0;x<gray.width();++x) ++histogram[row[x]];
    }
    double total=double(gray.width())*gray.height(), mean=0;
    for (int i=0;i<256;++i) mean+=i*double(histogram[i])/total;
    double probability=0, partialMean=0, bestVariance=0;
    int cut=0;
    for (int i=0;i<256;++i) {
        double p=double(histogram[i])/total;
        partialMean*=probability;
        probability+=p;
        double other=1-probability;
        if (std::min(probability,other)<std::numeric_limits<float>::epsilon() ||
            std::max(probability,other)>1-std::numeric_limits<float>::epsilon()) continue;
        partialMean=(partialMean+i*p)/probability;
        double otherMean=(mean-probability*partialMean)/other;
        double variance=probability*other*(partialMean-otherMean)*(partialMean-otherMean);
        if (variance>bestVariance) { bestVariance=variance;cut=i; }
    }
    quint64 inkCount=0;
    for (int i=0;i<cut;++i) inkCount+=histogram[i];
    if (inkCount<30) return gray;
    auto ranked=[&](quint64 rank) {
        quint64 sum=0;
        for (int i=0;i<cut;++i) {sum+=histogram[i];if (sum>rank) return i;}
        return cut;
    };
    double rank=.2*double(inkCount-1), lo=std::floor(rank);
    double black=ranked(quint64(lo))+(rank-lo)*(ranked(quint64(std::ceil(rank)))-ranked(quint64(lo)));
    double white=std::min(245.,cut+(245.-cut)*.35);
    if (white-black<12) return gray;
    std::array<uchar,256> mapping{};
    for (int i=0;i<256;++i) {
        float value=(float(i)-float(black))*255.f/float(white-black);
        mapping[i]=uchar(std::max(0.f,std::min(255.f,value)));
    }
    QImage result(gray.size(),QImage::Format_Grayscale8);
    for (int y=0;y<gray.height();++y) {
        auto src=gray.constScanLine(y);auto dst=result.scanLine(y);
        for (int x=0;x<gray.width();++x) dst[x]=mapping[src[x]];
    }
    return result;
}
}
