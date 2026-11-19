/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IntelHex.h"

#include <QFile>
#include <QList>
#include <QRegularExpression>

namespace Rt21 {
namespace {

QString hexAddress(quint32 address)
{
    const int width = address > 0xFFFF ? 6 : 4;
    return QStringLiteral("0x") + QStringLiteral("%1").arg(address, width, 16, QLatin1Char('0')).toUpper();
}

bool inRange(quint32 address)
{
    if (address >= kFirmwareAppStart && address <= kFirmwareAppEnd)
        return true;
    return address >= kFirmwareConfigStart && address <= kFirmwareConfigEnd;
}

QString versionAt(const QMap<quint32, quint8> &bytes)
{
    QString text;
    for (int i = 0; i < 32; ++i) {
        const auto it = bytes.constFind(kFirmwareVersionAddress + quint32(i));
        if (it == bytes.cend())
            break;
        const quint8 byte = it.value();
        if (byte == 0)
            break;
        if (byte < 0x20 || byte > 0x7E)
            break;
        text.append(QChar(byte));
    }
    return text;
}

void noteComment(const QString &line, FirmwareImage *image)
{
    static const QRegularExpression deviceRe(QStringLiteral("^;\\s*(PIC[0-9A-Za-z]+)\\s*$"));
    static const QRegularExpression crcCreatedRe(QStringLiteral("^;\\s*CRC=([0-9A-Fa-f]+)\\s+CREATED=(.*)$"));
    static const QRegularExpression crcOnlyRe(QStringLiteral("^;\\s*CRC=([0-9A-Fa-f]+)\\s*$"));
    static const QRegularExpression createdOnlyRe(QStringLiteral("^;\\s*CREATED=(.*)$"));

    const QRegularExpressionMatch device = deviceRe.match(line);
    if (device.hasMatch()) {
        image->device = device.captured(1);
        return;
    }
    const QRegularExpressionMatch both = crcCreatedRe.match(line);
    if (both.hasMatch()) {
        image->crc = both.captured(1);
        image->created = both.captured(2).trimmed();
        return;
    }
    const QRegularExpressionMatch crc = crcOnlyRe.match(line);
    if (crc.hasMatch()) {
        image->crc = crc.captured(1);
        return;
    }
    const QRegularExpressionMatch created = createdOnlyRe.match(line);
    if (created.hasMatch())
        image->created = created.captured(1).trimmed();
}

bool decodeRecord(const QString &line, QByteArray *raw, bool *checksumOk)
{
    *checksumOk = false;
    if (line.size() < 11 || !line.startsWith(QLatin1Char(':')))
        return false;
    if (((line.size() - 1) % 2) != 0)
        return false;
    QByteArray decoded;
    decoded.reserve((line.size() - 1) / 2);
    for (int i = 1; i < line.size(); i += 2) {
        bool ok = false;
        const int value = line.mid(i, 2).toInt(&ok, 16);
        if (!ok || value < 0 || value > 255)
            return false;
        decoded.append(char(value));
    }
    const int count = quint8(decoded.at(0));
    if (decoded.size() != count + 5)
        return false;
    int sum = 0;
    for (unsigned char byte : decoded)
        sum = (sum + byte) & 0xFF;
    *checksumOk = sum == 0;
    *raw = decoded;
    return true;
}

void finish(FirmwareImage *image, bool sawEof)
{
    if (image->error.isEmpty() && !sawEof)
        image->error = QStringLiteral("Missing end-of-file record");
    if (image->error.isEmpty() && image->device != QLatin1String("PIC18F26K22")) {
        image->error = image->device.isEmpty()
                           ? QStringLiteral("Missing PIC18F26K22 trailer")
                           : QStringLiteral("Device trailer is %1, not PIC18F26K22").arg(image->device);
    }
    if (image->error.isEmpty() && image->crc.isEmpty())
        image->error = QStringLiteral("Missing CRC= field");
    if (image->error.isEmpty() && image->created.isEmpty())
        image->error = QStringLiteral("Missing CREATED field");
    if (image->error.isEmpty() && image->applicationBytes == 0)
        image->error = QStringLiteral("Image has no application bytes");
    image->version = versionAt(image->bytes);
    if (image->error.isEmpty() && !image->version.startsWith(QLatin1String("RT-21")))
        image->error = QStringLiteral("No RT-21 version string at 0x0676");
    image->ok = image->error.isEmpty();
}

} // namespace

FirmwareImage loadFirmwareHex(const QString &text)
{
    FirmwareImage image;
    QString body = text;
    if (body.startsWith(QChar(0xFEFF)))
        body.remove(0, 1);

    quint32 base = 0;
    bool sawEof = false;
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const int lineNo = i + 1;
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char(';'))) {
            noteComment(line, &image);
            continue;
        }

        QByteArray raw;
        bool checksumOk = false;
        if (!decodeRecord(line, &raw, &checksumOk)) {
            if (image.error.isEmpty())
                image.error = QStringLiteral("Malformed Intel HEX on line %1").arg(lineNo);
            continue;
        }
        if (!checksumOk) {
            if (image.error.isEmpty())
                image.error = QStringLiteral("Checksum mismatch on line %1").arg(lineNo);
            continue;
        }
        if (sawEof) {
            if (image.error.isEmpty())
                image.error = QStringLiteral("Data after the end-of-file record on line %1").arg(lineNo);
            continue;
        }

        const int count = quint8(raw.at(0));
        const int offset = (quint8(raw.at(1)) << 8) | quint8(raw.at(2));
        const int type = quint8(raw.at(3));
        const QByteArray data = raw.mid(4, count);

        if (type == 0x01) {
            if (count != 0) {
                if (image.error.isEmpty())
                    image.error = QStringLiteral("Malformed Intel HEX on line %1").arg(lineNo);
                continue;
            }
            sawEof = true;
            continue;
        }
        if (type == 0x02 || type == 0x04) {
            if (count != 2) {
                if (image.error.isEmpty())
                    image.error = QStringLiteral("Malformed Intel HEX on line %1").arg(lineNo);
                continue;
            }
            const quint32 word = (quint8(data.at(0)) << 8) | quint8(data.at(1));
            base = type == 0x04 ? (word << 16) : (word << 4);
            continue;
        }
        if (type == 0x03 || type == 0x05) {
            if (count != 4 && image.error.isEmpty())
                image.error = QStringLiteral("Malformed Intel HEX on line %1").arg(lineNo);
            continue;
        }
        if (type != 0x00) {
            if (image.error.isEmpty())
                image.error = QStringLiteral("Unsupported record type on line %1").arg(lineNo);
            continue;
        }

        bool place = image.error.isEmpty();
        QList<quint32> addresses;
        addresses.reserve(count);
        for (int n = 0; n < count; ++n) {
            const quint64 full = quint64(base) + quint32(offset) + quint32(n);
            if (full > 0xFFFFFFFFULL) {
                if (image.error.isEmpty())
                    image.error = QStringLiteral("Malformed Intel HEX on line %1").arg(lineNo);
                place = false;
                break;
            }
            const quint32 address = quint32(full);
            if (address <= kFirmwareBootloaderEnd) {
                if (image.error.isEmpty()) {
                    image.error = QStringLiteral("Image touches the bootloader at 0x0000-0x03FF (%1)")
                                      .arg(hexAddress(address));
                }
                place = false;
                break;
            }
            if (!inRange(address)) {
                if (image.error.isEmpty()) {
                    image.error = QStringLiteral(
                                      "Address %1 is outside 0x0400-0xFFFF and the config words")
                                      .arg(hexAddress(address));
                }
                place = false;
                break;
            }
            if (image.bytes.contains(address)) {
                if (image.error.isEmpty())
                    image.error = QStringLiteral("Overlapping data at %1").arg(hexAddress(address));
                place = false;
                break;
            }
            addresses.append(address);
        }
        if (!place)
            continue;
        for (int n = 0; n < addresses.size(); ++n) {
            const quint32 address = addresses.at(n);
            image.bytes.insert(address, quint8(data.at(n)));
            if (address >= kFirmwareConfigStart)
                ++image.configBytes;
            else
                ++image.applicationBytes;
        }
    }

    finish(&image, sawEof);
    return image;
}

FirmwareImage loadFirmwareHexFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        FirmwareImage image;
        image.error = QStringLiteral("Couldn't read %1.").arg(path);
        return image;
    }
    return loadFirmwareHex(QString::fromUtf8(file.readAll()));
}

} // namespace Rt21
