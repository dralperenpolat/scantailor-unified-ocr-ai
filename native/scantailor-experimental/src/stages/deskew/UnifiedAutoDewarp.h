// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedGeometryCheck.h"
#include "dewarping/DistortionModel.h"
#include <deque>
#include <memory>

class TaskStatus;
class DebugImages;
class AcceleratableOperations;
namespace imageproc { class GrayImage; class AffineImageTransform; }
namespace deskew {
class ParamsDewarping;
struct UnifiedDewarpCandidate {
    QString source;
    dewarping::DistortionModel model;
    UnifiedGeometryMetrics metrics;
    double lineError = 0;
    double validationError = std::numeric_limits<double>::infinity();
    double score = std::numeric_limits<double>::infinity();
    double depth=-1,curves=-1,angle=-1;
    bool accepted() const { return model.isValid() && metrics.reason.isEmpty() && unifiedFinite(score); }
};
struct UnifiedDewarpSelection {
    dewarping::DistortionModel model;
    QString source;
    QString report;
    std::vector<UnifiedDewarpCandidate> candidates;
    double depth=-1,curves=-1,angle=-1;
    double currentError=std::numeric_limits<double>::infinity();
    double currentValidationError=std::numeric_limits<double>::infinity();
    int lineCount=0;
};

// Compare the same text evidence across candidates. A robust residual in
// thousandths of line width; sampling and sorting are bounded per text line.
double unifiedTextLineError(std::deque<std::vector<QPointF>> const& lines,
    std::function<QPointF(QPointF const&)> const& mapper, bool removeGlobalSkew = false);
int unifiedChooseDewarpCandidate(std::vector<UnifiedDewarpCandidate> const& candidates);

// Trace once, then independently build and validate text, edge and mixed models.
// No OCR or GPU model is required, and original image pixels are never modified.
UnifiedDewarpSelection unifiedSelectAutoDewarp(imageproc::GrayImage const& image,
    imageproc::AffineImageTransform const& transform, ParamsDewarping const& params,
    double fov, std::shared_ptr<AcceleratableOperations> const& ops,
    TaskStatus const& status, DebugImages* dbg = nullptr,
    std::vector<UnifiedDewarpCandidate> const& extra = {},
    std::function<QPointF(QPointF const&)> const& currentMapper = {}, bool optimizeStrength = false,
    QString const& tracePath = {});
}
