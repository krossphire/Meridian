/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Finder.h"
#include "rotor/UserFiles.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

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
    void networkScanStopsAtFirstHit();
    void savedIpIsReplacedOnlyWhenDown();
#ifdef Q_OS_UNIX
    void serialScanStopsAtFirstHit();
#endif
    void settingsFileBesideExe();
    void settingsRoundTrip();
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
    QCOMPARE(foundRotorLabel(QStringLiteral("192.168.1.106:6555"), QString(), 0, 1),
             QStringLiteral("RT-21 at 192.168.1.106:6555 (network), heading 1°"));
    const QList<int> ports = networkCandidatePorts({6555, 7000});
    QVERIFY(ports.contains(23));
    QVERIFY(ports.contains(6555));
    QVERIFY(ports.contains(6665));
    QVERIFY(ports.contains(8080));
    QVERIFY(ports.contains(10001));
    QCOMPARE(ports.count(6555), 1);
    QVERIFY(ports.contains(7000));
    QVERIFY(isVirtualAdapterName(QStringLiteral("WireGuard VPN")));
    QVERIFY(!isVirtualAdapterName(QStringLiteral("Ethernet")));
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
    QVERIFY(found->label.contains(QStringLiteral("(network)")));
    QVERIFY(found->label.contains(QStringLiteral("heading 132")));
    QVERIFY(found->label.contains(QStringLiteral("4.13.2")));
    const auto swept = Rt21Finder::scanHosts({QStringLiteral("127.0.0.1")}, {int(server.port())}, 300);
    QCOMPARE(swept.size(), 1);
    QVERIFY(Rt21Finder::scanHosts({QStringLiteral("127.0.0.1")}, {9}, 200).isEmpty());
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

void TestFinder::networkScanStopsAtFirstHit()
{
    MockRt21Server server;
    server.sim()->setHeading(132);
    QVERIFY(server.listenTcp(0));
    qint64 elapsed = -1;
    QList<FoundRotor> found;
    // The dead host would otherwise hold the batch for the full connect timeout.
    // alpha25d then destroyed those sockets as the worker thread exited.
    QThread *thread = QThread::create([&]() {
        QElapsedTimer timer;
        timer.start();
        found = Rt21Finder::scanHosts({QStringLiteral("127.0.0.1"), QStringLiteral("192.0.2.1")},
                                      {int(server.port()), 9}, 3000);
        elapsed = timer.elapsed();
    });
    QEventLoop loop;
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    QVERIFY(thread->isFinished());
    delete thread;
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.at(0).endpoint.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(found.at(0).endpoint.tcpPort, int(server.port()));
    QVERIFY2(elapsed >= 0 && elapsed < 2000, qPrintable(QString::number(elapsed)));
}

void TestFinder::savedIpIsReplacedOnlyWhenDown()
{
    RotorEndpoint saved;
    saved.transport = RotorEndpoint::Transport::Tcp;
    saved.host = QStringLiteral("192.168.1.106");
    saved.tcpPort = 6555;
    RotorEndpoint serial;
    serial.transport = RotorEndpoint::Transport::Serial;
    serial.serialPort = QStringLiteral("COM6");
    serial.baud = 4800;
    RotorEndpoint otherPort = saved;
    otherPort.tcpPort = 6665;
    QVERIFY(findReplacesSavedEndpoint(saved, false, serial));
    QVERIFY(findReplacesSavedEndpoint(saved, false, otherPort));
    QVERIFY(!findReplacesSavedEndpoint(saved, true, serial));
    QVERIFY(!findReplacesSavedEndpoint(saved, false, saved));
    RotorEndpoint blank;
    QVERIFY(!findReplacesSavedEndpoint(blank, false, serial));
}

#ifdef Q_OS_UNIX
void TestFinder::serialScanStopsAtFirstHit()
{
    MockRt21Server first;
    MockRt21Server second;
    first.sim()->setHeading(11);
    second.sim()->setHeading(22);
    const QString primary = first.listenPty();
    const QString extra = second.listenPty();
    if (primary.isEmpty() || extra.isEmpty())
        QSKIP("pty was not available");
    QList<FoundRotor> found;
    QThread *thread = QThread::create([&]() {
        found = Rt21Finder::scanSerialPorts(1000, {}, {}, {primary, extra});
    });
    QEventLoop loop;
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    QVERIFY(thread->isFinished());
    delete thread;
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.at(0).endpoint.serialPort, primary);
    QCOMPARE(second.sim()->trace().size(), 0);
}
#endif

void TestFinder::settingsFileBesideExe()
{
    QCOMPARE(UserFiles::chooseRoot(QStringLiteral("/opt/Meridian"), QStringLiteral("/tmp/appdata"), true),
             QStringLiteral("/opt/Meridian"));
    QCOMPARE(UserFiles::chooseRoot(QStringLiteral("/Program Files/Meridian"), QStringLiteral("C:/Users/a/AppData/Roaming/Meridian"),
                                   false),
             QStringLiteral("C:/Users/a/AppData/Roaming/Meridian"));
}

void TestFinder::settingsRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserFiles::setRoot(dir.path());
    QCOMPARE(UserFiles::root(), dir.path());
    QCOMPARE(UserFiles::settingsPath(), QDir(dir.path()).filePath(QStringLiteral("meridian.ini")));
    QCOMPARE(UserFiles::logDirectory(), QDir(dir.path()).filePath(QStringLiteral("logs")));
    QCOMPARE(UserFiles::backupDirectory(), QDir(dir.path()).filePath(QStringLiteral("backups")));

    AppConfig config = AppConfig::defaults();
    config.homeText = QStringLiteral("EM26ul");
    config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
    config.rotors[0].host = QStringLiteral("10.0.0.8");
    config.rotors[0].tcpPort = 6555;
    config.showSun = false;
    UserFiles::save(config);
    QVERIFY(QFileInfo::exists(UserFiles::settingsPath()));
    QVERIFY(!QFileInfo::exists(UserFiles::settingsPath() + QStringLiteral(".tmp")));

    const AppConfig loaded = UserFiles::load();
    QCOMPARE(loaded.homeText, QStringLiteral("EM26ul"));
    QCOMPARE(loaded.rotors.at(0).host, QStringLiteral("10.0.0.8"));
    QCOMPARE(loaded.rotors.at(0).tcpPort, 6555);
    QVERIFY(!loaded.showSun);

    config.homeText = QStringLiteral("FN42");
    UserFiles::save(config);
    QVERIFY(QFileInfo::exists(UserFiles::settingsPath() + QStringLiteral(".bak")));
    QFile ini(UserFiles::settingsPath());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(ini.readAll());
    QVERIFY(text.startsWith(QStringLiteral("; Meridian settings")));
    QVERIFY(text.contains(QStringLiteral("FN42")));
    QVERIFY(!text.contains(QStringLiteral("\npassword="), Qt::CaseInsensitive));
}

QTEST_GUILESS_MAIN(TestFinder)
#include "tst_finder.moc"
