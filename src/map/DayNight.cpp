/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DayNight.h"

#include "core/Projection.h"

#include <algorithm>
#include <cmath>

namespace {

double smoothstep(double edge0, double edge1, double value)
{
    const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

QImage renderDayNightDisc(int pixels, double homeLat, double homeLon, const SolarPosition &sun, double utcMinutes)
{
    const int n = std::clamp(pixels, 32, 1024);
    QImage image(n, n, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    constexpr double kNight = -6.0;
    constexpr double kDay = 6.0;
    constexpr int kDayAlpha = 46;
    for (int y = 0; y < n; ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        const double uy = 1.0 - (y + 0.5) / n * 2.0;
        for (int x = 0; x < n; ++x) {
            const double ux = (x + 0.5) / n * 2.0 - 1.0;
            if (ux * ux + uy * uy > 1.0)
                continue;
            double lat = 0;
            double lon = 0;
            if (!unprojectAzimuthal(homeLat, homeLon, ux, uy, lat, lon))
                continue;
            const double elevation = solarElevation(lat, lon, sun, utcMinutes);
            const double t = smoothstep(kNight, kDay, elevation);
            if (t <= 0.0)
                continue;
            const int alpha = std::clamp(int(std::lround(kDayAlpha * t)), 0, 255);
            const int red = int(std::lround(158 + (196 - 158) * t));
            const int green = int(std::lround(166 + (186 - 166) * t));
            const int blue = int(std::lround(170 + (156 - 170) * t));
            row[x] = qPremultiply(qRgba(red, green, blue, alpha));
        }
    }
    return image;
}
