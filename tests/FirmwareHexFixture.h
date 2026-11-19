/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Synthetic PIC18F26K22 image for the hex-loader tests. Not a factory file.
 */

#pragma once

#include <QByteArray>
#include <QString>

inline QString firmwareHexRecord(int address, int type, const QByteArray &data)
{
    QByteArray body;
    body.append(char(data.size()));
    body.append(char((address >> 8) & 0xFF));
    body.append(char(address & 0xFF));
    body.append(char(type));
    body.append(data);
    int sum = 0;
    for (unsigned char byte : body)
        sum = (sum + byte) & 0xFF;
    body.append(char((0x100 - sum) & 0xFF));
    return QLatin1Char(':') + QString::fromLatin1(body.toHex()).toUpper();
}

inline QString firmwareHexTrailer()
{
    return QStringLiteral(";PIC18F26K22\n;CRC=9241 CREATED=2026-11-19\n");
}

inline QString firmwareHexEof()
{
    return firmwareHexRecord(0, 1, QByteArray()) + QLatin1Char('\n');
}

inline QString validFirmwareHex()
{
    const QByteArray version = QByteArray("RT-21 4.13.2");
    return firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
           + firmwareHexRecord(0x0400, 0, QByteArray(1, char(0xA5))) + QLatin1Char('\n')
           + firmwareHexRecord(0x0676, 0, version) + QLatin1Char('\n')
           + firmwareHexRecord(0, 4, QByteArray::fromHex("0030")) + QLatin1Char('\n')
           + firmwareHexRecord(0x0000, 0, QByteArray(1, char(0x00))) + QLatin1Char('\n')
           + firmwareHexEof()
           + firmwareHexTrailer();
}
