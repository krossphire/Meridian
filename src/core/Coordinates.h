/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "ZipCodes.h"

#include <QString>

#include <optional>

struct ResolvedLocation {
    enum class Source { Grid, Zip, Coordinates };
    double latitude = 0;
    double longitude = 0;
    QString grid;     // 6-character square containing the point
    QString entered;  // normalized grid when the user typed one (may be 4/6/8)
    Source source = Source::Coordinates;

    QString summary() const;
    QString detail() const;
};

// Accepts a Maidenhead grid (4/6/8), a US ZIP / ZIP+4, or coordinates:
// decimal degrees, N/S/E/W prefix or suffix, DMS, and degrees + decimal minutes.
std::optional<ResolvedLocation> resolveLocation(const QString &text, const ZipDatabase *zips, QString *error);

QString formatLatLon(double lat, double lon);
