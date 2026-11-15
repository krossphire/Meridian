/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GeoMap.h"

#include "core/Projection.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

bool readU32(const uchar *&p, const uchar *end, quint32 &out)
{
    if (p + 4 > end)
        return false;
    std::memcpy(&out, p, 4);
    out = qFromLittleEndian(out);
    p += 4;
    return true;
}

bool readF32(const uchar *&p, const uchar *end, float &out)
{
    if (p + 4 > end)
        return false;
    std::memcpy(&out, p, 4);
    p += 4;
    return true;
}

bool readRings(const uchar *&p, const uchar *end, QVector<GeoRing> &rings, float *minLat, float *maxLat,
               float *minLon, float *maxLon)
{
    quint32 ringCount = 0;
    if (!readU32(p, end, ringCount) || ringCount > 100000)
        return false;
    rings.reserve(rings.size() + int(ringCount));
    for (quint32 r = 0; r < ringCount; ++r) {
        quint32 npts = 0;
        if (!readU32(p, end, npts) || npts > 2000000)
            return false;
        GeoRing ring;
        ring.pts.resize(int(npts));
        for (quint32 i = 0; i < npts; ++i) {
            float lat = 0;
            float lon = 0;
            if (!readF32(p, end, lat) || !readF32(p, end, lon))
                return false;
            ring.pts[int(i)] = {lat, lon};
            if (minLat) {
                *minLat = std::min(*minLat, lat);
                *maxLat = std::max(*maxLat, lat);
                *minLon = std::min(*minLon, lon);
                *maxLon = std::max(*maxLon, lon);
            }
        }
        if (ring.pts.size() >= 3)
            rings.append(std::move(ring));
    }
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
        if (std::abs(lonI - lonJ) > 180.0)
            continue;
        const bool cross = (latI > lat) != (latJ > lat);
        if (cross) {
            const double x = (lonJ - lonI) * (lat - latI) / (latJ - latI) + lonI;
            if (lon < x)
                inside = !inside;
        }
    }
    return inside;
}

bool pointInCountry(const GeoCountry &country, double lat, double lon)
{
    bool inside = false;
    for (const GeoRing &ring : country.rings) {
        if (pointInRing(ring, lat, lon))
            inside = !inside;
    }
    return inside;
}

struct P {
    double x = 0;
    double y = 0;
    double rho = 0;
};

P toP(const UnitPoint &u)
{
    return {u.x, u.y, u.rho};
}

bool breakSegment(const P &a, const P &b)
{
    // 179.5° from the station is rho = 179.5/180. Past that, AE piles the
    // antipode onto the rim and a straight join becomes a concentric arc.
    constexpr double kRim = 179.5 / 180.0;
    if (a.rho > kRim || b.rho > kRim)
        return true;
    const double d = std::hypot(a.x - b.x, a.y - b.y);
    if (d > 0.45)
        return true;
    if (a.rho > 0.90 && b.rho > 0.90 && d > 0.08)
        return true;
    return false;
}

void appendRing(QPainterPath &path, const GeoRing &ring, double homeLat, double homeLon)
{
    QVector<P> pts;
    pts.reserve(ring.pts.size() + 8);
    const auto push = [&](double lat, double lon) {
        pts.append(toP(projectAzimuthal(homeLat, homeLon, lat, lon)));
    };
    for (int i = 0; i < ring.pts.size(); ++i) {
        if (i == 0) {
            push(ring.pts.at(0).lat, ring.pts.at(0).lon);
            continue;
        }
        const double lat1 = ring.pts.at(i - 1).lat;
        const double lon1 = ring.pts.at(i - 1).lon;
        const double lat2 = ring.pts.at(i).lat;
        const double lon2 = ring.pts.at(i).lon;
        double dlon = lon2 - lon1;
        if (dlon > 180.0)
            dlon -= 360.0;
        if (dlon < -180.0)
            dlon += 360.0;
        const double dist = std::hypot(lat2 - lat1, dlon);
        int steps = int(std::ceil(dist / 4.0));
        steps = std::clamp(steps, 1, 16);
        for (int s = 1; s <= steps; ++s) {
            const double t = double(s) / double(steps);
            double lon = lon1 + dlon * t;
            if (lon > 180.0)
                lon -= 360.0;
            if (lon < -180.0)
                lon += 360.0;
            push(lat1 + (lat2 - lat1) * t, lon);
        }
    }
    if (pts.isEmpty())
        return;

    bool drawing = false;
    bool broke = false;
    P prev;
    for (const P &pt : pts) {
        if (!drawing) {
            path.moveTo(pt.x, pt.y);
            prev = pt;
            drawing = true;
            continue;
        }
        if (breakSegment(prev, pt)) {
            path.moveTo(pt.x, pt.y);
            broke = true;
        } else {
            path.lineTo(pt.x, pt.y);
        }
        prev = pt;
    }
    if (!broke)
        path.closeSubpath();
}

} // namespace

