/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QlogNotice.h"

#include <QJsonDocument>
#include <QJsonObject>

QlogContact parseQlogNotice(const QByteArray &json)
{
    QlogContact out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return out;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("msgtype")).toString() != QLatin1String("currentcontact"))
        return out;
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    out.isCurrentContact = true;
    out.call = data.value(QStringLiteral("callsign")).toString().trimmed().toUpper();
    out.grid = data.value(QStringLiteral("grid")).toString().trimmed().toUpper();
    const QJsonValue azimuth = data.value(QStringLiteral("azimuth"));
    if (azimuth.isDouble()) {
        out.azimuth = azimuth.toDouble();
        out.haveAzimuth = true;
    }
    return out;
}
