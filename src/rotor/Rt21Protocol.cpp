/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Protocol.h"

#include "core/Bearing.h"

#include <QString>

#include <cctype>
#include <cmath>

namespace Rt21 {
namespace {

QByteArray azField(double azimuth)
{
    return QString::asprintf("%05.1f", clampRt21Azimuth(azimuth)).toLatin1();
}

QByteArray stripNoise(QByteArray frame)
{
    // NUL between payload bytes (and any other embedded NUL) is wiring noise.
    // SOH stays so the echo delimiter and the R21 status flag still parse.
    if (frame.contains('\0')) {
        QByteArray cleaned;
        cleaned.reserve(frame.size());
        for (char ch : frame) {
            if (ch != '\0')
                cleaned.append(ch);
        }
        frame = cleaned;
    }
    if (!frame.isEmpty() && frame.at(0) == char(0x01))
        frame.remove(0, 1);
    frame = frame.trimmed();
    // Firmware 4.14.g1, read on 15 Nov 2026: the reply is
    // <echo><SOH><value>;  — 'A' SOH '5', '1' SOH 'RT-21 4.14.g1', '0' SOH '0334'.
    // The manual's form is a leading SOH and no echo. Both are accepted.
    if (frame.size() >= 2 && frame.at(1) == char(0x01)) {
        const unsigned char echo = static_cast<unsigned char>(frame.at(0));
        if (std::isalnum(echo) || echo == '[')
            frame.remove(0, 2);
    }
    return frame.trimmed();
}

} // namespace

QByteArray stopCommand()
{
    return QByteArray(";");
}

QByteArray queryTenths()
{
    return QByteArray("BI1;");
}

QByteArray queryInteger()
{
    return QByteArray("AI1;");
}

QByteArray queryStatus()
{
    return QByteArray("R21;");
}

QByteArray queryVersion()
{
    return QByteArray("R11;");
}

QByteArray queryPot()
{
    return QByteArray("R01;");
}

QByteArray queryReset()
{
    return QByteArray("R41;");
}

QByteArray gotoImmediate(double azimuth)
{
    // Hamlib RT-21: "AP1%05.1f\r;". The CR is the execute byte. Without it
    // the controller only stores a target and waits for AM1;. This is the
    // one command that carries a CR. Every other command ends in ';' only.
    // The same bytes go out on serial and on TCP.
    return QByteArray("AP1") + azField(azimuth) + QByteArray("\r;");
}

QByteArray preset(double azimuth)
{
    return QByteArray("AP1") + azField(azimuth) + QByteArray(";");
}

QByteArray executeMove()
{
    return QByteArray("AM1;");
}

QByteArray readEe(QChar location)
{
    const QChar letter = location.toUpper();
    return QByteArray("R") + QByteArray(1, letter.toLatin1()) + QByteArray("1;");
}

QByteArray writeEe(QChar location, int value)
{
    // 4.13.2 disassembly: W<letter><digit><value>;
    // Digit 0 stores and reloads the whole setup. Digits 1–9 store only,
    // applied at the next reload. Neither reboots. This helper is the
    // store-only form and is not what the brightness button sends.
    const QChar letter = location.toUpper();
    return QByteArray("W") + QByteArray(1, letter.toLatin1()) + QByteArray("1")
           + QByteArray::number(value) + QByteArray(";");
}

QByteArray registerWriteCommand(QChar letter, int value)
{
    const QChar upper = letter.toUpper();
    if (upper < QLatin1Char('A') || upper > QLatin1Char('Z'))
        return {};
    return QByteArray("W") + QByteArray(1, upper.toLatin1()) + QByteArray("0") + QByteArray::number(value)
           + QByteArray(";");
}

QByteArray registerReadCommand(QChar letter)
{
    const QChar upper = letter.toUpper();
    if (upper < QLatin1Char('A') || upper > QLatin1Char('Z'))
        return {};
    return QByteArray("R") + QByteArray(1, upper.toLatin1()) + QByteArray(";");
}

QByteArray brightnessWriteCommand(int value)
{
    return registerWriteCommand(QLatin1Char('O'), value);
}

QByteArray brightnessReadCommand()
{
    return registerReadCommand(QLatin1Char('O'));
}

QByteArray calibrateEndpoint(bool clockwise)
{
    return clockwise ? QByteArray("C11;") : QByteArray("C01;");
}

QByteArray setPulseHeading(double azimuth)
{
    // Appendix F "AWnxxx(.y);". Width follows the AP1 tenth-degree field.
    return QByteArray("AW1") + azField(azimuth) + QByteArray(";");
}

std::optional<double> parseHeading(const QByteArray &frame)
{
    const QByteArray body = stripNoise(frame);
    if (body.isEmpty())
        return std::nullopt;
    for (unsigned char c : body) {
        if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9'))
            continue;
        return std::nullopt;
    }
    bool ok = false;
    double value = QString::fromLatin1(body).toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;
    if (value == 360.0)
        value = 0.0;
    if (value < 0.0 || value > 359.9)
        return std::nullopt;
    return value;
}

bool isCommandEcho(const QByteArray &frame)
{
    const QByteArray body = stripNoise(frame).toUpper();
    if (body.isEmpty())
        return false;
    if (body == "BI1" || body == "AI1" || body == "AM1" || body == "R21" || body == "R11"
        || body == "R01" || body == "R41" || body == "ST1")
        return true;
    if (body.startsWith("AP"))
        return !parseHeading(body.mid(body.startsWith("AP1") ? 3 : 2)).has_value() || body.startsWith("AP");
    return false;
}

std::optional<double> parseHeadingFrame(const QByteArray &frame)
{
    QByteArray body = stripNoise(frame);
    if (body.isEmpty())
        return std::nullopt;
    if (const auto status = parseStatusFrame(body))
        return status->azimuth;

    const QByteArray upper = body.toUpper();
    if (upper.startsWith("AP") || upper.startsWith("AM") || upper.startsWith("ST") || upper.startsWith("R1")
        || upper.startsWith("R0") || upper.startsWith("R4") || upper == "R21")
        return std::nullopt;

    if (upper.startsWith("BI") || upper.startsWith("AI")) {
        int index = 0;
        while (index < body.size() && std::isalpha(static_cast<unsigned char>(body.at(index))))
            ++index;
        if (index < body.size() && std::isdigit(static_cast<unsigned char>(body.at(index)))) {
            const auto heading = parseHeading(body.mid(index + 1));
            if (heading)
                return heading;
        }
        return std::nullopt;
    }

    if (body.startsWith(';'))
        body = body.mid(1).trimmed();
    return parseHeading(body);
}

std::optional<HeadingReading> parseStatusFrame(const QByteArray &frame)
{
    QByteArray body = frame;
    if (!body.isEmpty() && body.at(0) == char(0x01))
        body.remove(0, 1);
    const int gt = body.indexOf('>');
    if (gt < 1)
        return std::nullopt;
    const auto heading = parseHeading(body.mid(gt + 1));
    if (!heading)
        return std::nullopt;
    HeadingReading reading;
    reading.azimuth = *heading;
    const unsigned char status = static_cast<unsigned char>(body.at(gt - 1));
    if (status >= '0' && status <= '9') {
        reading.haveStatus = true;
        reading.status = status - '0';
    } else if (status <= 5) {
        // Observed on the LAN module: 01 30 xx 3E, where xx is a binary
        // flag (0 stopped, 1 rotating), not the ASCII digit from the manual.
        reading.haveStatus = true;
        reading.status = int(status);
    }
    return reading;
}

QString payloadAfterSoh(const QByteArray &frame)
{
    return QString::fromLatin1(stripNoise(frame));
}

std::optional<int> parseIntegerPayload(const QByteArray &frame)
{
    const QString text = payloadAfterSoh(frame);
    if (text.isEmpty())
        return std::nullopt;
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok)
        return std::nullopt;
    return value;
}

