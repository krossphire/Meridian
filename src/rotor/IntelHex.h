/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Intel HEX loader for an RT-21 v4 image. The part is a PIC18F26K22.
 * Application bytes are 0x0400-0xFFFF. 0x0000-0x03FF is the bootloader and
 * is not in a Green Heron hex. Config words sit at 0x300000-0x30000D.
 * The bootloader wire protocol is not implemented here.
 */

#pragma once

#include <QMap>
#include <QString>

namespace Rt21 {

inline constexpr quint32 kFirmwareBootloaderEnd = 0x03FF;
inline constexpr quint32 kFirmwareAppStart = 0x0400;
inline constexpr quint32 kFirmwareAppEnd = 0xFFFF;
inline constexpr quint32 kFirmwareConfigStart = 0x300000;
inline constexpr quint32 kFirmwareConfigEnd = 0x30000D;
inline constexpr quint32 kFirmwareVersionAddress = 0x0676;

struct FirmwareImage {
    bool ok = false;
    QString error;
    QMap<quint32, quint8> bytes;
    QString device;
    QString crc;
    QString created;
    QString version;
    int applicationBytes = 0;
    int configBytes = 0;
};

FirmwareImage loadFirmwareHex(const QString &text);
FirmwareImage loadFirmwareHexFile(const QString &path);

} // namespace Rt21
