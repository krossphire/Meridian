/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "FirmwareHexFixture.h"
#include "StatusTone.h"
#include "rotor/FirmwareUpdater.h"
#include "rotor/IntelHex.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestHex : public QObject
{
    Q_OBJECT
private slots:
    void knownRecordChecksum();
    void validSyntheticImage();
    void versionStopsAtNul();
    void configWordAtEndOfBlock();
    void checksumMismatch();
    void malformedLine();
    void missingEof();
    void dataAfterEof();
    void bootloaderByte();
    void bootloaderSpan();
    void addressPastProgramSpace();
    void eepromSection();
    void pastConfigBlock();
    void overlap();
    void wrongDevice();
    void missingTrailerFields();
    void missingVersionString();
    void lowercaseRecords();
    void crlfAndBom();
    void loadFile();
    void updaterSendsNothing();
    void refusalIsAFailureTone();
    void officialHexIfPresent();
};

void TestHex::knownRecordChecksum()
{
    QCOMPARE(firmwareHexRecord(0, 4, QByteArray::fromHex("0000")), QStringLiteral(":020000040000FA"));
    QCOMPARE(firmwareHexRecord(0, 1, QByteArray()), QStringLiteral(":00000001FF"));
}

void TestHex::validSyntheticImage()
{
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(validFirmwareHex());
    QVERIFY2(image.ok, qPrintable(image.error));
    QCOMPARE(image.device, QStringLiteral("PIC18F26K22"));
    QCOMPARE(image.crc, QStringLiteral("9241"));
    QCOMPARE(image.created, QStringLiteral("2026-11-19"));
    QCOMPARE(image.version, QStringLiteral("RT-21 4.13.2"));
    QCOMPARE(image.applicationBytes, 13);
    QCOMPARE(image.configBytes, 1);
    QCOMPARE(image.bytes.value(0x0400), quint8(0xA5));
    QCOMPARE(image.bytes.value(0x0676), quint8('R'));
    QCOMPARE(image.bytes.value(0x300000), quint8(0x00));
    QVERIFY(!image.bytes.contains(0x0000));
    QVERIFY(!image.bytes.contains(0x03FF));
}

void TestHex::versionStopsAtNul()
{
    QByteArray version("RT-21 4.13.2");
    version.append(char(0));
    version.append("XXX");
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, version) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY2(image.ok, qPrintable(image.error));
    QCOMPARE(image.version, QStringLiteral("RT-21 4.13.2"));
    QCOMPARE(image.bytes.value(0x0676 + 13), quint8('X'));
}

void TestHex::configWordAtEndOfBlock()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexRecord(0, 4, QByteArray::fromHex("0030")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x000D, 0, QByteArray(1, char(0x11))) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY2(image.ok, qPrintable(image.error));
    QCOMPARE(image.bytes.value(0x30000D), quint8(0x11));
    QCOMPARE(image.configBytes, 1);
}

void TestHex::checksumMismatch()
{
    QString hex = validFirmwareHex();
    QVERIFY(hex.contains(QStringLiteral(":020000040000FA")));
    hex.replace(QStringLiteral(":020000040000FA"), QStringLiteral(":020000040000FB"));
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Checksum mismatch on line 1"));
    QCOMPARE(image.device, QStringLiteral("PIC18F26K22"));
    QCOMPARE(image.crc, QStringLiteral("9241"));
}

void TestHex::malformedLine()
{
    const Rt21::FirmwareImage bad = Rt21::loadFirmwareHex(QStringLiteral(":GG0000040000FA\n") + firmwareHexEof());
    QVERIFY(!bad.ok);
    QCOMPARE(bad.error, QStringLiteral("Malformed Intel HEX on line 1"));

    const Rt21::FirmwareImage odd = Rt21::loadFirmwareHex(QStringLiteral(":020000040000F\n"));
    QVERIFY(!odd.ok);
    QCOMPARE(odd.error, QStringLiteral("Malformed Intel HEX on line 1"));

    const QString shortAddress = firmwareHexRecord(0, 4, QByteArray(1, char(0))) + QLatin1Char('\n') + firmwareHexEof()
                                 + firmwareHexTrailer();
    const Rt21::FirmwareImage wrong = Rt21::loadFirmwareHex(shortAddress);
    QVERIFY(!wrong.ok);
    QCOMPARE(wrong.error, QStringLiteral("Malformed Intel HEX on line 1"));
}

