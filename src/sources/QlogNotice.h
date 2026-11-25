/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * QLog UDP JSON, msgtype currentcontact. The message is not in upstream
 * QLog yet. contrib/qlog-currentcontact/ is the draft patch.
 */

#pragma once

#include <QString>

struct QlogContact {
    bool isCurrentContact = false;
    QString call;
    QString grid;
    double azimuth = 0;
    bool haveAzimuth = false;
};

QlogContact parseQlogNotice(const QByteArray &json);
