/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "StatusTone.h"
#include "rotor/TrafficLog.h"
#include "rotor/UserFiles.h"

#include <QFile>
#include <QSettings>
#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Driver.h"
#include "rotor/Rt21Protocol.h"
#include "rotor/Rt21Finder.h"
#include "rotor/Rt21Setup.h"

#include <QSignalSpy>
#include <QTest>

class TestSetup : public QObject
{
    Q_OBJECT
private slots:
    void commandsMatchTheUtility();
    void adviceStaysOffTheConnectionFields();
    void secretsStayOutOfTheLog();
    void moduleLinesSplitOnCr();
    void mockNetworkRead();
    void networkWriteNeedsAReadThenReadsBack();
    void screenSaverIsOnlyInTheDisplayGroup();
    void firmwareGate();
    void serialPortsStayOnTheSavedName();
    void silentPortSkipsSetupReads();
    void silentModuleSkipsQueries();
    void brightnessWriteReadsBack();
    void statusToneStaysOffThePrimaryInk();
};

void TestSetup::commandsMatchTheUtility()
{
    QCOMPARE(Rt21::rotorTypeName(0), QStringLiteral("Pot"));
    QCOMPARE(Rt21::rotorTypeName(5), QStringLiteral("Orion"));
    QCOMPARE(Rt21::rotorTypeName(12), QStringLiteral("Sub-Lunar"));
    QCOMPARE(Rt21::modeName(2), QStringLiteral("Master counter-rotate"));
    QCOMPARE(Rt21::securityName(1), QStringLiteral("Admin"));
    QCOMPARE(Rt21::wifiSecurityName(6), QStringLiteral("WPA2-PSK"));
    QCOMPARE(Rt21::wifiSecurityName(0), QStringLiteral("None"));
    QVERIFY(Rt21::addressModeText(QStringLiteral("0")).contains(QStringLiteral("DHCP")));
    QVERIFY(Rt21::addressModeText(QStringLiteral("1")).contains(QStringLiteral("Static")));
    QVERIFY(!Rt21::setupWritesCompiledIn());

    QList<Rt21::PlannedWrite> plan;
    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("brightness"), QStringLiteral("8"));
    QVERIFY(Rt21::planGroup(QStringLiteral("display"), fields, &plan).isEmpty());
    QCOMPARE(plan.size(), 1);
    QCOMPARE(plan.first().command, QByteArray("WO08;"));
    QCOMPARE(plan.first().readBack, QByteArray("RO;"));
    QCOMPARE(plan.first().expectValue, QStringLiteral("8"));

    fields.insert(QStringLiteral("dhcp"), QStringLiteral("0"));
    fields.insert(QStringLiteral("aip"), QStringLiteral("192.168.1.20"));
    fields.insert(QStringLiteral("anmask"), QStringLiteral("255.255.255.0"));
    fields.insert(QStringLiteral("agw"), QStringLiteral("192.168.1.1"));
    QVERIFY(Rt21::planGroup(QStringLiteral("network-address"), fields, &plan).isEmpty());
    QCOMPARE(plan.first().command, QByteArray("AT*DHCP=0\r"));
    QVERIFY(!Rt21::planGroup(QStringLiteral("factory"), fields, &plan).isEmpty());
    QVERIFY(!Rt21::planGroup(QStringLiteral("tftp"), fields, &plan).isEmpty());

    const auto sequence = Rt21::networkReadSequence();
    QCOMPARE(sequence.first().command, QByteArray("AT*HWTYPE?\n"));
    QCOMPARE(sequence.last().command, QByteArray("AT*LINK?\n"));
    bool sawUser = false;
    int gwAt = -1;
    for (int i = 0; i < sequence.size(); ++i) {
        if (sequence.at(i).key == QLatin1String("gw"))
            gwAt = i;
    }
    QVERIFY(gwAt >= 0 && gwAt + 1 < sequence.size());
    QCOMPARE(sequence.at(gwAt + 1).command, QByteArray("AT*AGW?\n"));
    QCOMPARE(sequence.at(gwAt + 1).key, QStringLiteral("agw"));
    for (const auto &query : sequence) {
        QVERIFY(!query.command.contains(';'));
        QVERIFY(!query.command.contains("T11"));
        QVERIFY(!query.command.contains("T01"));
        QVERIFY(query.command.endsWith('\n'));
        if (query.key == QLatin1String("user")) {
            sawUser = true;
            QCOMPARE(query.token, QStringLiteral("HTTP_USER="));
        }
    }
    QVERIFY(sawUser);
}

