/*
 * Maidenhead locator conversion derived from QLog data/Gridsquare.cpp
 * (GPL-3.0-or-later). Logger-only helpers (locale distances, VUCC pairs)
 * are not included.
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

#include "Gridsquare.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

bool fieldChar(const QString &grid, int index)
{
    const char ch = grid.at(index).toLatin1();
    return ch >= 'A' && ch <= 'R';
}

bool digitChar(const QString &grid, int index)
{
    const char ch = grid.at(index).toLatin1();
    return ch >= '0' && ch <= '9';
}

bool subChar(const QString &grid, int index)
{
    const char ch = grid.at(index).toLatin1();
    return ch >= 'A' && ch <= 'X';
}

} // namespace

bool Gridsquare::isValid(const QString &grid)
{
    const int size = grid.size();
    if (size != 4 && size != 6 && size != 8)
        return false;
    if (!fieldChar(grid, 0) || !fieldChar(grid, 1))
        return false;
    if (!digitChar(grid, 2) || !digitChar(grid, 3))
        return false;
    if (size == 4)
        return true;
    if (!subChar(grid, 4) || !subChar(grid, 5))
        return false;
    if (size == 6)
        return true;
    return digitChar(grid, 6) && digitChar(grid, 7);
}

QString Gridsquare::normalize(const QString &grid)
{
    const QString upper = grid.trimmed().toUpper();
    return isValid(upper) ? upper : QString();
}

bool Gridsquare::toLatLon(const QString &gridIn, double &lat, double &lon)
{
    const QString grid = normalize(gridIn);
    if (grid.isEmpty())
        return false;

    lon = (grid.at(0).toLatin1() - 'A') * 20.0 - 180.0;
    lat = (grid.at(1).toLatin1() - 'A') * 10.0 - 90.0;
    lon += (grid.at(2).toLatin1() - '0') * 2.0;
    lat += (grid.at(3).toLatin1() - '0') * 1.0;

    if (grid.size() >= 6) {
        lon += (grid.at(4).toLatin1() - 'A') * (5.0 / 60.0);
        lat += (grid.at(5).toLatin1() - 'A') * (2.5 / 60.0);
        if (grid.size() >= 8) {
            lon += (grid.at(6).toLatin1() - '0') * (30.0 / 3600.0);
            lat += (grid.at(7).toLatin1() - '0') * (15.0 / 3600.0);
            lon += 15.0 / 3600.0;
            lat += 7.5 / 3600.0;
        } else {
            lon += 2.5 / 60.0;
            lat += 1.25 / 60.0;
        }
    } else {
        lon += 1.0;
        lat += 0.5;
    }
    return true;
}

QString Gridsquare::fromLatLon(double lat, double lon)
{
    if (!qIsFinite(lat) || !qIsFinite(lon) || qAbs(lat) >= 90.0 || qAbs(lon) >= 180.0)
        return {};

    const QString letters = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWX");
    const double modifiedLat = lat + 90.0;
    const double modifiedLon = lon + 180.0;

    const int i1 = std::clamp(static_cast<int>(modifiedLon / 20.0), 0, 17);
    const int i2 = std::clamp(static_cast<int>(modifiedLat / 10.0), 0, 17);
    const int i3 = std::clamp(static_cast<int>(std::fmod(modifiedLon / 2.0, 10.0)), 0, 9);
    const int i4 = std::clamp(static_cast<int>(std::fmod(modifiedLat, 10.0)), 0, 9);
    const double rLat = (modifiedLat - static_cast<int>(modifiedLat)) * 60.0;
    const double rLon = (modifiedLon - 2.0 * static_cast<int>(modifiedLon / 2.0)) * 60.0;
    const int i5 = std::clamp(static_cast<int>(rLon / 5.0), 0, 23);
    const int i6 = std::clamp(static_cast<int>(rLat / 2.5), 0, 23);

    return QString(letters.at(i1)) + letters.at(i2) + QString::number(i3) + QString::number(i4)
           + letters.at(i5) + letters.at(i6);
}

bool Gridsquare::bearingTo(double fromLat, double fromLon, double toLat, double toLon, double &bearing)
{
    if (!qIsFinite(fromLat) || !qIsFinite(fromLon) || !qIsFinite(toLat) || !qIsFinite(toLon)
        || qAbs(fromLat) > 90.0 || qAbs(toLat) > 90.0 || qAbs(fromLon) > 180.0 || qAbs(toLon) > 180.0) {
        bearing = 0.0;
        return false;
    }
    if (fromLat == toLat && fromLon == toLon) {
        bearing = 0.0;
        return true;
    }
    constexpr double kDegToRad = 0.017453292519943295;
    constexpr double kRadToDeg = 57.29577951308232;
    const double dLon = (toLon - fromLon) * kDegToRad;
    const double lat1 = fromLat * kDegToRad;
    const double lat2 = toLat * kDegToRad;
    const double y = std::sin(dLon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    bearing = std::fmod((std::atan2(y, x) * kRadToDeg + 360.0), 360.0);
    return true;
}

bool Gridsquare::bearingTo(const QString &fromGrid, const QString &toGrid, double &bearing)
{
    double fromLat = 0;
    double fromLon = 0;
    double toLat = 0;
    double toLon = 0;
    if (!toLatLon(fromGrid, fromLat, fromLon) || !toLatLon(toGrid, toLat, toLon)) {
        bearing = 0.0;
        return false;
    }
    return bearingTo(fromLat, fromLon, toLat, toLon, bearing);
}
