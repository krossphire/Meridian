/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * USB chips that can be an RT-21, and plugs that never enumerated.
 * A missing CP210x or FTDI driver is only a possible RT-21.
 * A descriptor failure is a cable or port problem, not an identification.
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>

struct UsbDeviceFact {
    quint16 vid = 0;
    quint16 pid = 0;
    QString name;
    QString instanceId;
    int problemCode = -1;
    bool hasSerialPort = false;
    QDateTime lastArrival;
};

enum class UartFamily { None, Cp210x, Ftdi };

struct UsbHint {
    enum class Kind { MissingDriver, FailedStart } kind = Kind::FailedStart;
    UartFamily family = UartFamily::None;
    QString text;
    QString html;
};

UartFamily uartFamily(quint16 vid, quint16 pid);

// True for Windows "Unknown USB Device (Device Descriptor Request Failed)",
// problem code 43, and the placeholder id VID_0000&PID_0002.
bool isDescriptorFailure(const UsbDeviceFact &device);

// Recent means it arrived within the last 15 minutes, or it showed up
// after Meridian's first look. An old failure with no clock is ignored.
bool failedStartIsRecent(const UsbDeviceFact &device, const QDateTime &now, bool baselineReady,
                         const QSet<QString> &seenAtStart);

QList<UsbHint> classifyUsbDevices(const QList<UsbDeviceFact> &devices, const QDateTime &now, bool baselineReady,
                                  const QSet<QString> &seenAtStart);

// Present USB devices. Linux reads sysfs. Windows uses SetupAPI. Mac uses IOKit.
QList<UsbDeviceFact> enumeratePresentUsbDevices();

// Test seam for the Linux layout.
QList<UsbDeviceFact> usbFactsFromSysfs(const QString &root);
