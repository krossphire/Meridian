/*
 * Maidenhead locator conversion derived from QLog data/Gridsquare.cpp
 * (GPL-3.0-or-later).
 *
 * Copyright (C) 2020 Thomas Gatzweiler
 * Copyright (C) 2021-2026 Ladislav Foldyna
 * Copyright (C) 2026 Andrew, K5PO
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <QString>

class Gridsquare
{
public:
    // 4, 6, or 8 character Maidenhead. Returns the center of the square.
    static bool toLatLon(const QString &grid, double &lat, double &lon);
    static bool isValid(const QString &grid);
    static QString normalize(const QString &grid);

    // Six-character square containing the point. Empty if out of range.
    static QString fromLatLon(double lat, double lon);
};
