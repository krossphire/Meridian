/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QSerialPort>

#if defined(Q_OS_UNIX)
#include <sys/ioctl.h>
#endif

// Setup Utility 4.B12 turns DTR and RTS on after open. Qt's
// setRequestToSend crashes on a pty under Qt 6.4, so Unix uses ioctl,
// which fails quietly when the device has no modem lines.
inline bool raiseDtrAndRts(QSerialPort *port, bool *dtrOn = nullptr, bool *rtsOn = nullptr)
{
    bool dtr = false;
    bool rts = false;
    if (port && port->isOpen()) {
#if defined(Q_OS_UNIX)
        const int fd = static_cast<int>(port->handle());
        int bits = TIOCM_DTR | TIOCM_RTS;
        if (fd >= 0 && ::ioctl(fd, TIOCMBIS, &bits) == 0) {
            dtr = rts = true;
        } else if (fd >= 0 && ::ioctl(fd, TIOCMGET, &bits) == 0) {
            bits |= TIOCM_DTR | TIOCM_RTS;
            if (::ioctl(fd, TIOCMSET, &bits) == 0)
                dtr = rts = true;
        }
#else
        dtr = port->setDataTerminalReady(true);
        rts = port->setRequestToSend(true);
#endif
    }
    if (dtrOn)
        *dtrOn = dtr;
    if (rtsOn)
        *rtsOn = rts;
    return dtr && rts;
}

// The setup utility drops DTR, purges, and closes. The line stays low for
// about 200 ms before the next open raises it again.
inline void dropDtr(QSerialPort *port)
{
    if (!port || !port->isOpen())
        return;
#if defined(Q_OS_UNIX)
    const int fd = static_cast<int>(port->handle());
    int bits = TIOCM_DTR;
    if (fd >= 0 && ::ioctl(fd, TIOCMBIC, &bits) != 0 && ::ioctl(fd, TIOCMGET, &bits) == 0) {
        bits &= ~TIOCM_DTR;
        ::ioctl(fd, TIOCMSET, &bits);
    }
#else
    port->setDataTerminalReady(false);
#endif
}

// A read completion's buffer is often larger than the bytes the driver
// transferred. Only the transferred count is payload. A zero count is an
// empty completion, not a received 0x00.
inline QByteArray serialBytesTaken(const char *data, int capacity, int transferred)
{
    if (!data || transferred <= 0 || capacity <= 0)
        return {};
    const int count = transferred < capacity ? transferred : capacity;
    return QByteArray(data, count);
}