void Framer::commit(const QByteArray &body)
{
    if (body.isEmpty())
        return;
    const int prompt = body.indexOf("<uart2>");
    if (prompt > 0) {
        frames.append(body.left(prompt));
        frames.append(body.mid(prompt));
        return;
    }
    frames.append(body);
}

void Framer::append(const QByteArray &bytes)
{
    // Frames end on ';'. A bare CR still finishes a heading ("37.7\r") because
    // that is how some serial BI1 replies arrive. A leading ';' is only a
    // separator, so ";131.9;" is one heading and never a short ghost.
    constexpr int kMaxFrame = 256;
    for (unsigned char c : bytes) {
        // Drop NUL and other controls. Keep SOH: it delimits an echoed
        // register reply and it is the binary "rotating" flag in an R21 frame.
        if (c == 0x00 || (c < 0x20 && c != '\n' && c != '\r' && c != 0x01)) {
            ++controls;
            continue;
        }
        if (c == 0x01)
            ++controls;
        if (c == '\n' || c == '\r') {
            if (lineMode && !dropping && !buffer.isEmpty()) {
                commit(buffer);
                buffer.clear();
                dropping = false;
                continue;
            }
            if (c == '\n')
                continue;
            if (!dropping && parseHeading(buffer)) {
                commit(buffer);
                buffer.clear();
            }
            continue;
        }
        if (c == ';') {
            if (!dropping && !buffer.isEmpty() && buffer != ";") {
                const QByteArray body = buffer.startsWith(';') ? buffer.mid(1) : buffer;
                commit(body);
            }
            buffer.clear();
            dropping = false;
            continue;
        }
        if (c == '<' && !dropping && !buffer.isEmpty() && buffer.at(0) != '<') {
            commit(buffer);
            buffer.clear();
        }
        if (dropping || buffer.size() >= kMaxFrame) {
            dropping = true;
            buffer.clear();
            continue;
        }
        buffer.append(char(c));
    }
}

QList<QByteArray> Framer::take()
{
    QList<QByteArray> out;
    out.swap(frames);
    return out;
}

void Framer::clear()
{
    buffer.clear();
    frames.clear();
    lineMode = false;
    dropping = false;
}

} // namespace Rt21