void TestHex::missingEof()
{
    QString hex = validFirmwareHex();
    hex.replace(firmwareHexEof(), QString());
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Missing end-of-file record"));
}

void TestHex::dataAfterEof()
{
    const QString hex = validFirmwareHex() + firmwareHexRecord(0x0800, 0, QByteArray(1, char(0x01))) + QLatin1Char('\n');
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QVERIFY(image.error.startsWith(QStringLiteral("Data after the end-of-file record")));
}

void TestHex::bootloaderByte()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0010, 0, QByteArray(1, char(0xFF))) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Image touches the bootloader at 0x0000-0x03FF (0x0010)"));
    QVERIFY(!image.bytes.contains(0x0010));
}

void TestHex::bootloaderSpan()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x03FE, 0, QByteArray::fromHex("01020304")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Image touches the bootloader at 0x0000-0x03FF (0x03FE)"));
    QVERIFY(!image.bytes.contains(0x0400));
    QVERIFY(!image.bytes.contains(0x03FE));
}

void TestHex::addressPastProgramSpace()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexRecord(0, 4, QByteArray::fromHex("0001")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0000, 0, QByteArray(1, char(0x22))) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Address 0x010000 is outside 0x0400-0xFFFF and the config words"));
}

void TestHex::eepromSection()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexRecord(0, 4, QByteArray::fromHex("00F0")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0000, 0, QByteArray(1, char(0x33))) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Address 0xF00000 is outside 0x0400-0xFFFF and the config words"));
}

void TestHex::pastConfigBlock()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexRecord(0, 4, QByteArray::fromHex("0030")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x000E, 0, QByteArray(1, char(0x44))) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Address 0x30000E is outside 0x0400-0xFFFF and the config words"));
}

void TestHex::overlap()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Overlapping data at 0x0676"));
}

void TestHex::wrongDevice()
{
    QString hex = validFirmwareHex();
    hex.replace(QStringLiteral(";PIC18F26K22"), QStringLiteral(";PIC18F46K22"));
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("Device trailer is PIC18F46K22, not PIC18F26K22"));
    QCOMPARE(image.crc, QStringLiteral("9241"));
    QCOMPARE(image.created, QStringLiteral("2026-11-19"));
}

void TestHex::missingTrailerFields()
{
    const QString records = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                            + firmwareHexRecord(0x0676, 0, QByteArray("RT-21 4.13.2")) + QLatin1Char('\n')
                            + firmwareHexEof();
    const Rt21::FirmwareImage noDevice = Rt21::loadFirmwareHex(records + QStringLiteral(";CRC=9241 CREATED=2026-11-19\n"));
    QVERIFY(!noDevice.ok);
    QCOMPARE(noDevice.error, QStringLiteral("Missing PIC18F26K22 trailer"));

    const Rt21::FirmwareImage crcOnly = Rt21::loadFirmwareHex(records + QStringLiteral(";PIC18F26K22\n;CRC=9241\n"));
    QVERIFY(!crcOnly.ok);
    QCOMPARE(crcOnly.crc, QStringLiteral("9241"));
    QCOMPARE(crcOnly.error, QStringLiteral("Missing CREATED field"));

    const Rt21::FirmwareImage noCrc = Rt21::loadFirmwareHex(records + QStringLiteral(";PIC18F26K22\n;CREATED=2026-11-19\n"));
    QVERIFY(!noCrc.ok);
    QCOMPARE(noCrc.error, QStringLiteral("Missing CRC= field"));
}

void TestHex::missingVersionString()
{
    const QString hex = firmwareHexRecord(0, 4, QByteArray::fromHex("0000")) + QLatin1Char('\n')
                        + firmwareHexRecord(0x0800, 0, QByteArray("HELLO")) + QLatin1Char('\n')
                        + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(hex);
    QVERIFY(!image.ok);
    QCOMPARE(image.error, QStringLiteral("No RT-21 version string at 0x0676"));

    const QString empty = firmwareHexRecord(0, 4, QByteArray::fromHex("0030")) + QLatin1Char('\n')
                          + firmwareHexRecord(0x0000, 0, QByteArray(1, char(0x01))) + QLatin1Char('\n')
                          + firmwareHexEof() + firmwareHexTrailer();
    const Rt21::FirmwareImage configOnly = Rt21::loadFirmwareHex(empty);
    QVERIFY(!configOnly.ok);
    QCOMPARE(configOnly.error, QStringLiteral("Image has no application bytes"));
}

