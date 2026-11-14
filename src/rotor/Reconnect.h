/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reconnect timing is pure so it can be tested without a socket.
 * Link down (nothing answered): every 15 s for the first two minutes, then
 * a backoff that stops at 5 min. A session the rotor closed, or one that
 * stopped answering, waits 20 s the first time and then uses that backoff.
 */

#pragma once

#include <QString>

#include <algorithm>

// linkDown: the host never accepted the TCP handshake.
// backoffStep: how many backoff delays have already been used. The first
// wedged wait is 20 s and does not count as a backoff step.
inline int reconnectDelayMs(qint64 downForMs, int backoffStep, bool linkDown)
{
    constexpr qint64 kFastWindowMs = 2LL * 60 * 1000;
    constexpr int kCapMs = 5 * 60 * 1000;
    if (linkDown && downForMs < kFastWindowMs)
        return 15000;
    if (!linkDown && backoffStep <= 0)
        return 20000;
    const int step = std::clamp(linkDown ? backoffStep : backoffStep - 1, 0, 4);
    return std::min(30000 * (1 << step), kCapMs);
}

inline QString lastSeenPhrase(qint64 elapsedSecs)
{
    if (elapsedSecs < 5)
        return QStringLiteral("Last seen just now");
    if (elapsedSecs < 60)
        return QStringLiteral("Last seen %1s ago").arg(elapsedSecs);
    if (elapsedSecs < 3600)
        return QStringLiteral("Last seen %1 min ago").arg(std::max<qint64>(1, elapsedSecs / 60));
    return QStringLiteral("Last seen %1 h ago").arg(std::max<qint64>(1, elapsedSecs / 3600));
}
