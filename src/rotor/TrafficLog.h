/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Raw TX/RX log so a bench session can be compared with the manual.
 * Logs sit in logs/ next to meridian.exe, or under the per-user Meridian
 * folder when the program folder cannot be written.
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
    void setByteLog(bool on);
    bool byteLogOn() const { return byteLog; }
    // Exact bytes that crossed QSerialPort or the raw overlapped handle.
    void recordPort(const char *direction, const QByteArray &bytes);
    QString directory() const;
    QString filePath() const { return path; }

    void note(const QString &text);
    void record(const char *direction, const QByteArray &bytes);
    // Network reads are logged even when the traffic checkbox is off.
    void recordSession(const char *direction, const QByteArray &bytes);
    void flush();

private:
    TrafficLog() = default;
    void ensureOpen();
    void writeLine(const QString &line);

    bool enabled = false;
    bool byteLog = false;
    QString path;
    bool opened = false;
};
