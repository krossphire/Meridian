/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ZipCodes.h"

#include <QtEndian>

#include <cstring>

bool ZipDatabase::load(const QByteArray &bytes)
{
    rows.clear();
    if (bytes.size() < 12 || !bytes.startsWith("MZCTA001"))
        return false;
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + 8);
    const auto *end = reinterpret_cast<const uchar *>(bytes.constData() + bytes.size());
    if (p + 4 > end)
        return false;
    quint32 count = 0;
    std::memcpy(&count, p, 4);
    count = qFromLittleEndian(count);
    p += 4;
    if (count > 200000)
        return false;
    const qsizetype need = qsizetype(count) * 12;
    if (end - p < need)
        return false;
    rows.resize(int(count));
    for (quint32 i = 0; i < count; ++i) {
        quint32 zip = 0;
        qint32 lat = 0;
        qint32 lon = 0;
        std::memcpy(&zip, p, 4);
        std::memcpy(&lat, p + 4, 4);
        std::memcpy(&lon, p + 8, 4);
        rows[int(i)] = {qFromLittleEndian(zip), qFromLittleEndian(lat), qFromLittleEndian(lon)};
        p += 12;
    }
    return true;
}

bool ZipDatabase::lookup(const QString &zipText, double &lat, double &lon) const
{
    QString digits;
    for (const QChar c : zipText) {
        if (c.isDigit())
            digits.append(c);
        else if (c == '-' && digits.size() == 5)
            break;
        else if (!c.isSpace())
            return false;
    }
    if (digits.size() < 5)
        return false;
    digits = digits.left(5);
    bool ok = false;
    const uint zip = digits.toUInt(&ok);
    if (!ok)
        return false;

    int lo = 0;
    int hi = rows.size() - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const quint32 z = rows.at(mid).zip;
        if (z == zip) {
            lat = rows.at(mid).latE6 / 1e6;
            lon = rows.at(mid).lonE6 / 1e6;
            return true;
        }
        if (z < zip)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return false;
}
