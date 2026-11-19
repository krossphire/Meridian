/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rotor/UsbHint.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestUsb : public QObject
{
    Q_OBJECT
private slots:
    void chipsWithoutPortsArePossible();
    void aPortMeansTheDriverIsThere();
    void descriptorFailureIsNotAnRt21();
    void oldFailureStaysQuiet();
    void sysfsTree();
};

UsbDeviceFact device(quint16 vid, quint16 pid, const QString &name, int problem, bool port, int ageSeconds)
{
    UsbDeviceFact fact;
    fact.vid = vid;
    fact.pid = pid;
    fact.name = name;
    fact.instanceId = QStringLiteral("%1:%2").arg(vid, 4, 16, QLatin1Char('0')).arg(pid, 4, 16, QLatin1Char('0'));
    fact.problemCode = problem;
    fact.hasSerialPort = port;
    if (ageSeconds >= 0)
        fact.lastArrival = QDateTime::currentDateTimeUtc().addSecs(-ageSeconds);
    return fact;
}

void TestUsb::chipsWithoutPortsArePossible()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QList<UsbDeviceFact> devices = {
        device(0x10C4, 0xEA60, QStringLiteral("CP2102 USB to UART Bridge"), 28, false, 30),
        device(0x0403, 0x6001, QStringLiteral("FT232R USB UART"), 28, false, 30),
    };
    const QList<UsbHint> hints = classifyUsbDevices(devices, now, true, {});
    QCOMPARE(hints.size(), 2);
    QCOMPARE(hints.at(0).family, UartFamily::Cp210x);
    QVERIFY(hints.at(0).text.startsWith(QStringLiteral("Possible RT-21.")));
    QVERIFY(hints.at(0).text.contains(QStringLiteral("CP210x")));
    QVERIFY(hints.at(0).text.contains(QStringLiteral("November 2022")));
    QVERIFY(hints.at(0).html.contains(QStringLiteral("silabs.com")));
    QCOMPARE(hints.at(1).family, UartFamily::Ftdi);
    QVERIFY(hints.at(1).text.contains(QStringLiteral("2000")));
    QVERIFY(hints.at(1).text.contains(QStringLiteral("Windows Update")));
    QVERIFY(hints.at(1).html.contains(QStringLiteral("greenheronengineering.com")));
    QVERIFY(!hints.at(1).text.contains(QStringLiteral("CP210x driver")));
}

void TestUsb::aPortMeansTheDriverIsThere()
{
    UsbDeviceFact chip = device(0x10C4, 0xEA60, QStringLiteral("Silicon Labs CP210x"), -1, true, 10);
    const QList<UsbHint> hints = classifyUsbDevices({chip}, QDateTime::currentDateTimeUtc(), true, {});
    QVERIFY(hints.isEmpty());
    QCOMPARE(uartFamily(0x10C4, 0xEA70), UartFamily::Cp210x);
    QCOMPARE(uartFamily(0x0403, 0x6015), UartFamily::Ftdi);
    QCOMPARE(uartFamily(0x046D, 0xC52B), UartFamily::None);
}

void TestUsb::descriptorFailureIsNotAnRt21()
{
    UsbDeviceFact failed = device(0x0000, 0x0002, QStringLiteral("Unknown USB Device (Device Descriptor Request Failed)"), 43,
                                  false, 60);
    QVERIFY(isDescriptorFailure(failed));
    const QList<UsbHint> hints = classifyUsbDevices({failed}, QDateTime::currentDateTimeUtc(), false, {});
    QCOMPARE(hints.size(), 1);
    QCOMPARE(hints.at(0).kind, UsbHint::Kind::FailedStart);
    QCOMPARE(hints.at(0).text,
             QStringLiteral("A USB device failed to start. If that's your RT-21, try another data cable, a rear USB port, "
                            "or power-cycle the controller."));
    QVERIFY(!hints.at(0).text.contains(QStringLiteral("Possible RT-21")));
    QVERIFY(!hints.at(0).text.contains(QStringLiteral("CP210x")));
    QVERIFY(!hints.at(0).text.contains(QStringLiteral("FTDI")));
    QVERIFY(hints.at(0).html == hints.at(0).text);
}

void TestUsb::oldFailureStaysQuiet()
{
    UsbDeviceFact failed = device(0x0000, 0x0002, QStringLiteral("Unknown USB Device (Device Descriptor Request Failed)"), 43,
                                  false, 3 * 24 * 3600);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVERIFY(!failedStartIsRecent(failed, now, true, {failed.instanceId}));
    QVERIFY(classifyUsbDevices({failed}, now, true, {failed.instanceId}).isEmpty());
    QVERIFY(failedStartIsRecent(failed, now, true, {}));
    UsbDeviceFact nameless;
    nameless.name = QStringLiteral("Device Descriptor Request Failed");
    nameless.instanceId = QStringLiteral("stale");
    nameless.lastArrival = now.addDays(-2);
    QVERIFY(isDescriptorFailure(nameless));
    QVERIFY(classifyUsbDevices({nameless}, now, true, {nameless.instanceId}).isEmpty());
}

void TestUsb::sysfsTree()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString chip = root.filePath(QStringLiteral("1-2"));
    QVERIFY(QDir().mkpath(chip + QStringLiteral("/1-2:1.0")));
    QFile vendor(chip + QStringLiteral("/idVendor"));
    QVERIFY(vendor.open(QIODevice::WriteOnly));
    vendor.write("10c4\n");
    vendor.close();
    QFile product(chip + QStringLiteral("/idProduct"));
    QVERIFY(product.open(QIODevice::WriteOnly));
    product.write("ea60\n");
    product.close();
    QFile label(chip + QStringLiteral("/product"));
    QVERIFY(label.open(QIODevice::WriteOnly));
    label.write("CP2102\n");
    label.close();

    const QString failed = root.filePath(QStringLiteral("1-3"));
    QVERIFY(QDir().mkpath(failed));
    QFile badVid(failed + QStringLiteral("/idVendor"));
    QVERIFY(badVid.open(QIODevice::WriteOnly));
    badVid.write("0000\n");
    badVid.close();
    QFile badPid(failed + QStringLiteral("/idProduct"));
    QVERIFY(badPid.open(QIODevice::WriteOnly));
    badPid.write("0002\n");
    badPid.close();

    const QList<UsbDeviceFact> facts = usbFactsFromSysfs(root.path());
    QCOMPARE(facts.size(), 2);
    QCOMPARE(facts.at(0).vid, quint16(0x10C4));
    QCOMPARE(facts.at(0).pid, quint16(0xEA60));
    QVERIFY(!facts.at(0).hasSerialPort);
    QVERIFY(isDescriptorFailure(facts.at(1)));

    QVERIFY(QDir().mkpath(chip + QStringLiteral("/1-2:1.0/ttyUSB0")));
    const QList<UsbDeviceFact> withPort = usbFactsFromSysfs(root.path());
    QVERIFY(withPort.at(0).hasSerialPort);
}

QTEST_GUILESS_MAIN(TestUsb)
#include "tst_usb.moc"