void TestSetup::adviceStaysOffTheConnectionFields()
{
    QCOMPARE(Rt21::recommendTcpPort({}), 6555);
    QCOMPARE(Rt21::recommendTcpPort({6555, 6556}), 6557);
    QCOMPARE(Rt21::gatewayFromProcRoute(QStringLiteral("Iface\tDestination\tGateway\tFlags\neth0\t00000000\t0101A8C0\t0003\n")),
             QStringLiteral("192.168.1.1"));
    const Rt21::StaticAdvice advice = Rt21::adviseStaticAddress(QStringLiteral("192.168.1.23"), 24, QStringLiteral("192.168.1.1"),
                                                                {QStringLiteral("192.168.1.20")});
    QCOMPARE(advice.address, QStringLiteral("192.168.1.21"));
    QCOMPARE(advice.mask, QStringLiteral("255.255.255.0"));
    QVERIFY(advice.explanation.contains(QStringLiteral("192.168.1.21")));
    QVERIFY(advice.explanation.contains(QStringLiteral(".100")));

    const QString text = Rt21::formatSetupBackup({{QStringLiteral("D"), QStringLiteral("180")}},
                                                  {{QStringLiteral("key"), QStringLiteral("secret-phrase")}});
    QVERIFY(text.contains(QStringLiteral("Offset (D) = 180 degrees")));
    QVERIFY(text.contains(QStringLiteral("key = secret-phrase")));
    QVERIFY(text.contains(QStringLiteral("DNS is not")));
    QCOMPARE(Rt21::formatEeLine(QStringLiteral("J"), QStringLiteral("10")),
             QStringLiteral("Rotor type (J) = 10 (Yaesu G-2800)"));
    QVERIFY(Rt21::formatEeLine(QStringLiteral("T"), QStringLiteral("2576")).contains(QStringLiteral("auto dim")));
    QVERIFY(Rt21::formatEeLine(QStringLiteral("R"), QStringLiteral("361")).contains(QStringLiteral("unused")));
    QVERIFY(Rt21::formatEeLine(QStringLiteral("K"), QStringLiteral("3960")).contains(QStringLiteral("3960")));
    QVERIFY(Rt21::formatEeLine(QStringLiteral("F"), QStringLiteral("125")).contains(QStringLiteral("125")));
    QVERIFY(Rt21::formatEeLine(QStringLiteral("W"), QStringLiteral("0")).contains(QStringLiteral("Off")));
}

void TestSetup::secretsStayOutOfTheLog()
{
    const QByteArray tx = Rt21::redactForLog(QByteArray("AT*WIFI_KEY=example-wifi-key\r"));
    QVERIFY(!tx.contains("example-wifi-key"));
    QVERIFY(tx.contains("AT*WIFI_KEY=****"));
    const QByteArray rx = Rt21::redactForLog(QByteArray("PASS=webpass\r\n"));
    QVERIFY(!rx.contains("webpass"));
    QVERIFY(rx.contains("PASS=****"));
}

void TestSetup::moduleLinesSplitOnCr()
{
    Rt21::Framer framer;
    framer.setLineMode(true);
    framer.append(QByteArray("DHCP=0\r\nKEY=secret\r\n"));
    const auto frames = framer.take();
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0), QByteArray("DHCP=0"));
    QCOMPARE(frames.at(1), QByteArray("KEY=secret"));
}

