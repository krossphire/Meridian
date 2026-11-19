/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Framing for the RT-21 command set (DCU-1 superset). See docs/RT21_PROTOCOL.md.
 * The rotor address digit is always 1.
 */

#pragma once

#include <QByteArray>
#include <QList>

#include <optional>

namespace Rt21 {

QByteArray stopCommand();
QByteArray queryTenths();
QByteArray queryInteger();
QByteArray queryStatus();
QByteArray queryVersion();
QByteArray queryPot();
QByteArray queryReset();
QByteArray gotoImmediate(double azimuth);
QByteArray preset(double azimuth);
QByteArray executeMove();
QByteArray readEe(QChar location);
QByteArray writeEe(QChar location, int value);
// 4.13.2 disassembly: W<letter>0<value>; digit 0 stores and reloads. Brightness 10 is WO010;
QByteArray registerWriteCommand(QChar letter, int value);
QByteArray registerReadCommand(QChar letter);
QByteArray brightnessWriteCommand(int value);
QByteArray brightnessReadCommand();
QByteArray calibrateEndpoint(bool clockwise);
QByteArray setPulseHeading(double azimuth);

struct HeadingReading {
    double azimuth = 0;
    bool haveStatus = false;
    int status = -1; // v4 R2n: 0 idle, 1 moving, 2 error, 4 pot, 5 counter
};

// Strict numeric token: "123", "123.4", " 45.2".
std::optional<double> parseHeading(const QByteArray &frame);

// A full reply frame. Accepts DCU-1 ";123", "123;", " 100.0", "BI1100.0",
// and ignores pure command echoes (BI1, AI1, AP1..., AM1).
std::optional<double> parseHeadingFrame(const QByteArray &frame);
bool isCommandEcho(const QByteArray &frame);

std::optional<HeadingReading> parseStatusFrame(const QByteArray &frame);
QString payloadAfterSoh(const QByteArray &frame);
std::optional<int> parseIntegerPayload(const QByteArray &frame);

class Framer
{
public:
    void append(const QByteArray &bytes);
    QList<QByteArray> take();
    void clear();
    void setLineMode(bool on) { lineMode = on; }

private:
    void commit(const QByteArray &body);

    QByteArray buffer;
    QList<QByteArray> frames;
    bool lineMode = false;
    bool dropping = false;
};

} // namespace Rt21
