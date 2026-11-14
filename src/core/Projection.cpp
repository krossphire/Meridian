/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Projection.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kDeg = M_PI / 180.0;

double wrapPi(double radians)
{
    while (radians > M_PI)
        radians -= 2.0 * M_PI;
    while (radians < -M_PI)
        radians += 2.0 * M_PI;
    return radians;
}

double wrapLon(double lon)
{
    while (lon > 180.0)
        lon -= 360.0;
    while (lon < -180.0)
        lon += 360.0;
    return lon;
}

} // namespace

UnitPoint projectAzimuthal(double homeLat, double homeLon, double lat, double lon)
{
    const double phi1 = homeLat * kDeg;
    const double phi = lat * kDeg;
    const double dlon = wrapPi((lon - homeLon) * kDeg);
    const double cosc = std::clamp(std::sin(phi1) * std::sin(phi)
                                       + std::cos(phi1) * std::cos(phi) * std::cos(dlon),
                                   -1.0, 1.0);
    const double c = std::acos(cosc);
    const double rho = c / M_PI;
    if (c < 1e-7)
        return {};

    const double az = std::atan2(std::cos(phi) * std::sin(dlon),
                                 std::cos(phi1) * std::sin(phi)
                                     - std::sin(phi1) * std::cos(phi) * std::cos(dlon));
    return {std::sin(az) * rho, std::cos(az) * rho, rho};
}

bool unprojectAzimuthal(double homeLat, double homeLon, double x, double y, double &lat, double &lon)
{
    double rho = std::hypot(x, y);
    if (rho > 1.000001)
        return false;
    if (rho > 1.0)
        rho = 1.0;
    const double c = rho * M_PI;
    const double az = std::atan2(x, y);
    const double phi1 = homeLat * kDeg;
    const double phi = std::asin(std::clamp(std::sin(phi1) * std::cos(c)
                                                + std::cos(phi1) * std::sin(c) * std::cos(az),
                                            -1.0, 1.0));
    const double lam = homeLon * kDeg
                       + std::atan2(std::sin(az) * std::sin(c) * std::cos(phi1),
                                    std::cos(c) - std::sin(phi1) * std::sin(phi));
    lat = phi / kDeg;
    lon = wrapLon(lam / kDeg);
    return true;
}
