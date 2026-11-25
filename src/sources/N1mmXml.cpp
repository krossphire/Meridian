/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "N1mmXml.h"

#include <QXmlStreamReader>

N1mmPacket parseN1mmPacket(const QByteArray &xml)
{
    N1mmPacket out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        const QString name = reader.name().toString();
        if (name.compare(QStringLiteral("lookupinfo"), Qt::CaseInsensitive) == 0)
            out.kind = N1mmPacket::Kind::Lookup;
        else if (name.compare(QStringLiteral("contactinfo"), Qt::CaseInsensitive) == 0)
            out.kind = N1mmPacket::Kind::Contact;
        else if (name.compare(QStringLiteral("call"), Qt::CaseInsensitive) == 0)
            out.call = reader.readElementText().trimmed().toUpper();
        else if (out.grid.isEmpty()
                 && (name.compare(QStringLiteral("gridsquare"), Qt::CaseInsensitive) == 0
                     || name.compare(QStringLiteral("grid"), Qt::CaseInsensitive) == 0
                     || name.compare(QStringLiteral("grid4"), Qt::CaseInsensitive) == 0))
            out.grid = reader.readElementText().trimmed().toUpper();
    }
    if (out.kind == N1mmPacket::Kind::None)
        out = {};
    return out;
}
