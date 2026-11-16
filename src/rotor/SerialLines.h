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
        if (fd >= 0 && ::ioctl(fd, TIOCMBIS, &bits) == 0)
            dtr = rts = true;
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
