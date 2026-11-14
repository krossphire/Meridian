/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Offline US ZIP centroids from the Census Bureau 2020 ZCTA gazetteer.
 * ZCTAs approximate ZIP codes; some PO-box-only ZIPs are absent.
 */

#pragma once

#include <QString>
#include <QVector>

class ZipDatabase
{
public:
    bool load(const QByteArray &bytes);
    int size() const { return rows.size(); }
    bool lookup(const QString &zip, double &lat, double &lon) const;

private:
    struct Row {
        quint32 zip = 0;
        qint32 latE6 = 0;
        qint32 lonE6 = 0;
    };
    QVector<Row> rows;
};
