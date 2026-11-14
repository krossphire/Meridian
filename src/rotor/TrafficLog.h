/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Raw TX/RX log so a bench session can be compared with the manual.
 * On Windows the files land in %APPDATA%\Meridian\logs.
 */

#pragma once

#include <QByteArray>
#include <QString>

class TrafficLog
{
public:
    static TrafficLog &instance();

    void setEnabled(bool on);
    bool isEnabled() const { return enabled; }
    QString directory() const;
    QString filePath() const { return path; }

    void note(const QString &text);
    void record(const char *direction, const QByteArray &bytes);
    void flush();

private:
    TrafficLog() = default;
    void ensureOpen();
    void writeLine(const QString &line);

    bool enabled = false;
    QString path;
    bool opened = false;
};
