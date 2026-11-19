/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "TrafficLog.h"

#include "Rt21Setup.h"
#include "UserFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

namespace {

QString stamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString hexBytes(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size() * 3);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i)
            out += QLatin1Char(' ');
        out += QString::asprintf("%02X", static_cast<unsigned char>(bytes.at(i)));
    }
    return out;
}

QString printable(const QByteArray &bytes)
{
    QString out;
    for (unsigned char c : bytes) {
        if (c == '\\')
            out += QStringLiteral("\\\\");
        else if (c == '\r')
            out += QStringLiteral("\\r");
        else if (c == '\n')
            out += QStringLiteral("\\n");
        else if (c >= 32 && c < 127)
            out += QChar(c);
        else
            out += QString::asprintf("\\x%02X", c);
    }
    return out;
}

QFile &logFile()
{
    static QFile file;
    return file;
}

} // namespace

TrafficLog &TrafficLog::instance()
{
    static TrafficLog log;
    return log;
}

QString TrafficLog::directory() const
{
    return UserFiles::logDirectory();
}

void TrafficLog::setEnabled(bool on)
{
    enabled = on;
    if (enabled)
        ensureOpen();
}

void TrafficLog::ensureOpen()
{
    if (opened)
        return;
    const QString dir = UserFiles::logDirectory();
    QDir().mkpath(dir);
    path = QDir(dir).filePath(QStringLiteral("meridian-%1.log")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    logFile().setFileName(path);
    opened = logFile().open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered);
    if (opened)
        writeLine(QStringLiteral("NOTE Meridian 0.1.0-alpha25e traffic log ") + path);
}

void TrafficLog::flush()
{
    if (opened)
        logFile().flush();
}

void TrafficLog::writeLine(const QString &line)
{
    if (!opened)
        return;
    const QByteArray bytes = (line + QLatin1Char('\n')).toUtf8();
    logFile().write(bytes);
    logFile().flush();
}

void TrafficLog::note(const QString &text)
{
    if (!enabled)
        return;
    ensureOpen();
    writeLine(stamp() + QStringLiteral(" NOTE ") + text);
}

void TrafficLog::recordSession(const char *direction, const QByteArray &bytes)
{
    const bool was = enabled;
    enabled = true;
    record(direction, bytes);
    enabled = was;
}

void TrafficLog::record(const char *direction, const QByteArray &bytes)
{
    if (!enabled || bytes.isEmpty())
        return;
    ensureOpen();
    const QByteArray safe = Rt21::redactForLog(bytes);
    writeLine(stamp() + QLatin1Char(' ') + QLatin1String(direction) + QLatin1Char(' ') + QString::number(safe.size())
              + QStringLiteral(" |") + printable(safe) + QStringLiteral("| ") + hexBytes(safe));
}
