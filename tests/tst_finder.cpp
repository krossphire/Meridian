/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Finder.h"

#include <QTest>

#include <cmath>

class TestFinder : public QObject
{
    Q_OBJECT
private slots:
    void chipNames();
    void labelText();
    void freshRotorHasNoAddress();
    void tcpProbe();
#ifdef Q_OS_UNIX
    void serialProbe();
#endif
    void missingPortIsQuiet();
};

void TestFinder::chipNames()
{
    QCOMPARE(usbChipName(QStringLiteral("FTDI"), QStringLiteral("USB Serial")), QStringLiteral("FTDI"));
    QCOMPARE(usbChipName(QStringLiteral("Prolific"), QStringLiteral("USB-to-Serial")), QStringLiteral("Prolific"));
    QCOMPARE(usbChipName(QStringLiteral("wch.cn"), QStringLiteral("USB-SERIAL CH340")), QStringLiteral("CH340"));
    QCOMPARE(usbChipName(QStringLiteral("Silicon Labs"), QStringLiteral("CP2102")), QStringLiteral("CP210"));
    QVERIFY(usbChipName(QStringLiteral("Acme"), QStringLiteral("Widget")).isEmpty());
}

void TestFinder::labelText()
{
    QCOMPARE(foundRotorLabel(QStringLiteral("COM6"), QStringLiteral("USB"), 4800, 132),
             QStringLiteral("RT-21 on COM6 (USB, 4800), heading 132°"));
    QCOMPARE(foundRotorLabel(QStringLiteral("COM4"), QStringLiteral("FTDI"), 9600, 12.5),
             QStringLiteral("RT-21 on COM4 (FTDI, 9600), heading 12.5°"));
    QCOMPARE(foundRotorLabel(QStringLiteral("192.168.1.20:6555"), QString(), 0, 90),
             QStringLiteral("RT-21 at 192.168.1.20:6555, heading 90°"));
}

void TestFinder::freshRotorHasNoAddress()
{
    const AppConfig config = AppConfig::defaults();
    QCOMPARE(config.rotors.size(), 1);
    QVERIFY(!config.rotors.at(0).configured());
    QCOMPARE(config.rotors.at(0).transport, RotorEndpoint::Transport::Unset);
    QVERIFY(config.rotors.at(0).host.isEmpty());
    QCOMPARE(config.rotors.at(0).tcpPort, 0);
    QVERIFY(!config.rotors.at(0).host.contains(QStringLiteral("192.168")));
}

void TestFinder::tcpProbe()
{
    MockRt21Server server;
    server.sim()->setHeading(132);
    QVERIFY(server.listenTcp(0));
    const auto found = Rt21Finder::probeTcp(QStringLiteral("127.0.0.1"), server.port(), 1000);
    QVERIFY(found);
    QCOMPARE(found->endpoint.transport, RotorEndpoint::Transport::Tcp);
    QCOMPARE(found->endpoint.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(found->endpoint.tcpPort, int(server.port()));
    QVERIFY(std::abs(found->heading - 132.0) < 0.2);
    QVERIFY(found->label.contains(QStringLiteral("heading 132")));
    QVERIFY(!Rt21Finder::probeTcp(QStringLiteral("192.0.2.1"), 9, 300));
}

#ifdef Q_OS_UNIX
void TestFinder::serialProbe()
{
    MockRt21Server server;
    server.sim()->setHeading(132);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");
    const auto found = Rt21Finder::probeSerial(pty, QStringLiteral("USB"), 4800, 1000);
    QVERIFY(found);
    QCOMPARE(found->endpoint.transport, RotorEndpoint::Transport::Serial);
    QCOMPARE(found->endpoint.serialPort, pty);
    QCOMPARE(found->endpoint.baud, 4800);
    QVERIFY(std::abs(found->heading - 132.0) < 0.2);
    QVERIFY(found->label.contains(QStringLiteral("(USB, 4800)")));
}
#endif

void TestFinder::missingPortIsQuiet()
{
    QVERIFY(!Rt21Finder::probeSerial(QStringLiteral("meridian-no-such-port"), QStringLiteral("USB"), 4800, 200));
}

QTEST_GUILESS_MAIN(TestFinder)
#include "tst_finder.moc"