void TestSetup::mockNetworkRead()
{
    MockRt21Server server;
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Serial;
    endpoint.serialPort = pty;
    endpoint.baud = 4800;
    driver.connectEndpoint(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);

    QMap<QString, QString> got;
    connect(&driver, &Rt21Driver::networkField, &driver, [&](const QString &key, const QString &value) { got.insert(key, value); });
    QSignalSpy done(&driver, &Rt21Driver::networkFinished);
    QSignalSpy status(&driver, &Rt21Driver::networkStatus);
    QSettings cycleSettings(UserFiles::settingsPath(), QSettings::IniFormat);
    cycleSettings.remove(QStringLiteral("net_cycle_mode"));
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
    QCOMPARE(status.at(0).at(0).toString(), QStringLiteral("Reading network (try 1 of 12)…"));
    QCOMPARE(got.value(QStringLiteral("id")), QStringLiteral("RT-21"));
    QCOMPARE(got.value(QStringLiteral("dhcp")), QStringLiteral("0"));
    QCOMPARE(got.value(QStringLiteral("ip")), QStringLiteral("192.168.1.106"));
    QCOMPARE(got.value(QStringLiteral("aip")), QStringLiteral("192.168.1.106"));
    QCOMPARE(got.value(QStringLiteral("nmask")), QStringLiteral("255.255.255.0"));
    QCOMPARE(got.value(QStringLiteral("gw")), QStringLiteral("192.168.2.1"));
    QCOMPARE(got.value(QStringLiteral("agw")), QStringLiteral("192.168.2.1"));
    QCOMPARE(got.value(QStringLiteral("http")), QStringLiteral("801"));
    QCOMPARE(got.value(QStringLiteral("udp")), QStringLiteral("23073"));
    QCOMPARE(got.value(QStringLiteral("tftp")), QStringLiteral("69"));
    QCOMPARE(got.value(QStringLiteral("serialPort")), QStringLiteral("6555"));
    QCOMPARE(got.value(QStringLiteral("hwtype")), QStringLiteral("2"));
    QCOMPARE(got.value(QStringLiteral("link")), QStringLiteral("1"));
    QVERIFY(got.contains(QStringLiteral("user")));
    QCOMPARE(got.value(QStringLiteral("user")), QString());
    QCOMPARE(got.value(QStringLiteral("pass")), QString());
    QCOMPARE(done.at(0).at(0).toBool(), true);
    QCOMPARE(done.at(0).at(1).toString(), QStringLiteral("Network module answered. Nothing was written."));
    QCOMPARE(driver.link(), Rt21Driver::Link::On);
    bool sawEnter = false;
    bool sawEcho = false;
    bool sawEsc = false;
    bool sawQuery = false;
    for (const QByteArray &cmd : server.sim()->trace()) {
        if (cmd == "AT")
            sawEnter = true;
        if (cmd == "AT*SERIAL_ECHO=0")
            sawEcho = true;
        if (cmd == "ESC")
            sawEsc = true;
        if (cmd.startsWith("AT*")) {
            sawQuery = true;
            QVERIFY(!cmd.contains(';'));
        }
        QVERIFY(cmd != "T01");
        QVERIFY(!cmd.startsWith("WW"));
        QVERIFY(!cmd.startsWith("W0"));
    }
    const QList<QByteArray> trace = server.sim()->trace();
    const int enter = trace.indexOf(QByteArray("AT"));
    QVERIFY(enter > 0);
    QCOMPARE(trace.at(enter - 1), QByteArray("T11"));
    QVERIFY(sawEnter);
    QVERIFY(sawEcho);
    QVERIFY(sawEsc);
    QVERIFY(sawQuery);
    QFile logFile(TrafficLog::instance().filePath());
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    const QString log = QString::fromUtf8(logFile.readAll());
    QVERIFY(log.contains(QStringLiteral("network lead-in ESC ESC")));
    QVERIFY(log.contains(QStringLiteral("network lead-in ;")));
    QVERIFY(log.contains(QStringLiteral("network variant F T11;")));
    QVERIFY(!log.contains(QStringLiteral("network bridge write")));
    const auto phaseMs = [&](const QString &label, bool *ok) {
        const QString key = QStringLiteral("network ") + label + QStringLiteral(" ");
        const int at = log.lastIndexOf(key);
        if (at < 0) {
            *ok = false;
            return -1;
        }
        return log.mid(at + key.size()).section(QLatin1Char(' '), 0, 0).toInt(ok);
    };
    bool timingOk = true;
    const int uartToDtr = phaseMs(QStringLiteral("uart2->DTR low"), &timingOk);
    const int dtrLow = phaseMs(QStringLiteral("DTR-low"), &timingOk);
    const int dtrToEcho = phaseMs(QStringLiteral("DTR high->ECHO"), &timingOk);
    const int uartToEcho = phaseMs(QStringLiteral("uart2->ECHO"), &timingOk);
    const int preAtQuiet = phaseMs(QStringLiteral("pre-AT quiet"), &timingOk);
    QVERIFY(timingOk);
    QVERIFY(uartToDtr >= 490 && uartToDtr <= 560);
    QVERIFY(dtrLow >= 0 && dtrLow <= 150);
    QVERIFY(dtrToEcho >= 0 && dtrToEcho <= 15);
    QVERIFY(uartToEcho >= 500 && uartToEcho <= 720);
    QVERIFY(log.contains(QStringLiteral("variant D")));
    QVERIFY(log.contains(QStringLiteral("network echo rx ERROR 1")));
    QVERIFY(log.contains(QStringLiteral("network echo send 2")));
    QVERIFY(log.contains(QStringLiteral("network query rx AGW=")));
    QVERIFY(preAtQuiet >= 650 && preAtQuiet <= 900);
}

