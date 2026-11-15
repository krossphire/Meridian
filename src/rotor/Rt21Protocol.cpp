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
    if (!frame.isEmpty() && frame.at(0) == char(0x01))
        frame.remove(0, 1);
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
    // Hamlib RT-21: "AP1XXX.Y\r;" — the CR makes the controller move now.
    // Sent on serial and on TCP. Whether TCP requires the CR is unverified
    // on hardware; the mock accepts both.
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
    // Inferred from Appendix F "WXn(value)" with n ≠ 0 (write, do not reboot):
    // 'W' + location + '1' + decimal ASCII + ';'. Not verified on hardware.
    const QChar letter = location.toUpper();
    return QByteArray("W") + QByteArray(1, letter.toLatin1()) + QByteArray("1")
           + QByteArray::number(value) + QByteArray(";");
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

void Framer::append(const QByteArray &bytes)
{
    for (unsigned char c : bytes) {
        if (c == '\n' || c == '\r') {
            if (lineMode && !buffer.isEmpty()) {
                frames.append(buffer);
                buffer.clear();
                continue;
            }
            if (c == '\n')
                continue;
            // "100.0\r;" still terminates on the semicolon. A bare CR after a
            // heading is also accepted so a short reply is not stuck.
            if (parseHeading(buffer)) {
                frames.append(buffer);
                buffer.clear();
            }
            continue;
        }
        if (c == ';') {
            if (buffer.isEmpty()) {
                buffer = ";"; // DCU-1 replies start with ';' and have no trailer.
                continue;
            }
            if (buffer == ";") {
                buffer.clear();
                continue;
            }
            frames.append(buffer.startsWith(';') ? buffer.mid(1) : buffer);
            buffer.clear();
            continue;
        }
        buffer.append(char(c));
        if (buffer.size() == 4 && buffer.at(0) == ';'
            && std::isdigit(static_cast<unsigned char>(buffer.at(1)))
            && std::isdigit(static_cast<unsigned char>(buffer.at(2)))
            && std::isdigit(static_cast<unsigned char>(buffer.at(3)))) {
            frames.append(buffer.mid(1));
            buffer.clear();
        }
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
}

} // namespace Rt21
