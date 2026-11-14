/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Natural Earth land and admin-0 polygons, packed by tools/prepare_data.py.
 */

#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector>

struct GeoPt {
    float lat = 0;
    float lon = 0;
};

struct GeoRing {
    QVector<GeoPt> pts;
};

struct GeoCountry {
    QString name;
    float labelLat = 0;
    float labelLon = 0;
    quint32 population = 0;
    float minLat = 90;
    float maxLat = -90;
    float minLon = 180;
    float maxLon = -180;
    QVector<GeoRing> rings;
};

struct GeoRecord {
    QVector<GeoRing> rings;
};

struct GeoLayer {
    QVector<GeoRecord> land;
    QVector<GeoCountry> countries;
};

bool loadGeoLayer(const QByteArray &bytes, GeoLayer &layer);

// Even-odd over the country's rings. -1 if the point is in the ocean (or unknown).
int countryAt(const GeoLayer &layer, double lat, double lon);

QString shortCountryName(const QString &name);

struct ProjectedCountry {
    QString name;
    quint32 population = 0;
    QPointF label;
    bool labelOk = false;
    QPainterPath path;
    int index = -1;
};

struct ProjectedLayer {
    QVector<QPainterPath> land;
    QVector<ProjectedCountry> countries;
};

// Unit disk, +y north. Built off the UI thread.
ProjectedLayer projectLayer(const GeoLayer &layer, double homeLat, double homeLon);
