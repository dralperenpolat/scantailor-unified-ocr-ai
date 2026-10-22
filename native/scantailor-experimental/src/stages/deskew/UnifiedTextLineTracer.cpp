// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedTextLineTracer.h"
#include "TaskStatus.h"
#include "acceleration/AcceleratableOperations.h"
#include "dewarping/DetectVerticalBounds.h"
#include "dewarping/DistortionModelBuilder.h"
#include "imageproc/AffineImageTransform.h"
#include "imageproc/AffineTransform.h"
#include "imageproc/GrayImage.h"
#include "imageproc/Binarize.h"
#include "imageproc/ConnCompEraserExt.h"
#include "imageproc/Morphology.h"
#include "imageproc/RasterOp.h"
#include "imageproc/PolygonRasterizer.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <numeric>

namespace deskew {
namespace {
using namespace imageproc;
double quantile(std::vector<double> values, double q) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    double at = q * (values.size() - 1);
    auto i = size_t(at);
    return values[i] + (values[std::min(i + 1, values.size() - 1)] - values[i]) * (at - i);
}
bool black(BinaryImage const& image, int x, int y) {
    return (image.data()[y * image.wordsPerLine() + (x >> 5)] >> (31 - (x & 31))) & 1;
}
struct Word {
    QRect rect;
    double baseline = 0, slope = 0;
    std::vector<QPointF> points;
};
}

