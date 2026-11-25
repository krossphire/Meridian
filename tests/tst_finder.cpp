/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Finder.h"
#include "rotor/UserFiles.h"

#include <QCoreApplication>
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

#ifdef Q_OS_UNIX
#include <pty.h>
#include <unistd.h>
#endif

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
    void foundBaudStays4800Or9600();
#ifdef Q_OS_UNIX
    void serialScanStopsAtFirstHit();
    void findFiftyTimesHitsTheSixthPort();
#endif
    void findScanFindsTcp();
    void settingsFileBesideExe();
    void settingsRoundTrip();
    void missingIniHasNoEndpoint();
    void nativeSettingsAreNotImported();
    void windowsZipHasNoIni();
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
    QVERIFY(ports.indexOf(6555) < ports.indexOf(6665));
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
    QVERIFY(found->label.contains(QStringLiteral("RT-21 4.13.2")));
    QCOMPARE(found->firmware, QStringLiteral("RT-21 4.13.2"));
    QVERIFY(rt21Firmware(QByteArray("1\x01RT-21 4.13.2")).contains(QStringLiteral("RT-21")));
    QVERIFY(rt21Firmware(QByteArray("DCU-1")).isEmpty());
    QCOMPARE(closeNote(QStringLiteral("peer closed"), QStringLiteral("Unknown error")),
             QStringLiteral("close peer closed"));
    QCOMPARE(closeNote(QStringLiteral("peer closed"), QStringLiteral("Connection refused")),
             QStringLiteral("close peer closed error=Connection refused"));
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
    QElapsedTimer timer;
    timer.start();
    const QList<FoundRotor> found = Rt21Finder::scanHosts(
        {QStringLiteral("127.0.0.1"), QStringLiteral("192.0.2.1")}, {int(server.port()), 9}, 3000);
    const qint64 elapsed = timer.elapsed();
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
    QCOMPARE(tcpPortAfterFind(6555, serial), 6555);
    QCOMPARE(tcpPortAfterFind(6555, otherPort), 6665);
    QCOMPARE(tcpPortAfterFind(4001, serial), 4001);
    RotorEndpoint blank;
    QVERIFY(!findReplacesSavedEndpoint(blank, false, serial));
}

void TestFinder::foundBaudStays4800Or9600()
{
    QCOMPARE(baudKeptAfterFind(4800, 4800), 4800);
    QCOMPARE(baudKeptAfterFind(4800, 9600), 9600);
    QCOMPARE(baudKeptAfterFind(9600, 4800), 4800);
    QCOMPARE(baudKeptAfterFind(9600, 19200), 9600);
    QCOMPARE(baudKeptAfterFind(4800, 19200), 4800);
    QVERIFY(baudKeptAfterFind(4800, 19200) != 19200);
    QCOMPARE(plainLinkFailure(QStringLiteral("Unknown error")), QStringLiteral("The rotor closed the connection."));
    QCOMPARE(plainLinkFailure(QStringLiteral("Connection timed out")), QStringLiteral("Connection timed out"));
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
    const QList<FoundRotor> found = Rt21Finder::scanSerialPorts(1000, {}, {}, {primary, extra});
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.at(0).endpoint.serialPort, primary);
    QCOMPARE(second.sim()->trace().size(), 0);
}