void TestSetup::networkWriteNeedsAReadThenReadsBack()
{
    MockRt21Server server;
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Serial;
    endpoint.serialPort = pty;
    endpoint.baud = 4800;
    driver.connectEndpoint(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);

    QSignalSpy wrote(&driver, &Rt21Driver::writeFinished);
    driver.writeNetworkSetting(QStringLiteral("dhcp"), QStringLiteral("1"), QStringLiteral("DHCP"));
    QTRY_COMPARE_WITH_TIMEOUT(wrote.count(), 1, 1000);
    QVERIFY(!wrote.at(0).at(0).toBool());
    QVERIFY(wrote.at(0).at(1).toString().contains(QStringLiteral("Only the gateway")));

    driver.writeNetworkSetting(QStringLiteral("agw"), QStringLiteral("192.168.1.1"), QStringLiteral("Gateway"));
    QTRY_COMPARE_WITH_TIMEOUT(wrote.count(), 2, 1000);
    QVERIFY(!wrote.at(1).at(0).toBool());
    QVERIFY(wrote.at(1).at(1).toString().contains(QStringLiteral("Read the network module")));

    QSignalSpy read(&driver, &Rt21Driver::networkFinished);
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(read.count(), 1, 15000);
    QVERIFY(read.at(0).at(0).toBool());
    QVERIFY(driver.networkReadOk());

    driver.writeNetworkSetting(QStringLiteral("agw"), QStringLiteral("192.168.1"), QStringLiteral("Gateway"));
    QTRY_COMPARE_WITH_TIMEOUT(wrote.count(), 3, 1000);
    QVERIFY(!wrote.at(2).at(0).toBool());
    QVERIFY(wrote.at(2).at(1).toString().contains(QStringLiteral("IPv4")));

    QSignalSpy status(&driver, &Rt21Driver::networkStatus);
    driver.writeNetworkSetting(QStringLiteral("agw"), QStringLiteral("192.168.1.1"), QStringLiteral("Gateway"));
    QTRY_COMPARE_WITH_TIMEOUT(wrote.count(), 4, 40000);
    QVERIFY(wrote.at(3).at(0).toBool());
    QCOMPARE(wrote.at(3).at(1).toString(), QStringLiteral("Saved Gateway"));
    QCOMPARE(status.at(0).at(0).toString(), QStringLiteral("Writing Gateway (try 1 of 12)…"));
    const QList<QByteArray> trace = server.sim()->trace();
    int setAt = -1;
    for (int i = 0; i < trace.size(); ++i) {
        QVERIFY(!trace.at(i).contains("AT*RESET"));
        QVERIFY(!trace.at(i).contains(';') || trace.at(i) == QByteArray("ESC") || !trace.at(i).startsWith("AT"));
        if (trace.at(i) == QByteArray("AT*AGW=192.168.1.1"))
            setAt = i;
    }
    QVERIFY(setAt >= 0);
    QCOMPARE(trace.at(setAt + 1), QByteArray("AT*AGW?"));
    QCOMPARE(trace.at(setAt + 2), QByteArray("AT*GW?"));
}

