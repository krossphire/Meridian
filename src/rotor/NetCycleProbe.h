/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Same-process timestamp of the moment Meridian raises DTR. A pty on this
 * host returns ENOTTY for TIOCM, so the mock cannot see the modem line.
 * The mock reads this to require the echo within 30 ms of that raise.
 * It is not sent to the rotor.
 */

#pragma once

#include <atomic>

inline std::atomic<qint64> &networkDtrRoseProbeMs()
{
    static std::atomic<qint64> ms{0};
    return ms;
}