void TestFinder::findFiftyTimesHitsTheSixthPort()
{
    MockRt21Server server;
    server.sim()->setHeading(35.1);
    const QString live = server.listenPty();
    if (live.isEmpty())
        QSKIP("pty was not available");
    struct Silent {
        int master = -1;
        QString name;
    };
    QList<Silent> quiet;
    for (int i = 0; i < 5; ++i) {
        int master = -1;
        int slave = -1;
        char name[256] = {};
        if (openpty(&master, &slave, name, nullptr, nullptr) != 0) {
            for (const Silent &held : quiet)
                ::close(held.master);
            QSKIP("pty was not available");
        }
        ::close(slave);
        quiet.append(Silent{master, QString::fromLocal8Bit(name)});
    }
    QList<std::pair<QString, QString>> ports;
    for (const Silent &held : quiet)
        ports.append({held.name, QStringLiteral("serial")});
    ports.append({live, QStringLiteral("USB")});
    for (int pass = 0; pass < 50; ++pass) {
        FindScan scan;
        QEventLoop loop;
        int hits = 0;
        FoundRotor hit;
        QObject::connect(&scan, &FindScan::hitFound, &loop, [&](const FoundRotor &found) {
            ++hits;
            hit = found;
        });
        QObject::connect(&scan, &FindScan::settled, &loop, &QEventLoop::quit);
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        FindScan::Request request;
        request.serial = ports;
        request.doSerial = true;
        request.doNetwork = false;
        request.bauds = {4800};
        request.serialTimeoutMs = 120;
        scan.start(request);
        loop.exec();
        QCOMPARE(hits, 1);
        QCOMPARE(hit.endpoint.serialPort, live);
        QCOMPARE(hit.endpoint.baud, 4800);
        QVERIFY(std::abs(hit.heading - 35.1) < 0.2);
        QVERIFY(!scan.running());
    }
    for (const Silent &held : quiet)
        ::close(held.master);
}
#endif

void TestFinder::findScanFindsTcp()
{
    MockRt21Server server;
    server.sim()->setHeading(12);
    QVERIFY(server.listenTcp(0));
    FindScan scan;
    QEventLoop loop;
    int hits = 0;
    FoundRotor hit;
    QObject::connect(&scan, &FindScan::hitFound, &loop, [&](const FoundRotor &found) {
        ++hits;
        hit = found;
    });
    QObject::connect(&scan, &FindScan::settled, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    FindScan::Request request;
    request.doSerial = false;
    request.doNetwork = true;
    request.hosts = {QStringLiteral("127.0.0.1")};
    request.ports = {int(server.port())};
    request.connectMs = 1000;
    scan.start(request);
    loop.exec();
    QCOMPARE(hits, 1);
    QCOMPARE(hit.endpoint.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(hit.endpoint.tcpPort, int(server.port()));
    QVERIFY(hit.label.contains(QStringLiteral("4.13.2")));
}

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
    config.rotors[0].tcpPort = 4001;
    UserFiles::save(config);
    QCOMPARE(UserFiles::load().rotors.at(0).tcpPort, 4001);
    config.rotors[0].tcpPort = 6555;
    config.showSun = false;
    UserFiles::save(config);
    QVERIFY(QFileInfo::exists(UserFiles::settingsPath()));
    QVERIFY(!QFileInfo::exists(UserFiles::settingsPath() + QStringLiteral(".tmp")));

    const AppConfig loaded = UserFiles::load();
    QCOMPARE(loaded.homeText, QStringLiteral("EM26ul"));
    QCOMPARE(loaded.rotors.at(0).host, QStringLiteral("10.0.0.8"));
    QCOMPARE(loaded.rotors.at(0).tcpPort, 6555);
    QFile migrated(UserFiles::settingsPath());
    QVERIFY(migrated.open(QIODevice::ReadOnly));
    const QString migratedText = QString::fromUtf8(migrated.readAll());
    migrated.close();
    QVERIFY(migratedText.contains(QStringLiteral("6555")));
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

void TestFinder::missingIniHasNoEndpoint()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.filePath(QStringLiteral("absent.ini"));
    QVERIFY(!QFileInfo::exists(missing));
    QSettings settings(missing, QSettings::IniFormat);
    const AppConfig loaded = AppConfig::load(settings);
    QCOMPARE(loaded.rotors.size(), 1);
    QVERIFY(loaded.rotors.at(0).host.isEmpty());
    QVERIFY(loaded.rotors.at(0).serialPort.isEmpty());
    QCOMPARE(loaded.rotors.at(0).tcpPort, 0);

    UserFiles::setRoot(dir.filePath(QStringLiteral("fresh")));
    QVERIFY(!QFileInfo::exists(UserFiles::settingsPath()));
    const AppConfig fromDisk = UserFiles::load();
    QCOMPARE(fromDisk.rotors.size(), 1);
    QVERIFY(fromDisk.rotors.at(0).host.isEmpty());
    QVERIFY(fromDisk.rotors.at(0).serialPort.isEmpty());
    QCOMPARE(fromDisk.rotors.at(0).tcpPort, 0);
    QCOMPARE(fromDisk.homeText, QStringLiteral("EM26"));
    QVERIFY(!QFileInfo::exists(UserFiles::settingsPath()));
}

void TestFinder::nativeSettingsAreNotImported()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("K5PO"));
    QCoreApplication::setApplicationName(QStringLiteral("Meridian"));
    QSettings native(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("K5PO"),
                     QStringLiteral("Meridian"));
    native.setValue(QStringLiteral("rotors/1/host"), QStringLiteral("192.168.1.106"));
    native.setValue(QStringLiteral("rotors/1/tcpPort"), 6555);
    native.setValue(QStringLiteral("rotors/1/transport"), QStringLiteral("tcp"));
    native.setValue(QStringLiteral("rotors/1/serialPort"), QStringLiteral("COM4"));
    native.sync();

    UserFiles::setRoot(dir.path());
    const AppConfig loaded = UserFiles::load();
    QVERIFY(loaded.rotors.at(0).host.isEmpty());
    QVERIFY(loaded.rotors.at(0).serialPort.isEmpty());
    QCOMPARE(loaded.rotors.at(0).tcpPort, 0);
    QCOMPARE(loaded.homeText, QStringLiteral("EM26"));
    QVERIFY(!QFileInfo::exists(UserFiles::settingsPath()));
    if (QFileInfo::exists(UserFiles::settingsPath())) {
        QFile ini(UserFiles::settingsPath());
        QVERIFY(ini.open(QIODevice::ReadOnly));
        QVERIFY(!QString::fromUtf8(ini.readAll()).contains(QStringLiteral("192.168.1.106")));
    }
    native.remove(QStringLiteral("rotors/1/host"));
    native.remove(QStringLiteral("rotors/1/tcpPort"));
    native.remove(QStringLiteral("rotors/1/transport"));
    native.remove(QStringLiteral("rotors/1/serialPort"));
    native.sync();
}