bool unifiedTraceWords(imageproc::GrayImage const& image,
    imageproc::AffineImageTransform const& transform,
    std::shared_ptr<AcceleratableOperations> const& ops,
    TaskStatus const& status, dewarping::DistortionModelBuilder& output) {
    using namespace imageproc;
    AffineImageTransform small(transform);
    small.scaleTo(QSize(1500, 1500), Qt::KeepAspectRatio);
    QRect rect = small.transformedCropArea().boundingRect().toAlignedRect();
    if (rect.isEmpty()) return false;
    GrayImage sample(ops->affineTransform(image, small.transform(), rect, OutsidePixels::assumeColor(Qt::white)));
    BinaryImage binary(binarizeWolf(sample, 31, .30f));
    auto crop = small.transformedCropArea(); crop.translate(-rect.x(), -rect.y());
    PolygonRasterizer::fillExcept(binary, WHITE, crop, Qt::WindingFill);
    int const width = binary.width(), height = binary.height();
    std::vector<double> heights;
    auto glyph = [&](ConnComp const& c) {
        return c.height() >= 4 && c.height() < height * .035 && c.width() >= 2 &&
            c.width() < width * .045 && c.pixCount() >= 5;
    };
    ConnCompEraser glyphs(binary, CONN8);
    for (auto c = glyphs.nextConnComp(); !c.isNull(); c = glyphs.nextConnComp())
        if (glyph(c)) heights.push_back(c.height());
    if (heights.size() < 30) return false;
    double const charHeight = quantile(heights, .5);
    BinaryImage clean(binary.size(), WHITE);
    ConnCompEraserExt components(binary, CONN8);
    for (auto c = components.nextConnComp(); !c.isNull(); c = components.nextConnComp()) {
        if (!glyph(c) && !(c.height() < charHeight * 1.6 && c.width() < charHeight * 18 && c.pixCount() >= 5)) continue;
        auto mask = components.computeConnCompImage();
        rasterOp<RopOr<RopSrc, RopDst>>(clean, c.rect(), mask, QPoint());
    }
    status.throwIfCancelled();
    BinaryImage joined(closeBrick(clean, QSize(std::max(3, int(charHeight * .75)), 1)));
    ConnCompEraserExt wordsCC(joined, CONN8);
    std::vector<Word> words;
    for (auto c = wordsCC.nextConnComp(); !c.isNull(); c = wordsCC.nextConnComp()) {
        if (c.width() < charHeight * 1.25 || c.height() < charHeight * .5 ||
            c.height() > charHeight * 2.2 + c.width() * .16 || c.width() > width * .85 || c.pixCount() < 12) continue;
        auto mask = wordsCC.computeConnCompImage();
        std::vector<double> allY;
        std::vector<int> bottom(c.width(), -1);
        for (int x = 0; x < c.width(); ++x) {
            for (int y = c.height() - 1; y >= 0; --y) {
                if (black(mask, x, y) && black(clean, x + c.rect().x(), y + c.rect().y())) {
                    bottom[x] = y; allY.push_back(y); break;
                }
            }
        }
        if (allY.size() < 5) continue;
        Word word; word.rect = c.rect(); word.baseline = quantile(allY, .55) + c.rect().y();
        int const step = std::max(5, int(charHeight * 2.8));
        for (int start = 0; start < c.width(); start += step) {
            double xSum = 0; std::vector<double> ys;
            for (int x = start; x < std::min(c.width(), start + step); ++x)
                if (bottom[x] >= 0) { xSum += x; ys.push_back(bottom[x]); }
            if (ys.size() >= 4) word.points.emplace_back(c.rect().x() + xSum / ys.size(), c.rect().y() + quantile(ys, .55));
        }
        if (word.points.size() < 2) word.points = {QPointF(c.rect().x() + c.width() * .5, word.baseline)};
        double mx = 0, my = 0, xy = 0, xx = 0;
        for (auto p : word.points) { mx += p.x(); my += p.y(); }
        mx /= word.points.size(); my /= word.points.size();
        for (auto p : word.points) { xy += (p.x() - mx) * (p.y() - my); xx += (p.x() - mx) * (p.x() - mx); }
        if (xx > 0) word.slope = xy / xx;
        if (std::abs(word.slope) <= .3) words.push_back(std::move(word));
    }
    if (words.size() < 10) return false;
    std::sort(words.begin(), words.end(), [](Word const& a, Word const& b) { return a.rect.x() < b.rect.x(); });
    QRect text;
    for (auto const& word : words) text |= word.rect;
    std::vector<double> density(width);
    for (int x = text.left(); x <= text.right(); ++x)
        for (int y = text.top(); y <= text.bottom(); ++y) density[x] += black(clean, x, y) / double(text.height());
    std::vector<double> smooth(width); int radius = std::max(1, int(charHeight * .4));
    for (int x = text.left(); x <= text.right(); ++x) {
        for (int dx = -radius; dx <= radius; ++dx) smooth[x] += density[std::max(0, std::min(width - 1, x + dx))];
        smooth[x] /= 2 * radius + 1;
    }
    double const valley = std::max(.003, *std::max_element(smooth.begin(), smooth.end()) * .08);
    std::vector<double> separators;
    for (int start = text.left(); start <= text.right();) {
        if (smooth[start] >= valley) { ++start; continue; }
        int end = start; while (end <= text.right() && smooth[end] < valley) ++end;
        if (end - start > charHeight * 1.1 && start > text.left() + text.width() * .2 && end < text.right() - text.width() * .2)
            separators.push_back((start + end - 1) * .5);
        start = end;
    }
    struct Edge { double cost; size_t from, to; };
    std::vector<Edge> edges;
    for (size_t i = 0; i < words.size(); ++i) {
        auto const& a = words[i]; double best = 1e9; size_t selected = i;
        double right = a.rect.x() + a.rect.width();
        for (size_t j = i + 1; j < words.size(); ++j) {
            auto const& b = words[j]; double gap = b.rect.x() - right;
            if (gap >= charHeight * 4) break;
            if (gap < -charHeight * .3 || b.rect.center().x() <= a.rect.center().x()) continue;
            bool crosses = false;
            for (double separator : separators) if (right < separator && b.rect.x() > separator) crosses = true;
            if (crosses) continue;
            double dy = (b.baseline - b.rect.width() * .5 * b.slope) - (a.baseline + a.rect.width() * .5 * a.slope);
            if (std::abs(dy) >= charHeight * .6 + std::max(0., gap) * .14) continue;
            double cost = std::max(0., gap) + std::abs(dy) * 3;
            if (cost < best) { best = cost; selected = j; }
        }
        if (selected != i) edges.push_back({best, i, selected});
    }
    std::sort(edges.begin(), edges.end(), [](Edge const& a, Edge const& b) { return a.cost < b.cost; });
    std::vector<size_t> parent(words.size()); std::iota(parent.begin(), parent.end(), 0);
    std::vector<bool> incoming(words.size()), outgoing(words.size());
    auto root = [&](size_t i) { while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; } return i; };
    for (auto const& edge : edges) {
        if (outgoing[edge.from] || incoming[edge.to]) continue;
        size_t a = root(edge.from), b = root(edge.to); if (a == b) continue;
        parent[b] = a; outgoing[edge.from] = true; incoming[edge.to] = true;
    }
    std::map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < words.size(); ++i) groups[root(i)].push_back(i);
    std::list<std::vector<QPointF>> lines;
    for (auto const& group : groups) {
        auto const& ids = group.second;
        if (words[ids.back()].rect.right() - words[ids.front()].rect.left() < width * .18) continue;
        std::vector<QPointF> line;
        for (auto id : ids) line.insert(line.end(), words[id].points.begin(), words[id].points.end());
        if (line.size() >= 4) lines.push_back(std::move(line));
    }
    if (lines.size() < 5) return false;
    lines.sort([](auto const& a, auto const& b) { return a.front().y() < b.front().y(); });
    auto bounds = dewarping::detectVerticalBounds(lines, 10.);
    // A densely populated left column must not make its right edge become
    // the page's right boundary. Fit the two outer margins independently.
    double x0 = width, x1 = 0;
    for (auto const& line : lines) { x0 = std::min(x0, line.front().x()); x1 = std::max(x1, line.back().x()); }
    std::list<std::vector<QPointF>> leftLines, rightLines;
    for (auto const& line : lines) {
        if (line.front().x() <= x0 + (x1 - x0) * .3) leftLines.push_back(line);
        if (line.back().x() >= x1 - (x1 - x0) * .3) rightLines.push_back(line);
    }
    auto left = dewarping::detectVerticalBounds(leftLines, 10.).first;
    auto right = dewarping::detectVerticalBounds(rightLines, 10.).second;
    bounds.first = !left.isNull() ? left : QLineF(x0 - charHeight, 0, x0 - charHeight, height);
    bounds.second = !right.isNull() ? right : QLineF(x1 + charHeight, 0, x1 + charHeight, height);
    if (bounds.first.isNull() || bounds.second.isNull()) return false;
    QTransform inverse = small.transform().inverted();
    auto toOriginal = [&](QPointF const& p) { return inverse.map(p + rect.topLeft()); };
    output.setVerticalBounds(QLineF(toOriginal(bounds.first.p1()), toOriginal(bounds.first.p2())),
        QLineF(toOriginal(bounds.second.p1()), toOriginal(bounds.second.p2())));
    for (auto& line : lines) {
        for (auto& p : line) p = toOriginal(p);
        output.addHorizontalCurve(line);
    }
    status.throwIfCancelled();
    return true;
}
}
