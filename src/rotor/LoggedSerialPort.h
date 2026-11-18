/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Logs the bytes QSerialPort actually accepts and returns. This is below
 * the driver's own SER-TX / SER-RX notes, so a capture can be compared
 * with the port buffer rather than with a QString.
 */

#pragma once

#include "TrafficLog.h"

#include <QSerialPort>

class LoggedSerialPort : public QSerialPort
{
public:
    explicit LoggedSerialPort(QObject *parent = nullptr)
        : QSerialPort(parent)
    {
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 n = QSerialPort::readData(data, maxSize);
        if (n > 0 && data)
            TrafficLog::instance().recordPort("PORT-RX", QByteArray(data, int(n)));
        return n;
    }

    qint64 writeData(const char *data, qint64 maxSize) override
    {
        if (data && maxSize > 0)
            TrafficLog::instance().recordPort("PORT-TX", QByteArray(data, int(maxSize)));
        return QSerialPort::writeData(data, maxSize);
    }
};