void TestSetup::screenSaverIsOnlyInTheDisplayGroup()
{
    QCOMPARE(Rt21::recommendedScreenSaverMinutes(), 0);
    QVERIFY(!Rt21::screenSaverMinuteAllowed(90));
    QVERIFY(Rt21::screenSaverRecommendation().contains(QStringLiteral("Off")));
    QVERIFY(Rt21::screenSaverUnsafeNote().contains(QStringLiteral("4.14.g1")));
    QVERIFY(Rt21::screenSaverUnsafeNote().contains(QStringLiteral("Set it to Off")));

    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("screenSaver"), QStringLiteral("1"));
    fields.insert(QStringLiteral("brightness"), QStringLiteral("8"));
    QList<Rt21::PlannedWrite> plan;
    QVERIFY(Rt21::planGroup(QStringLiteral("display"), fields, &plan).isEmpty());
    QByteArray joined;
    for (const auto &step : plan)
        joined += step.command;
    QVERIFY(joined.contains("WW01;"));
    QVERIFY(joined.contains("WO08;"));

    fields.insert(QStringLiteral("screenSaver"), QStringLiteral("90"));
    QVERIFY(Rt21::planGroup(QStringLiteral("display"), fields, &plan).contains(QStringLiteral("90")));

    const QStringList others = {QStringLiteral("motion"), QStringLiteral("stops"), QStringLiteral("calibration"),
                                QStringLiteral("advanced"), QStringLiteral("rotor-type")};
    QMap<QString, QString> filled;
    filled.insert(QStringLiteral("screenSaver"), QStringLiteral("1"));
    filled.insert(QStringLiteral("minSpeed"), QStringLiteral("3"));
    filled.insert(QStringLiteral("maxSpeed"), QStringLiteral("10"));
    filled.insert(QStringLiteral("ramp"), QStringLiteral("3"));
    filled.insert(QStringLiteral("delays"), QStringLiteral("3"));
    filled.insert(QStringLiteral("counterDelay"), QStringLiteral("10"));
    filled.insert(QStringLiteral("ccwLimit"), QStringLiteral("180"));
    filled.insert(QStringLiteral("cwLimit"), QStringLiteral("180"));
    filled.insert(QStringLiteral("calRange"), QStringLiteral("360"));
    filled.insert(QStringLiteral("overtravel"), QStringLiteral("90"));
    filled.insert(QStringLiteral("ccwCal"), QStringLiteral("0"));
    filled.insert(QStringLiteral("cwCal"), QStringLiteral("950"));
    filled.insert(QStringLiteral("offset"), QStringLiteral("180"));
    filled.insert(QStringLiteral("divider"), QStringLiteral("3960"));
    filled.insert(QStringLiteral("alt1"), QStringLiteral("361"));
    filled.insert(QStringLiteral("alt2"), QStringLiteral("361"));
    filled.insert(QStringLiteral("advanced"), QStringLiteral("2576"));
    filled.insert(QStringLiteral("option"), QStringLiteral("10"));
    filled.insert(QStringLiteral("mode"), QStringLiteral("0"));
    filled.insert(QStringLiteral("security"), QStringLiteral("1"));
    for (const QString &group : others) {
        QVERIFY2(Rt21::planGroup(group, filled, &plan).isEmpty(), qPrintable(group));
        for (const auto &step : plan) {
            QVERIFY2(!step.command.contains("WW"), qPrintable(group + QLatin1Char(' ') + QString::fromLatin1(step.command)));
            QVERIFY2(!step.readBack.startsWith("RW"), qPrintable(group));
        }
    }
}

void TestSetup::firmwareGate()
{
    QCOMPARE(Rt21::classifyFirmware(QStringLiteral("RT-21 4.13.2")), Rt21::FirmwareClass::Tested);
    QCOMPARE(Rt21::classifyFirmware(QStringLiteral("RT-21 4.14.g1")), Rt21::FirmwareClass::PreRelease);
    QCOMPARE(Rt21::classifyFirmware(QStringLiteral("RT-21 3.08")), Rt21::FirmwareClass::Untested);
    QVERIFY(Rt21::firmwareNotice(QStringLiteral("RT-21 4.13.2")).isEmpty());
    QVERIFY(Rt21::firmwareNotice(QStringLiteral("RT-21 4.14.g1")).contains(QStringLiteral("pre-release")));
    QVERIFY(Rt21::firmwareWritesAllowed(QStringLiteral("RT-21 4.13.2")));
    QVERIFY(!Rt21::firmwareWritesAllowed(QStringLiteral("RT-21 4.14.g1")));
    QVERIFY(!Rt21::firmwareWritesAllowed(QStringLiteral("RT-21 4.14")));
    QVERIFY(!Rt21::setupWritesCompiledIn());
    QVERIFY(Rt21::gatewaySubnetNote(QStringLiteral("192.168.1.106"), QStringLiteral("255.255.255.0"),
                                    QStringLiteral("192.168.2.1"))
                .contains(QStringLiteral("not on the same subnet")));
    QVERIFY(Rt21::gatewaySubnetNote(QStringLiteral("192.168.1.106"), QStringLiteral("255.255.255.0"),
                                    QStringLiteral("192.168.1.1"))
                .isEmpty());
    QCOMPARE(serialProbeBauds().first(), 4800);
    QVERIFY(serialProbeBauds().contains(19200));
}

