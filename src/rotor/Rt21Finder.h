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

#include <functional>
#include <optional>

struct FoundRotor {
    RotorEndpoint endpoint;
    double heading = 0;
    QString label;
};

// Chip family from a USB serial adapter's manufacturer and description.
// Empty when it is not a known USB UART.
QString usbChipName(const QString &manufacturer, const QString &description);

// "RT-21 on COM6 (USB, 4800), heading 132°" or "RT-21 at 192.168.1.20:6555 (network), heading 1°".
QString foundRotorLabel(const QString &place, const QString &kind, int baud, double heading);

// True for VPN, virtual, and tunnel adapters that should not be swept.
bool isVirtualAdapterName(const QString &name);

// Ports to try on each host. Factory numeric defaults are not published; see the protocol note.
// `extra` is a saved or custom port. Order is stable and duplicates are dropped.
QList<int> networkCandidatePorts(const QList<int> &extra = {});

// 4800, then 9600, then 19200.
QList<int> serialProbeBauds();

struct SerialPortChoice {
    QString port;
    QString label;
};

// COM1, COM2, … COM10, COM110. A saved port that is not present is appended as "COM6 (missing)".
QList<SerialPortChoice> arrangeSerialPorts(QList<SerialPortChoice> present, const QString &saved);

class Rt21Finder
{
public:
    // One harmless BI1; read. Empty if the port is busy, missing, or not an RT-21.
    static std::optional<FoundRotor> probeSerial(const QString &portName, const QString &kind, int baud, int timeoutMs);

    static std::optional<FoundRotor> probeTcp(const QString &host, int port, int timeoutMs);

    // Every present serial port, including Edgeport COM1–COM8. Ports that will not open are skipped.
    // `onFound` is called as each rotor answers, including from a worker thread.
    static QList<FoundRotor> scanSerialPorts(int timeoutMs = 650,
                                             const std::function<void(const FoundRotor &)> &onFound = {});

    // IPv4 hosts on local /24s of physical adapters, excluding this machine.
    static QStringList localSubnetHosts();

    // Probes host × port with `connectMs` handshakes, 32 at a time, and stops
    // launching new probes after about 14 s. A hit is kept only when BI1; parses.
    static QList<FoundRotor> scanHosts(const QStringList &hosts, const QList<int> &ports, int connectMs = 300,
                                       const std::function<void(const FoundRotor &)> &onFound = {});
};