void TestHex::lowercaseRecords()
{
    QStringList lines = validFirmwareHex().split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line.startsWith(QLatin1Char(':')))
            line = line.toLower();
    }
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHex(lines.join(QLatin1Char('\n')));
    QVERIFY2(image.ok, qPrintable(image.error));
    QCOMPARE(image.version, QStringLiteral("RT-21 4.13.2"));
}

void TestHex::crlfAndBom()
{
    const QString crlf = validFirmwareHex().replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    QVERIFY2(Rt21::loadFirmwareHex(crlf).ok, qPrintable(Rt21::loadFirmwareHex(crlf).error));
    const Rt21::FirmwareImage bom = Rt21::loadFirmwareHex(QString(QChar(0xFEFF)) + validFirmwareHex());
    QVERIFY2(bom.ok, qPrintable(bom.error));
}

void TestHex::loadFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("RT21_4_13.2.hex"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(validFirmwareHex().toUtf8());
    file.close();
    const Rt21::FirmwareImage image = Rt21::loadFirmwareHexFile(path);
    QVERIFY2(image.ok, qPrintable(image.error));
    QCOMPARE(image.version, QStringLiteral("RT-21 4.13.2"));

    const Rt21::FirmwareImage missing = Rt21::loadFirmwareHexFile(path + QStringLiteral(".absent"));
    QVERIFY(!missing.ok);
    QVERIFY(missing.error.contains(QStringLiteral("Couldn't read")));
}

void TestHex::updaterSendsNothing()
{
    Rt21::RefusingFirmwareUpdater updater;
    Rt21::FirmwareImage image = Rt21::loadFirmwareHex(validFirmwareHex());
    QVERIFY(image.ok);
    QString status;
    QVERIFY(!updater.flash(image, &status));
    QCOMPARE(status, QStringLiteral("Bootloader protocol not yet captured"));
    QCOMPARE(updater.bytesSent(), 0);

    QFile source(QString::fromLatin1(MERIDIAN_SOURCE_DIR) + QStringLiteral("/src/rotor/FirmwareUpdater.cpp"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(source.readAll());
    QVERIFY(!text.contains(QStringLiteral("B2")));
    QVERIFY(!text.contains(QStringLiteral("BU")));
}

void TestHex::refusalIsAFailureTone()
{
    QCOMPARE(statusTone(QStringLiteral("Bootloader protocol not yet captured")), StatusTone::Failure);
}

void TestHex::officialHexIfPresent()
{
    const QString root = QString::fromLatin1(MERIDIAN_SOURCE_DIR);
    const QStringList names = {
        QStringLiteral("/RT21_4_13.2.hex"),
        QStringLiteral("/data/RT21_4_13.2.hex"),
        QStringLiteral("/firmware/RT21_4_13.2.hex"),
    };
    QString path;
    for (const QString &name : names) {
        if (QFile::exists(root + name)) {
            path = root + name;
            break;
        }
    }
    if (path.isEmpty())
        QSKIP("RT21_4_13.2.hex is not in the repo");

    const Rt21::FirmwareImage image = Rt21::loadFirmwareHexFile(path);
    QVERIFY2(image.ok, qPrintable(image.error));
    QCOMPARE(image.device, QStringLiteral("PIC18F26K22"));
    QCOMPARE(image.version, QStringLiteral("RT-21 4.13.2"));
    QVERIFY(!image.crc.isEmpty());
    QVERIFY(!image.created.isEmpty());
    for (auto it = image.bytes.cbegin(); it != image.bytes.cend(); ++it)
        QVERIFY(it.key() > Rt21::kFirmwareBootloaderEnd);
}

QTEST_GUILESS_MAIN(TestHex)
#include "tst_hex.moc"