void TestSetup::serialPortsStayOnTheSavedName()
{
    const QList<SerialPortChoice> messy = {
        {QStringLiteral("COM10"), QStringLiteral("COM10")},
        {QStringLiteral("COM2"), QStringLiteral("COM2")},
        {QStringLiteral("COM1"), QStringLiteral("COM1 — Edgeport")},
        {QStringLiteral("COM109"), QStringLiteral("COM109")},
        {QStringLiteral("COM6"), QStringLiteral("COM6 — Edgeport")},
        {QStringLiteral("COM9"), QStringLiteral("COM9")},
        {QStringLiteral("COM110"), QStringLiteral("COM110")},
    };
    const QList<SerialPortChoice> arranged = arrangeSerialPorts(messy, QStringLiteral("COM6"));
    QStringList names;
    for (const SerialPortChoice &choice : arranged)
        names.append(choice.port);
    QCOMPARE(names, QStringList({QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM6"),
                                 QStringLiteral("COM9"), QStringLiteral("COM10"), QStringLiteral("COM109"),
                                 QStringLiteral("COM110")}));
    const QList<SerialPortChoice> missing = arrangeSerialPorts(messy, QStringLiteral("COM4"));
    QCOMPARE(missing.last().port, QStringLiteral("COM4"));
    QVERIFY(missing.last().label.contains(QStringLiteral("(missing)")));
    const QList<SerialPortChoice> mac = {
        {QStringLiteral("cu.usbserial-110"), QStringLiteral("cu.usbserial-110")},
        {QStringLiteral("cu.usbserial-2"), QStringLiteral("cu.usbserial-2")},
        {QStringLiteral("cu.usbserial-10"), QStringLiteral("cu.usbserial-10")},
    };
    QStringList macNames;
    for (const SerialPortChoice &choice : arrangeSerialPorts(mac, QString()))
        macNames.append(choice.port);
    QCOMPARE(macNames, QStringList({QStringLiteral("cu.usbserial-2"), QStringLiteral("cu.usbserial-10"),
                                    QStringLiteral("cu.usbserial-110")}));
}

void TestSetup::silentPortSkipsSetupReads()
{
    MockRt21Server server;
    server.sim()->setIgnoreQueries(true);
    QVERIFY(server.listenTcp(0));
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("127.0.0.1");
    endpoint.tcpPort = int(server.port());
    driver.connectEndpoint(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);
    QSignalSpy done(&driver, &Rt21Driver::configFinished);
    driver.readConfiguration();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 4000);
    QVERIFY(!done.at(0).at(0).toBool());
    QVERIFY(done.at(0).at(1).toString().contains(QStringLiteral("No reply from RT-21 on")));
    for (const QByteArray &cmd : server.sim()->trace()) {
        QVERIFY(!cmd.contains("RA1"));
        QVERIFY(!cmd.contains("AT*"));
    }
}

void TestSetup::silentModuleSkipsQueries()
{
    MockRt21Server server;
    server.sim()->setIgnoreModule(true);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Serial;
    endpoint.serialPort = pty;
    endpoint.baud = 4800;
    driver.connectEndpoint(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);
    QSignalSpy done(&driver, &Rt21Driver::networkFinished);
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 180000);
    QVERIFY(!done.at(0).at(0).toBool());
    QCOMPARE(done.at(0).at(1).toString(), QStringLiteral("Network module didn't answer after 12 tries"));
    QCOMPARE(driver.link(), Rt21Driver::Link::On);
    int queries = 0;
    int exits = 0;
    int enters = 0;
    const QList<QByteArray> trace = server.sim()->trace();
    for (int i = 0; i < trace.size(); ++i) {
        const QByteArray &cmd = trace.at(i);
        if (cmd.startsWith("AT*"))
            ++queries;
        if (cmd == "ESC")
            ++exits;
        if (cmd == "AT") {
            ++enters;
            QVERIFY(i > 0);
            QCOMPARE(trace.at(i - 1), QByteArray("T11"));
        }
        QVERIFY(cmd != "T01");
        QVERIFY(!cmd.startsWith("WW"));
        QVERIFY(!cmd.startsWith("W0"));
    }
    QCOMPARE(queries, 0);
    // Two lead-in ESC bytes plus the exit ESC, on each of the 12 tries.
    QCOMPARE(exits, 36);
    QCOMPARE(enters, 12);
}

