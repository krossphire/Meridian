/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QrzXml.h"

#include <QXmlStreamReader>

QrzXml parseQrzXml(const QByteArray &xml)
{
    QrzXml out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        const QString name = reader.name().toString();
        if (name.compare(QStringLiteral("bearing"), Qt::CaseInsensitive) == 0) {
            out.sawBearing = true;
            continue;
        }
        if (name.compare(QStringLiteral("Key"), Qt::CaseInsensitive) == 0)
            out.sessionKey = reader.readElementText().trimmed();
        else if (name.compare(QStringLiteral("Error"), Qt::CaseInsensitive) == 0)
            out.error = reader.readElementText().trimmed();
        else if (name.compare(QStringLiteral("call"), Qt::CaseInsensitive) == 0)
            out.call = reader.readElementText().trimmed().toUpper();
        else if (name.compare(QStringLiteral("grid"), Qt::CaseInsensitive) == 0)
            out.grid = reader.readElementText().trimmed().toUpper();
        else if (name.compare(QStringLiteral("lat"), Qt::CaseInsensitive) == 0) {
            bool ok = false;
            const double value = reader.readElementText().trimmed().toDouble(&ok);
            if (ok) {
                out.latitude = value;
                out.haveLatitude = true;
            }
        } else if (name.compare(QStringLiteral("lon"), Qt::CaseInsensitive) == 0) {
            bool ok = false;
            const double value = reader.readElementText().trimmed().toDouble(&ok);
            if (ok) {
                out.longitude = value;
                out.haveLongitude = true;
            }
        }
    }
    return out;
}
