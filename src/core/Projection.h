/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Azimuthal equidistant projection centered on the station. The whole Earth
 * fits in the unit disk: the antipode lies on the rim. +x is east, +y is north.
 * The forward formulas are the standard spherical ones (Snyder). QLog's
 * RotatorWidget::redrawMap uses the matching inverse to sample a raster;
 * Meridian projects vector geometry instead.
 */

#pragma once

struct UnitPoint {
    double x = 0;
    double y = 0;
    double rho = 0; // angular distance / pi, 0..1
};

UnitPoint projectAzimuthal(double homeLat, double homeLon, double lat, double lon);

// Inverse of projectAzimuthal. x/y are unit-disk coordinates. False if outside the disk.
bool unprojectAzimuthal(double homeLat, double homeLon, double x, double y, double &lat, double &lon);
