// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PageInfo.h"
#include "stages/deskew/Params.h"
#include "stages/deskew/UnifiedAutoDewarp.h"
#include "imageproc/AffineImageTransform.h"
#include <functional>

class ProjectPages;
class StageSequence;
namespace unified_geometry {
struct Sample {
    PageInfo page;
    int ordinal;
    deskew::Params params;
    imageproc::AffineImageTransform transform;
    QString state;
};
struct Candidate {
    QString name,path,reason;
    deskew::Params params;
    deskew::UnifiedGeometryMetrics metrics;
    double error=0,score=0;
    bool accepted=false;
};
struct Result {
    int index=-1,recommended=-1;
    QString source,current,error,report;
    double seconds=0,currentError=0;
    bool cached=false;
    std::vector<Candidate> candidates;
};
std::vector<Sample> collect(ProjectPages const& pages,StageSequence& stages);
std::shared_ptr<imageproc::AbstractImageTransform> currentTransform(Sample const& sample,deskew::Params const& params);
void run(std::vector<Sample> const& samples,std::vector<int> const& indices,QString const& root,
    QString const& device,bool useAi,QString const& directory,std::function<bool()> const& cancelled,
    std::function<void(Result)> const& completed,bool renderPreviews=true);
bool apply(Sample const& sample,Candidate const& candidate,StageSequence& stages,bool allowManual);
bool matchesCurrent(Sample const& sample,StageSequence& stages,Candidate const* applied = nullptr);
bool undo(Sample const& sample,Candidate const& candidate,StageSequence& stages);
QString state(deskew::Params const& params);
}
