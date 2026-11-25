/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * WSJT-X / WSJT-Z UDP Status (message type 1). Same framing QLog reads:
 * magic 0xadbccbda, then schema, type, and UTF-8 QByteArray strings.
 */

#pragma once

#include <QString>

struct WsjtStatus {
    bool isStatus = false;
    QString dxCall;
    QString dxGrid;
};

WsjtStatus parseWsjtStatus(const QByteArray &datagram);
