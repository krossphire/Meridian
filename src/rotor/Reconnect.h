/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reconnect timing is pure so it can be tested without a socket.
 * The RT-21 network module wedges if we redial immediately, so the first
 * retries are 20 s apart. After five minutes down the gap grows and stops at 5 min.
 */

#pragma once

#include <QString>

#include <algorithm>

inline int reconnectDelayMs(qint64 downForMs, int postGraceAttempt)
{
    constexpr qint64 kGraceMs = 5LL * 60 * 1000;
    constexpr int kInitialMs = 20000;
    constexpr int kCapMs = 5 * 60 * 1000;
    if (downForMs < kGraceMs)
        return kInitialMs;
    const int shift = std::clamp(postGraceAttempt, 0, 4);
    const int raw = 30000 * (1 << shift);
    return std::min(raw, kCapMs);
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
