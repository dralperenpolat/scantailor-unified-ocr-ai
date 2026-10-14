// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PageInfo.h"
#include "stages/select_content/Params.h"
#include "imageproc/AbstractImageTransform.h"
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <functional>
#include <memory>
#include <vector>

class ProjectPages;
class StageSequence;
namespace unified_content {
struct Sample {
    PageInfo page;
    int ordinal;
    select_content::Params params;
    std::shared_ptr<imageproc::AbstractImageTransform const> transform;
    QRectF crop, rect, normalized;
};
struct Proposal {
    int index = -1;
    int peers = 0;
    bool manualReferences = false, safe = false;
    QRectF rect;
    QString reason;
    QImage preview;
};
struct Result {
    std::vector<Sample> samples;
    std::vector<Proposal> proposals;
    int manual = 0, unavailable = 0, compared = 0;
    bool cancelled = false;
    QJsonObject toJson() const;
};
std::vector<Sample> collect(ProjectPages const& pages, StageSequence& stages);
// References may be supplied separately for held-out validation. Current page
// is always excluded from its own reference set. Manual selections are immutable.
Result review(std::vector<Sample> samples,
              std::function<bool(int,int)> const& progress = {},
              std::vector<Sample> const* references = nullptr);
bool apply(Proposal const& proposal, Sample const& sample, StageSequence& stages);
QJsonObject referenceFor(Sample const& sample, std::vector<Sample> const& references);
// Explicitly reviewed AI proposals may restore missing content outside the old
// rectangle. Manual overrides require a separate user control in the dialog.
bool applyAi(Proposal const& proposal, Sample const& sample, StageSequence& stages, bool allowManual);
// Validate against live upstream geometry as well as the content rectangle.
// A cached Params object alone can survive a geometry change.
bool matchesCurrent(Sample const& sample, StageSequence& stages, QRectF const* applied = nullptr);
bool undoAi(Sample const& sample, QRectF const& applied, StageSequence& stages);
} // namespace unified_content
