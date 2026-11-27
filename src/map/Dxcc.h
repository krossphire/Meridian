/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DXCC hover. Country names use data/dxcc-names.tsv. Split entities
 * (Alaska, Hawaii, Puerto Rico, Azores, Sardinia, French overseas
 * territories, and the rest packed in data/dxcc.bin) have their own
 * outlines, from Natural Earth map subunits, public domain.
 * Prefixes follow Big CTY. See third_party/cty/LICENSE.CTY.
 */

#pragma once

#include "map/GeoMap.h"

#include <QString>

#include <optional>

struct DxccLabel {
    QString name;
    QString prefix;
};

struct DxccEntity {
    QString name;
    QString prefix;
    QVector<GeoRing> rings;
    float minLat = 90;
    float maxLat = -90;
    float minLon = 180;
    float maxLon = -180;
};

// prefixes is optional Big CTY output: prefix, natural name, P or E.
bool loadDxcc(const QByteArray &outlines, const QByteArray &names, const QByteArray &prefixes = {});
const QVector<DxccEntity> &dxccEntities();

// Split-entity polygon, or an empty label when the point is in the parent only.
DxccLabel dxccAt(double lat, double lon);

// Natural Earth country name to the DXCC label. Prefix may be empty.
DxccLabel dxccForCountry(const QString &naturalEarthName);

// Longest prefix of a callsign in the local DXCC table.
struct DxccCall {
    QString prefix;
    QString country;
    bool havePoint = false;
    double latitude = 0;
    double longitude = 0;
};
std::optional<DxccCall> dxccForCallsign(const QString &call);
