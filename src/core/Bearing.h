/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Click-to-bearing follows QLog ui/RotatorWidget::setBearingFromMapPosition:
 * azimuth = atan2(east, north), in degrees clockwise from north.
 */

#pragma once

#include <QPointF>

#include <optional>

struct ClickBearing {
    double azimuth = 0; // degrees, 0..360
    double rho = 0;     // 0 at the station, 1 at the antipodal rim
};

// radius is the globe disk in the same coordinate space as pos and center.
// Clicks outside the disk, and the exact center, return nullopt.
std::optional<ClickBearing> bearingFromClick(QPointF pos, QPointF center, double radius);

// RT-21 / Hamlib max_az is 359.9. Result is a tenth of a degree in [0.0, 359.9].
// 360 degrees (and values that round to it) become 0.0.
double clampRt21Azimuth(double degrees);