void TestSetup::brightnessWriteReadsBack()
{
    QCOMPARE(Rt21::registerWriteCommand(QLatin1Char('D'), 180), QByteArray("WD0180;"));
    QCOMPARE(Rt21::registerWriteCommand(QLatin1Char('W'), 1), QByteArray("WW01;"));
    QVERIFY(Rt21::registerWriteCommand(QLatin1Char('0'), 1).isEmpty());
    QCOMPARE(Rt21::brightnessWriteCommand(10), QByteArray("WO010;"));
    QCOMPARE(Rt21::brightnessWriteCommand(1), QByteArray("WO01;"));
    QCOMPARE(Rt21::brightnessReadCommand(), QByteArray("RO;"));
    QVERIFY(!Rt21::brightnessWriteCommand(10).contains('\n'));

    MockRt21Server server;
    server.sim()->setHeading(20);
    QVERIFY(server.listenTcp(0));
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("127.0.0.1");
    endpoint.tcpPort = int(server.port());
    driver.connectEndpoint(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);

    QSignalSpy done(&driver, &Rt21Driver::writeFinished);
    driver.writeBrightness(1);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
    QCOMPARE(done.at(0).at(0).toBool(), true);
    QCOMPARE(done.at(0).at(1).toString(), QStringLiteral("Saved Brightness"));
    QVERIFY(server.sim()->trace().contains(QByteArray("WO01")));
    QVERIFY(server.sim()->trace().contains(QByteArray("RO")));

    server.sim()->setIgnoreQueries(true);
    driver.writeBrightness(2);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 15000);
    QCOMPARE(done.at(1).at(0).toBool(), false);
    QCOMPARE(done.at(1).at(1).toString(), QStringLiteral("Write not confirmed: Brightness"));

    driver.applySetupGroup(QStringLiteral("motion"), {}, QString());
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, 1000);
    QVERIFY(done.at(2).at(1).toString().contains(QStringLiteral("off in this alpha")));
}

void TestSetup::statusToneStaysOffThePrimaryInk()
{
    const QColor ink(244, 246, 244);
    const auto apart = [&](StatusTone tone) {
        const QColor color = statusToneColor(tone);
        QVERIFY(color != ink);
        QVERIFY(color != QColor(232, 236, 234));
        QVERIFY(color != QColor(Qt::white));
    };
    QCOMPARE(statusTone(QStringLiteral("Writing Brightness…")), StatusTone::Busy);
    QCOMPARE(statusTone(QStringLiteral("Saved Brightness")), StatusTone::Success);
    QCOMPARE(statusTone(QStringLiteral("Write not confirmed: Brightness")), StatusTone::Failure);
    QCOMPARE(statusTone(QStringLiteral("Controller settings read. Nothing was written.")), StatusTone::Success);
    QCOMPARE(statusTone(QStringLiteral("RT-21 not connected")), StatusTone::Failure);
    QCOMPARE(statusTone(QStringLiteral("Set up your RT-21 in settings")), StatusTone::Neutral);
    QCOMPARE(statusTone(QStringLiteral("Reading network (try 3 of 12)…")), StatusTone::Busy);
    QCOMPARE(statusTone(QStringLiteral("Writing DHCP (try 1 of 12)…")), StatusTone::Busy);
    QCOMPARE(statusTone(QStringLiteral("Network module answered. Nothing was written.")), StatusTone::Success);
    QCOMPARE(statusTone(QStringLiteral("Network module didn't answer after 12 tries")), StatusTone::Failure);
    QCOMPARE(statusTone(QStringLiteral("Read the network module before writing it.")), StatusTone::Failure);
    QCOMPARE(statusTone(QStringLiteral("The link dropped while talking to the network module.")), StatusTone::Failure);
    apart(StatusTone::Neutral);
    apart(StatusTone::Busy);
    apart(StatusTone::Success);
    apart(StatusTone::Failure);
    QCOMPARE(statusToneColor(StatusTone::Neutral), QColor(142, 160, 153));
}

QTEST_MAIN(TestSetup)
#include "tst_setup.moc"
