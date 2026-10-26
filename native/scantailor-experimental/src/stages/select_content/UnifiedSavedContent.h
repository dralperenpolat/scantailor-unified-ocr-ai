// Recognize only the subpixel round-off in old float32 curve projects.
// Four source-space edge witnesses and the saved dimensions must still agree.
#pragma once
#include "Params.h"
#include "dewarping/DewarpingImageTransform.h"
#include <cmath>
namespace select_content {
inline bool unifiedSavedContentMatches(Params const& params,imageproc::AbstractImageTransform const& transform){
    auto warp=dynamic_cast<dewarping::DewarpingImageTransform const*>(&transform);
    if(!warp||!warp->legacyCurvePrecision()||!params.dependencies().currentAlgorithm()||!params.contentBox().isValid())return false;
    auto map=transform.forwardMapper();auto const& box=params.contentBox();
    auto t=map(box.topEdgeMidPoint()),b=map(box.bottomEdgeMidPoint()),l=map(box.leftEdgeMidPoint()),r=map(box.rightEdgeMidPoint());
    double errors[]={t.x()-b.x(),l.y()-r.y(),t.x()-(l.x()+r.x())*.5,l.y()-(t.y()+b.y())*.5,
        r.x()-l.x()-params.contentSizePx().width(),b.y()-t.y()-params.contentSizePx().height()};
    for(double error:errors)if(!std::isfinite(error)||std::abs(error)>.05)return false;
    return true;
}
}
