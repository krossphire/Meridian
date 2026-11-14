/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Find an RT-21. USB and COM ports are checked first. A network scan is
 * separate and only runs when asked. Nothing is selected on its own.
 */

#pragma once

#include "AppConfig.h"

#include <QString>
#include <QStringList>

#include <optional>

struct FoundRotor {
    RotorEndpoint endpoint;
    double heading = 0;
    QString label;
};

// Chip family from a USB serial adapter's manufacturer and description.
// Empty when it is not a known USB UART.
QString usbChipName(const QString &manufacturer, const QString &description);

// "RT-21 on COM6 (USB, 4800), heading 132°" or "RT-21 at 192.168.1.20, heading 132°".
QString foundRotorLabel(const QString &place, const QString &kind, int baud, double heading);

class Rt21Finder
{
public:
    // One harmless BI1; read. Empty if the port is busy, missing, or not an RT-21.
    static std::optional<FoundRotor> probeSerial(const QString &portName, const QString &kind, int baud, int timeoutMs);

    static std::optional<FoundRotor> probeTcp(const QString &host, int port, int timeoutMs);

    // Every present serial port, 4800 then 9600. Ports that will not open are skipped.
    static QList<FoundRotor> scanSerialPorts(int timeoutMs = 650);

    // IPv4 hosts on local /24s, excluding this machine. Capped so a scan stays short.
    static QStringList localSubnetHosts();

    // Rate-limited. `connectMs` is the handshake budget per batch, not per host.
    static QList<FoundRotor> scanHosts(const QStringList &hosts, int port, int connectMs = 400);
};
