// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>

class TaskStatus;
class AcceleratableOperations;
namespace imageproc { class GrayImage; class AffineImageTransform; }
namespace dewarping { class DistortionModelBuilder; }
namespace deskew {
// Word baselines in original image coordinates. Connected words are joined
// only across small gaps; persistent column gutters are never crossed.
bool unifiedTraceWords(imageproc::GrayImage const& image,
    imageproc::AffineImageTransform const& transform,
    std::shared_ptr<AcceleratableOperations> const& ops,
    TaskStatus const& status, dewarping::DistortionModelBuilder& output);
}