bool loadGeoLayer(const QByteArray &bytes, GeoLayer &layer)
{
    layer = {};
    if (bytes.size() < 16 || !bytes.startsWith("MRDMP002"))
        return false;
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + 8);
    const auto *end = reinterpret_cast<const uchar *>(bytes.constData() + bytes.size());
    quint32 landCount = 0;
    quint32 countryCount = 0;
    if (!readU32(p, end, landCount) || !readU32(p, end, countryCount))
        return false;
    if (landCount > 100000 || countryCount > 100000)
        return false;
    layer.land.reserve(int(landCount));
    for (quint32 i = 0; i < landCount; ++i) {
        GeoRecord rec;
        if (!readRings(p, end, rec.rings, nullptr, nullptr, nullptr, nullptr))
            return false;
        if (!rec.rings.isEmpty())
            layer.land.append(std::move(rec));
    }
    layer.countries.reserve(int(countryCount));
    for (quint32 i = 0; i < countryCount; ++i) {
        if (p + 2 > end)
            return false;
        quint16 nameLen = 0;
        std::memcpy(&nameLen, p, 2);
        nameLen = qFromLittleEndian(nameLen);
        p += 2;
        if (p + nameLen + 12 > end)
            return false;
        GeoCountry country;
        country.name = QString::fromUtf8(reinterpret_cast<const char *>(p), nameLen);
        p += nameLen;
        float labelLat = 0;
        float labelLon = 0;
        if (!readF32(p, end, labelLat) || !readF32(p, end, labelLon))
            return false;
        quint32 pop = 0;
        if (!readU32(p, end, pop))
            return false;
        country.labelLat = labelLat;
        country.labelLon = labelLon;
        country.population = pop;
        if (!readRings(p, end, country.rings, &country.minLat, &country.maxLat, &country.minLon,
                       &country.maxLon))
            return false;
        if (!country.rings.isEmpty())
            layer.countries.append(std::move(country));
    }
    return p == end;
}

int countryAt(const GeoLayer &layer, double lat, double lon)
{
    for (int i = 0; i < layer.countries.size(); ++i) {
        const GeoCountry &country = layer.countries.at(i);
        if (lat < country.minLat || lat > country.maxLat || lon < country.minLon || lon > country.maxLon)
            continue;
        if (pointInCountry(country, lat, lon))
            return i;
    }
    return -1;
}

QString shortCountryName(const QString &name)
{
    if (name == QLatin1String("United States of America"))
        return QStringLiteral("United States");
    if (name == QLatin1String("Democratic Republic of the Congo"))
        return QStringLiteral("DR Congo");
    if (name == QLatin1String("Central African Republic"))
        return QStringLiteral("Central African Rep.");
    if (name == QLatin1String("United Republic of Tanzania"))
        return QStringLiteral("Tanzania");
    if (name == QLatin1String("Dominican Republic"))
        return QStringLiteral("Dominican Rep.");
    return name;
}

ProjectedLayer projectLayer(const GeoLayer &layer, double homeLat, double homeLon)
{
    ProjectedLayer out;
    out.land.reserve(layer.land.size());
    for (const GeoRecord &rec : layer.land) {
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (const GeoRing &ring : rec.rings)
            appendRing(path, ring, homeLat, homeLon);
        if (!path.isEmpty())
            out.land.append(std::move(path));
    }
    out.countries.reserve(layer.countries.size());
    for (int i = 0; i < layer.countries.size(); ++i) {
        const GeoCountry &country = layer.countries.at(i);
        ProjectedCountry projected;
        projected.name = country.name;
        projected.population = country.population;
        projected.index = i;
        const UnitPoint label = projectAzimuthal(homeLat, homeLon, country.labelLat, country.labelLon);
        projected.label = QPointF(label.x, label.y);
        projected.labelOk = label.rho > 0.12 && label.rho < 0.86;
        projected.path.setFillRule(Qt::OddEvenFill);
        for (const GeoRing &ring : country.rings)
            appendRing(projected.path, ring, homeLat, homeLon);
        out.countries.append(std::move(projected));
    }
    return out;
}
