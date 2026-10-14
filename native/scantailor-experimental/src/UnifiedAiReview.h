// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedContentReview.h"
#include <QString>
#include <QJsonObject>
#include <functional>

namespace unified_ai {
struct Options {QString root,device="cpu";bool layout=true;};
struct PageResult {
    int index=-1;
    QString imagePath,error;
    QJsonObject analysis;
    unified_content::Proposal proposal;
    QRect pixelCrop;
    double scale=1.;
};
// One persistent inference process, one materialized source at a time. Result
// images live on disk; analyzing a large book does not retain all page pixels.
void run(std::vector<unified_content::Sample> const& samples,std::vector<int> const& indices,
         Options const& options,QString const& directory,
         std::function<bool()> const& cancelled,std::function<void(PageResult)> const& completed);
QImage preview(PageResult const& result,unified_content::Sample const& sample,bool text,bool layout);
QString label(QString const& value);
}
