/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Solar position from the NOAA solar calculator equations. No network.
 */

#pragma once

#include <QDateTime>

struct SolarPosition {
    double azimuth = 0;            // degrees, clockwise from north
    double elevation = 0;          // degrees above the horizon, refraction included
    double declination = 0;        // degrees
    double equationOfTime = 0;     // minutes
    double subsolarLatitude = 0;   // degrees
    double subsolarLongitude = 0;  // degrees, east positive
};

// Position of the sun as seen from latitude/longitude at a UTC instant.
SolarPosition solarAt(double latitudeDeg, double longitudeDeg, const QDateTime &utc);

// Elevation at an arbitrary map point, reusing the declination and equation of
// time already computed for this minute.
double solarElevation(double latitudeDeg, double longitudeDeg, const SolarPosition &sun, double utcMinutes);
