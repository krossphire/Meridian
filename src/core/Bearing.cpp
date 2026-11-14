/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Bearing.h"

#include <cmath>

std::optional<ClickBearing> bearingFromClick(QPointF pos, QPointF center, double radius)
{
    if (!(radius > 0.0) || !std::isfinite(radius) || !std::isfinite(pos.x()) || !std::isfinite(pos.y()))
        return std::nullopt;
    const double east = pos.x() - center.x();
    const double north = center.y() - pos.y();
    const double rhoPx = std::hypot(east, north);
    if (rhoPx > radius || rhoPx < 1.0)
        return std::nullopt;

    double degrees = std::atan2(east, north) * (180.0 / M_PI);
    if (degrees < 0.0)
        degrees += 360.0;
    if (degrees >= 360.0)
        degrees -= 360.0;
    return ClickBearing{degrees, rhoPx / radius};
}

double clampRt21Azimuth(double degrees)
{
    if (!std::isfinite(degrees))
        return 0.0;
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0)
        degrees += 360.0;
    double tenths = std::round(degrees * 10.0) / 10.0;
    if (tenths >= 360.0 || tenths < 0.0)
        tenths = 0.0;
    if (tenths > 359.9)
        tenths = 359.9;
    return tenths;
}
