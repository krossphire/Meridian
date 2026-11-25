/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Dxcc.h"

#include <QHash>

#include <cstring>
#include <algorithm>

namespace {

QVector<DxccEntity> entities;
QHash<QString, DxccLabel> byName;

bool readU32(const uchar *&p, const uchar *end, quint32 &value)
{
    if (p + 4 > end)
        return false;
    value = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    p += 4;
    return true;
}

bool readU16(const uchar *&p, const uchar *end, quint16 &value)
{
    if (p + 2 > end)
        return false;
    value = quint16(p[0]) | (quint16(p[1]) << 8);
    p += 2;
    return true;
}

bool pointInRing(const GeoRing &ring, double lat, double lon)
{
    bool inside = false;
    const int n = ring.pts.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const double latI = ring.pts.at(i).lat;
        const double lonI = ring.pts.at(i).lon;
        const double latJ = ring.pts.at(j).lat;
        const double lonJ = ring.pts.at(j).lon;
        const bool cross = (latI > lat) != (latJ > lat);
        if (!cross)
            continue;
        const double x = (lonJ - lonI) * (lat - latI) / (latJ - latI) + lonI;
        if (lon < x)
            inside = !inside;
    }
    return inside;
}

} // namespace

bool loadDxcc(const QByteArray &outlines, const QByteArray &names)
{
    entities.clear();
    byName.clear();
    const QString text = QString::fromUtf8(names);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 3)
            continue;
        byName.insert(parts.at(0), DxccLabel{parts.at(1), parts.at(2)});
    }
    if (!outlines.startsWith("MRDX001"))
        return !byName.isEmpty();
    const uchar *p = reinterpret_cast<const uchar *>(outlines.constData()) + 8;
    const uchar *end = p + (outlines.size() - 8);
    quint32 count = 0;
    if (!readU32(p, end, count) || count > 10000)
        return false;
    entities.reserve(int(count));
    for (quint32 i = 0; i < count; ++i) {
        quint16 nameLen = 0;
        quint16 prefixLen = 0;
        if (!readU16(p, end, nameLen) || p + nameLen > end)
            return false;
        DxccEntity entity;
        entity.name = QString::fromUtf8(reinterpret_cast<const char *>(p), nameLen);
        p += nameLen;
        if (!readU16(p, end, prefixLen) || p + prefixLen > end)
            return false;
        entity.prefix = QString::fromUtf8(reinterpret_cast<const char *>(p), prefixLen);
        p += prefixLen;
        quint32 rings = 0;
        if (!readU32(p, end, rings))
            return false;
        for (quint32 r = 0; r < rings; ++r) {
            quint32 npts = 0;
            if (!readU32(p, end, npts) || p + npts * 8 > end)
                return false;
            GeoRing ring;
            ring.pts.reserve(int(npts));
            for (quint32 n = 0; n < npts; ++n) {
                float lat = 0;
                float lon = 0;
                memcpy(&lat, p, 4);
                memcpy(&lon, p + 4, 4);
                p += 8;
                ring.pts.append(GeoPt{lat, lon});
                entity.minLat = std::min(entity.minLat, lat);
                entity.maxLat = std::max(entity.maxLat, lat);
                entity.minLon = std::min(entity.minLon, lon);
                entity.maxLon = std::max(entity.maxLon, lon);
            }
            if (ring.pts.size() >= 3)
                entity.rings.append(std::move(ring));
        }
        if (!entity.rings.isEmpty())
            entities.append(std::move(entity));
    }
    return true;
}

const QVector<DxccEntity> &dxccEntities()
{
    return entities;
}

DxccLabel dxccAt(double lat, double lon)
{
    for (const DxccEntity &entity : entities) {
        if (lat < entity.minLat || lat > entity.maxLat || lon < entity.minLon || lon > entity.maxLon)
            continue;
        bool inside = false;
        for (const GeoRing &ring : entity.rings) {
            if (pointInRing(ring, lat, lon))
                inside = !inside;
        }
        if (inside)
            return DxccLabel{entity.name, entity.prefix};
    }
    return {};
}

DxccLabel dxccForCountry(const QString &naturalEarthName)
{
    const auto found = byName.constFind(naturalEarthName);
    if (found == byName.constEnd())
        return {};
    return found.value();
}
