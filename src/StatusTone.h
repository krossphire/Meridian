/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QColor>
#include <QString>

// Status text is never the primary ink. Neutral is the muted secondary grey.
enum class StatusTone { Neutral, Busy, Success, Failure };

inline StatusTone statusTone(const QString &text)
{
    const QString t = text.toLower();
    if (t.contains(QStringLiteral("write not confirmed")) || t.contains(QStringLiteral("timeout"))
        || t.contains(QStringLiteral("not connected")) || t.contains(QStringLiteral("didn't answer"))
        || t.contains(QStringLiteral("did not answer")) || t.contains(QStringLiteral("couldn't"))
        || t.contains(QStringLiteral("could not")) || t.contains(QStringLiteral("no reply"))
        || t.contains(QStringLiteral("stopped answering")) || t.contains(QStringLiteral("disconnected"))
        || t.contains(QStringLiteral("link dropped")) || t.contains(QStringLiteral("link closed"))
        || t.contains(QStringLiteral("didn't reopen"))
        || t.contains(QStringLiteral("read the network module"))
        || t.contains(QStringLiteral("only the gateway"))
        || t.contains(QStringLiteral("ipv4"))
        || t.contains(QStringLiteral("connect to a rotor")) || t.contains(QStringLiteral("drops the link"))
        || t.contains(QStringLiteral("pre-release")) || t.contains(QStringLiteral("hang serial")))
        return StatusTone::Failure;
    if (t.startsWith(QStringLiteral("saved ")) || t.contains(QStringLiteral("settings read"))
        || t.contains(QStringLiteral("module answered")) || t.contains(QStringLiteral("saved a copy"))
        || t.contains(QStringLiteral("read back the same")))
        return StatusTone::Success;
    if (t.contains(QStringLiteral("writing")) || t.contains(QStringLiteral("reading"))
        || t.contains(QStringLiteral("waiting")) || t.contains(QStringLiteral("checking"))
        || t.contains(QStringLiteral("scanning")) || t.contains(QStringLiteral("cancelling"))
        || text.endsWith(QStringLiteral("…")) || text.endsWith(QStringLiteral("...")))
        return StatusTone::Busy;
    return StatusTone::Neutral;
}

inline QColor statusToneColor(StatusTone tone)
{
    switch (tone) {
    case StatusTone::Busy:
        return QColor(224, 177, 92);
    case StatusTone::Success:
        return QColor(125, 206, 160);
    case StatusTone::Failure:
        return QColor(224, 108, 92);
    case StatusTone::Neutral:
        break;
    }
    return QColor(142, 160, 153);
}
