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
// A grid uses the square's center. Typed coordinates keep the parsed degrees;
// the 6-character grid on the result is a display label only.
std::optional<ResolvedLocation> resolveLocation(const QString &text, const ZipDatabase *zips, QString *error);

// latitude and longitude, when both are non-empty, win over place. They are
// parsed as degrees and used as-is. The derived grid is for display only.
std::optional<ResolvedLocation> resolveHome(const QString &place, const QString &latitude, const QString &longitude,
                                            const ZipDatabase *zips, QString *error);

QString formatLatLon(double lat, double lon);
