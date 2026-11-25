/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * QRZ XML Logbook Data, spec 1.34 (https://www.qrz.com/docs/xml/current_spec.html).
 * The callsign field list has lat, lon, and grid. It has no bearing relative
 * to the logged-in user. Meridian computes that from home.
 */

#pragma once

#include <QString>

struct QrzXml {
    QString sessionKey;
    QString error;
    QString call;
    QString grid;
    double latitude = 0;
    double longitude = 0;
    bool haveLatitude = false;
    bool haveLongitude = false;
    // True only if a response contains a <bearing> element. Spec 1.34 does not
    // define one. Callers ignore it and compute the short path themselves.
    bool sawBearing = false;
};

QrzXml parseQrzXml(const QByteArray &xml);
