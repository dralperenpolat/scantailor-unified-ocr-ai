// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <algorithm>
#include <cmath>
#include <vector>

namespace output {
// Same display-only profile as output_appearance.local_soft_monochrome().
// Rolling sums use O(width) auxiliary memory even for very large native output.
inline QImage unifiedLocalMonochrome(QImage const& source) {
    if (source.isNull()) return {};
    QImage gray = source.convertToFormat(QImage::Format_Grayscale8);
    int const width = gray.width(), height = gray.height();
    int low = 255, high = 0;
    for (int y = 0; y < height; ++y) {
        auto row = gray.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            low = std::min(low, int(row[x])); high = std::max(high, int(row[x]));
        }
    }
    if (high - low < 2) return gray;
    int const radius = std::max(15, std::min(150, int(std::min(width, height) * .03 + .5)));
    int const window = 2 * radius + 1;
    double const area = double(window) * window;
    auto reflect = [](int p, int size) {
        int q = p % (2 * size); if (q < 0) q += 2 * size;
        return q < size ? q : 2 * size - q - 1;
    };
    std::vector<double> sum(width, 0), square(width, 0);
    auto addRow = [&](int y, double sign) {
        auto row = gray.constScanLine(reflect(y, height));
        for (int x = 0; x < width; ++x) {
            double v = row[x]; sum[x] += sign * v; square[x] += sign * v * v;
        }
    };
    for (int y = -radius; y <= radius; ++y) addRow(y, 1);
    QImage result(gray.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        if (y) { addRow(y - radius - 1, -1); addRow(y + radius, 1); }
        double localSum = 0, localSquare = 0;
        for (int x = -radius; x <= radius; ++x) {
            int xx = reflect(x, width); localSum += sum[xx]; localSquare += square[xx];
        }
        auto src = gray.constScanLine(y); auto dst = result.scanLine(y);
        for (int x = 0; x < width; ++x) {
            if (x) {
                int oldX = reflect(x - radius - 1, width), newX = reflect(x + radius, width);
                localSum += sum[newX] - sum[oldX]; localSquare += square[newX] - square[oldX];
            }
            double mean = localSum / area;
            double sd = std::sqrt(std::max(0., localSquare / area - mean * mean));
            double threshold = mean * (1 + .18 * (sd / 128 - 1));
            double value = std::max(0., std::min(255., 127.5 + (src[x] - threshold) * 5.5));
            if (src[x] < 32) value = std::min(value, double(src[x]));
            dst[x] = uchar(value);
        }
    }
    return result;
}
}