void TestFinder::windowsZipHasNoIni()
{
    const QDir root(QStringLiteral(MERIDIAN_SOURCE_DIR));
    const QStringList zips = root.entryList({QStringLiteral("Meridian-*-win64.zip")}, QDir::Files);
    QVERIFY2(!zips.isEmpty(), "no Meridian win64 zip next to the sources");
    for (const QString &name : zips) {
        QFile zip(root.filePath(name));
        QVERIFY2(zip.open(QIODevice::ReadOnly), qPrintable(name));
        const QByteArray data = zip.readAll();
        int at = 0;
        int entries = 0;
        while (true) {
            at = data.indexOf(QByteArray("PK\x01\x02", 4), at);
            if (at < 0 || at + 46 > data.size())
                break;
            const int nameLen = quint8(data.at(at + 28)) | (quint8(data.at(at + 29)) << 8);
            QVERIFY2(at + 46 + nameLen <= data.size(), qPrintable(name));
            const QString entry = QString::fromUtf8(data.mid(at + 46, nameLen));
            QVERIFY2(!entry.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive),
                     qPrintable(name + QStringLiteral(" contains ") + entry));
            ++entries;
            at += 46 + nameLen;
        }
        QVERIFY2(entries > 0, qPrintable(name + QStringLiteral(" had no zip entries")));
    }
}

QTEST_GUILESS_MAIN(TestFinder)
#include "tst_finder.moc"
