/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Compass.h"

#include <cmath>

namespace {

const char *kLabels[kCompassCount] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

} // namespace

const char *compassLabel(int index)
{
    if (index < 0 || index >= kCompassCount)
        return kLabels[0];
    return kLabels[index];
}

int nearestCompassIndex(double headingDegrees)
{
    if (!std::isfinite(headingDegrees))
        return 0;
    double heading = std::fmod(headingDegrees, 360.0);
    if (heading < 0.0)
        heading += 360.0;
    // 22.5° is the boundary. 22.5 belongs to NE; 337.5 belongs to N.
    const int index = static_cast<int>(std::floor((heading + 22.5) / 45.0)) % kCompassCount;
    return index < 0 ? 0 : index;
}
