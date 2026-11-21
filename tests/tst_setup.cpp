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
#include "SetupPanel.h"
#include "rotor/Rt21Setup.h"

#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <algorithm>

class TestSetup : public QObject
{
    Q_OBJECT
private slots:
    void commandsMatchTheUtility();
    void dhcpZeroIsStaticAcrossChunkedReplies();
    void adviceStaysOffTheConnectionFields();
    void secretsStayOutOfTheLog();
    void moduleLinesSplitOnCr();
    void fragmentedModuleLinesFillEveryField();
    void mockNetworkRead();
    void benchNetworkReadFillsSetupFields();
    void coldBootWakeThenRead();
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
    QCOMPARE(Rt21::addressModeText(QStringLiteral("0")),
             QStringLiteral("Static — the saved address is the one below"));
    QCOMPARE(Rt21::addressModeText(QStringLiteral("1")),
             QStringLiteral("DHCP — the router assigns the address"));
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
    QCOMPARE(plan.first().expectValue, QStringLiteral("0"));
    fields.insert(QStringLiteral("dhcp"), QStringLiteral("1"));
    QVERIFY(Rt21::planGroup(QStringLiteral("network-address"), fields, &plan).isEmpty());
    QCOMPARE(plan.first().command, QByteArray("AT*DHCP=1\r"));
    QCOMPARE(plan.first().expectValue, QStringLiteral("1"));
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

void TestSetup::dhcpZeroIsStaticAcrossChunkedReplies()
{
    // COM6, 2026-11-21. Replies arrive a few bytes at a time, often with a
    // leading LF and a trailing CR or CR LF. DHCP=0 is static; DHCP=1 is DHCP.
    auto dhcpFrom = [](const QList<QByteArray> &chunks) {
        Rt21::Framer framer;
        framer.setLineMode(true);
        Rt21::ModuleLineBuffer lines;
        QString fromFramer;
        QString fromLines;
        for (const QByteArray &chunk : chunks) {
            framer.append(chunk);
            for (const QByteArray &frame : framer.take()) {
                const auto parsed = Rt21::parseModuleLine(frame);
                if (parsed && parsed->key.compare(QLatin1String("DHCP"), Qt::CaseInsensitive) == 0)
                    fromFramer = parsed->value;
            }
            for (const Rt21::ModuleAssignment &row : lines.feed(chunk)) {
                if (row.key.compare(QLatin1String("DHCP"), Qt::CaseInsensitive) == 0)
                    fromLines = row.value;
            }
        }
        return qMakePair(fromFramer, fromLines);
    };

    const QList<QByteArray> staticCr = {QByteArray("\n"), QByteArray("AT*"), QByteArray("DH"),
                                        QByteArray("CP="), QByteArray("0"), QByteArray("\r")};
    const auto staticParsed = dhcpFrom(staticCr);
    QCOMPARE(staticParsed.first, staticParsed.second);
    QCOMPARE(staticParsed.first, QStringLiteral("0"));
    QCOMPARE(Rt21::addressModeText(staticParsed.first),
             QStringLiteral("Static — the saved address is the one below"));

    const QList<QByteArray> staticCrLf = {QByteArray("\nAT*DHCP=0\r\n")};
    const auto staticLine = dhcpFrom(staticCrLf);
    QCOMPARE(staticLine.first, staticLine.second);
    QCOMPARE(staticLine.first, QStringLiteral("0"));

    const QList<QByteArray> dhcpChunks = {QByteArray("\n"), QByteArray("A"), QByteArray("T*D"),
                                          QByteArray("HCP=1\r"), QByteArray("\n")};
    const auto dhcpParsed = dhcpFrom(dhcpChunks);
    QCOMPARE(dhcpParsed.first, dhcpParsed.second);
    QCOMPARE(dhcpParsed.first, QStringLiteral("1"));
    QCOMPARE(Rt21::addressModeText(dhcpParsed.first),
             QStringLiteral("DHCP — the router assigns the address"));

    const QByteArray stream =
        QByteArray("\nAT*HWTYPE=2\r") + "\nAT*DHCP=0\r\n" + "\nAT*IP=192.168.1.106\r" + "\nAT*AIP=192.168.1.106\r\n"
        + "\nAT*NMASK=255.255.255.0\r" + "\nAT*GW=192.168.1.1\r\n" + "\nAT*AGW=192.168.1.1\r" + "\nAT*ID=RT-21\r\n"
        + "\nAT*HTTP=801\r" + "\nAT*UDP=23073\r\n" + "\nAT*TFTP=69\r" + "\nAT*HTTP_USER=\r\n" + "\nAT*HTTP_PASS=\r"
        + "\nAT*NVREG16_0=6665\r\n" + "\nAT*LINK=1\r";
    auto readStream = [&](const QByteArray &bytes) {
        QList<QByteArray> chunks;
        for (int at = 0; at < bytes.size(); at += 3)
            chunks.append(bytes.mid(at, std::min(3, int(bytes.size() - at))));
        Rt21::Framer framer;
        framer.setLineMode(true);
        QMap<QString, QString> fields;
        for (const QByteArray &chunk : chunks) {
            framer.append(chunk);
            for (const QByteArray &frame : framer.take()) {
                if (const auto parsed = Rt21::parseModuleLine(frame))
                    fields.insert(parsed->key, parsed->value);
            }
        }
        return fields;
    };
    const QMap<QString, QString> read = readStream(stream);
    QCOMPARE(read.value(QStringLiteral("HWTYPE")), QStringLiteral("2"));
    QCOMPARE(read.value(QStringLiteral("DHCP")), QStringLiteral("0"));
    QCOMPARE(read.value(QStringLiteral("IP")), QStringLiteral("192.168.1.106"));
    QCOMPARE(read.value(QStringLiteral("AIP")), QStringLiteral("192.168.1.106"));
    QCOMPARE(read.value(QStringLiteral("NMASK")), QStringLiteral("255.255.255.0"));
    QCOMPARE(read.value(QStringLiteral("GW")), QStringLiteral("192.168.1.1"));
    QCOMPARE(read.value(QStringLiteral("AGW")), QStringLiteral("192.168.1.1"));
    QCOMPARE(read.value(QStringLiteral("ID")), QStringLiteral("RT-21"));
    QCOMPARE(read.value(QStringLiteral("HTTP")), QStringLiteral("801"));
    QCOMPARE(read.value(QStringLiteral("UDP")), QStringLiteral("23073"));
    QCOMPARE(read.value(QStringLiteral("TFTP")), QStringLiteral("69"));
    QCOMPARE(read.value(QStringLiteral("HTTP_USER")), QString());
    QCOMPARE(read.value(QStringLiteral("HTTP_PASS")), QString());
    QCOMPARE(read.value(QStringLiteral("NVREG16_0")), QStringLiteral("6665"));
    QCOMPARE(read.value(QStringLiteral("LINK")), QStringLiteral("1"));
    QCOMPARE(Rt21::addressModeText(read.value(QStringLiteral("DHCP"))),
             QStringLiteral("Static — the saved address is the one below"));

    QByteArray flipped = stream;
    QVERIFY(flipped.contains("AT*DHCP=0"));
    flipped.replace("AT*DHCP=0", "AT*DHCP=1");
    const QMap<QString, QString> dhcpOn = readStream(flipped);
    QCOMPARE(dhcpOn.value(QStringLiteral("DHCP")), QStringLiteral("1"));
    QCOMPARE(Rt21::addressModeText(dhcpOn.value(QStringLiteral("DHCP"))),
             QStringLiteral("DHCP — the router assigns the address"));

    QList<Rt21::PlannedWrite> plan;
    QMap<QString, QString> writeFields;
    writeFields.insert(QStringLiteral("dhcp"), read.value(QStringLiteral("DHCP")));
    writeFields.insert(QStringLiteral("aip"), read.value(QStringLiteral("AIP")));
    writeFields.insert(QStringLiteral("anmask"), read.value(QStringLiteral("NMASK")));
    writeFields.insert(QStringLiteral("agw"), read.value(QStringLiteral("AGW")));
    QVERIFY(Rt21::planGroup(QStringLiteral("network-address"), writeFields, &plan).isEmpty());
    QCOMPARE(plan.first().command, QByteArray("AT*DHCP=0\r"));
    QCOMPARE(plan.first().expectValue, QStringLiteral("0"));
    writeFields.insert(QStringLiteral("dhcp"), dhcpOn.value(QStringLiteral("DHCP")));
    QVERIFY(Rt21::planGroup(QStringLiteral("network-address"), writeFields, &plan).isEmpty());
    QCOMPARE(plan.first().command, QByteArray("AT*DHCP=1\r"));
    QCOMPARE(plan.first().expectValue, QStringLiteral("1"));
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

void TestSetup::fragmentedModuleLinesFillEveryField()
{
    // The 19 Nov bench delivered the current IP as 1–3 byte reads, and the
    // other keys as either "*KEY=value" or "AT*KEY=value".
    const QList<QByteArray> ipChunks = {QByteArray("A"), QByteArray("T"), QByteArray("*"), QByteArray("IP="),
                                        QByteArray("19"), QByteArray("2.1"), QByteArray("68"), QByteArray(".1"),
                                        QByteArray(".10"), QByteArray("6\r"), QByteArray("\n")};
    Rt21::ModuleLineBuffer buffer;
    QList<Rt21::ModuleAssignment> ip;
    for (const QByteArray &chunk : ipChunks)
        ip += buffer.feed(chunk);
    QCOMPARE(ip.size(), 1);
    QCOMPARE(ip.at(0).key, QStringLiteral("IP"));
    QCOMPARE(ip.at(0).value, QStringLiteral("192.168.1.106"));

    const QByteArray stream =
        QByteArray("*HWTYPE=2\r\n") + "*DHCP=0\r\n" + "AT*AIP=192.168.1.106\r\n" + "AT*NMASK=255.255.255.0\r\n"
        + "AT*GW=192.168.2.1\r\n" + "AT*AGW=192.168.2.1\r\n" + "AT*ID=RT-21\r\n" + "AT*HTTP=801\r\n"
        + "AT*UDP=23073\r\n" + "AT*TFTP=69\r\n" + "AT*HTTP_USER=\r\n" + "AT*HTTP_PASS=s3cret-not-shown\r\n"
        + "AT*NVREG16_0=6555\r\n" + "AT*LINK=1\r\n";
    QList<Rt21::ModuleAssignment> rows;
    int at = 0;
    while (at < stream.size()) {
        const int n = std::min(3, int(stream.size() - at));
        rows += buffer.feed(stream.mid(at, n));
        at += n;
    }
    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("IP"), QStringLiteral("192.168.1.106"));
    for (const Rt21::ModuleAssignment &row : rows)
        fields.insert(row.key, row.value);
    QCOMPARE(fields.value(QStringLiteral("HWTYPE")), QStringLiteral("2"));
    QCOMPARE(fields.value(QStringLiteral("DHCP")), QStringLiteral("0"));
    QCOMPARE(fields.value(QStringLiteral("IP")), QStringLiteral("192.168.1.106"));
    QCOMPARE(fields.value(QStringLiteral("AIP")), QStringLiteral("192.168.1.106"));
    QCOMPARE(fields.value(QStringLiteral("NMASK")), QStringLiteral("255.255.255.0"));
    QCOMPARE(fields.value(QStringLiteral("GW")), QStringLiteral("192.168.2.1"));
    QCOMPARE(fields.value(QStringLiteral("AGW")), QStringLiteral("192.168.2.1"));
    QCOMPARE(fields.value(QStringLiteral("ID")), QStringLiteral("RT-21"));
    QCOMPARE(fields.value(QStringLiteral("HTTP")), QStringLiteral("801"));
    QCOMPARE(fields.value(QStringLiteral("UDP")), QStringLiteral("23073"));
    QCOMPARE(fields.value(QStringLiteral("TFTP")), QStringLiteral("69"));
    QCOMPARE(fields.value(QStringLiteral("HTTP_USER")), QString());
    QCOMPARE(fields.value(QStringLiteral("HTTP_PASS")), QStringLiteral("s3cret-not-shown"));
    QCOMPARE(fields.value(QStringLiteral("NVREG16_0")), QStringLiteral("6555"));
    QCOMPARE(fields.value(QStringLiteral("LINK")), QStringLiteral("1"));
    QVERIFY(!Rt21::redactForLog(QByteArray("AT*HTTP_PASS=s3cret-not-shown\r\n")).contains("s3cret"));
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
    qint64 lastTick = QDateTime::currentMSecsSinceEpoch();
    int maxGap = 0;
    QTimer tick;
    tick.setInterval(30);
    QObject::connect(&tick, &QTimer::timeout, &tick, [&]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        maxGap = std::max(maxGap, int(now - lastTick));
        lastTick = now;
    });
    tick.start();
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
    tick.stop();
    QVERIFY2(maxGap < 500, qPrintable(QStringLiteral("event loop stalled %1 ms").arg(maxGap)));
    QCOMPARE(status.at(0).at(0).toString(), QStringLiteral("Reading network (try 1 of 3)…"));
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
    QVERIFY(trace.at(enter - 1).startsWith("R2") || trace.at(enter - 1).startsWith("R0")
            || trace.at(enter - 1).startsWith("BI"));
    for (int i = 0; i < enter; ++i)
        QVERIFY(trace.at(i) != QByteArray("T11"));
    QVERIFY(sawEnter);
    QVERIFY(sawEcho);
    QVERIFY(sawEsc);
    QVERIFY(sawQuery);
    QFile logFile(TrafficLog::instance().filePath());
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    const QString log = QString::fromUtf8(logFile.readAll());
    QVERIFY(log.contains(QStringLiteral("network lead-in ESC ESC")));
    QVERIFY(log.contains(QStringLiteral("network lead-in ;")));
    QVERIFY(log.contains(QStringLiteral("network enter AT;")));
    QVERIFY(log.contains(QStringLiteral("network open DTR pulse")));
    QVERIFY(log.contains(QStringLiteral("post-uart2")));
    QVERIFY(!log.contains(QStringLiteral("network variant F T11;")));
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
    QVERIFY(dtrLow >= 180 && dtrLow <= 210);
    QVERIFY(dtrToEcho >= 0 && dtrToEcho <= 15);
    QVERIFY(uartToEcho >= 700 && uartToEcho <= 760);
    QVERIFY(log.contains(QStringLiteral("variant D")));
    QVERIFY(log.contains(QStringLiteral("network echo rx ERROR 1")));
    QVERIFY(log.contains(QStringLiteral("network echo send 2")));
    QVERIFY(log.contains(QStringLiteral("network query rx AGW=")));
    QVERIFY(preAtQuiet >= 650 && preAtQuiet <= 900);
}

void TestSetup::benchNetworkReadFillsSetupFields()
{
    MockRt21Server server;
    server.sim()->setBenchModuleLines(true);
    server.sim()->setModuleValue("HTTP_PASS", "s3cret-not-shown");
    server.sim()->setHeading(35.1);
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

    SetupPanel panel;
    panel.setDriver(&driver);
    QSignalSpy done(&driver, &Rt21Driver::networkFinished);
    qint64 logMark = 0;
    {
        QFile prior(TrafficLog::instance().filePath());
        if (prior.open(QIODevice::ReadOnly))
            logMark = prior.size();
    }
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 20000);
    QCOMPARE(done.at(0).at(0).toBool(), true);

    QWidget *page = panel.networkPage();
    QVERIFY(page);
    auto text = [&](const char *name) {
        auto *edit = page->findChild<QLineEdit *>(QString::fromLatin1(name));
        return edit ? edit->text() : QStringLiteral("<missing>");
    };
    QCOMPARE(text("net-id"), QStringLiteral("RT-21"));
    QCOMPARE(text("net-dhcp"), QStringLiteral("Static — the saved address is the one below"));
    QCOMPARE(panel.networkValues().value(QStringLiteral("dhcp")), QStringLiteral("0"));
    QCOMPARE(text("net-ip"), QStringLiteral("192.168.1.106"));
    QCOMPARE(text("net-aip"), QStringLiteral("192.168.1.106"));
    QCOMPARE(text("net-nmask"), QStringLiteral("255.255.255.0"));
    QCOMPARE(text("net-gw"), QStringLiteral("192.168.2.1"));
    QCOMPARE(text("net-agw"), QStringLiteral("192.168.2.1"));
    QCOMPARE(text("net-http"), QStringLiteral("801"));
    QCOMPARE(text("net-udp"), QStringLiteral("23073"));
    QCOMPARE(text("net-tftp"), QStringLiteral("69"));
    QCOMPARE(text("net-serialPort"), QStringLiteral("6555"));
    QCOMPARE(text("net-hwtype"), QStringLiteral("Type 2"));
    QCOMPARE(text("net-link"), QStringLiteral("Link OK"));
    QCOMPARE(text("net-user"), QString());
    auto *pass = page->findChild<QLineEdit *>(QStringLiteral("net-pass"));
    QVERIFY(pass);
    QCOMPARE(pass->echoMode(), QLineEdit::Password);
    QVERIFY(!pass->text().contains(QStringLiteral("s3cret")));
    QVERIFY(!pass->displayText().contains(QStringLiteral("s3cret")));
    QVERIFY(pass->text() != QStringLiteral("Not read"));
    auto *note = page->findChild<QLabel *>(QStringLiteral("gatewayNote"));
    QVERIFY(note);
    QVERIFY(note->text().contains(QStringLiteral("not on the same subnet")));
    QVERIFY(note->styleSheet().contains(statusToneColor(StatusTone::Busy).name()));

    const QList<QByteArray> trace = server.sim()->trace();
    const int enter = trace.indexOf(QByteArray("AT"));
    QVERIFY(enter > 0);
    for (int i = 0; i < enter; ++i)
        QVERIFY(trace.at(i) != QByteArray("T11"));
    QVERIFY(trace.at(enter - 1).startsWith("R2") || trace.at(enter - 1).startsWith("R0")
            || trace.at(enter - 1).startsWith("BI"));
    QVERIFY(trace.contains(QByteArray("AT*SERIAL_ECHO=0")));
    QVERIFY(trace.contains(QByteArray("ESC")));
    QVERIFY(!trace.contains(QByteArray("T11")));
    QFile logFile(TrafficLog::instance().filePath());
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    QVERIFY(logFile.seek(logMark));
    const QString log = QString::fromUtf8(logFile.readAll());
    QVERIFY(log.contains(QStringLiteral("network enter AT;")));
    QVERIFY(log.contains(QStringLiteral("network success variant D")));
    QVERIFY(log.contains(QStringLiteral("network echo ok")));
    QVERIFY(!log.contains(QStringLiteral("network echo send 2")));
    QVERIFY(log.contains(QStringLiteral("network query rx *HWTYPE=2")));
    QVERIFY(log.contains(QStringLiteral("network query rx AT*IP=192.168.1.106")));
    QVERIFY(log.contains(QStringLiteral("AT*HTTP_PASS=****")));
    QVERIFY(!log.contains(QStringLiteral("s3cret")));
    bool timingOk = true;
    const QString key = QStringLiteral("network uart2->ECHO ");
    const int at = log.lastIndexOf(key);
    QVERIFY(at >= 0);
    const int uartToEcho = log.mid(at + key.size()).section(QLatin1Char(' '), 0, 0).toInt(&timingOk);
    QVERIFY(timingOk);
    QVERIFY(uartToEcho >= 700 && uartToEcho <= 760);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    QCOMPARE(driver.link(), Rt21Driver::Link::On);
    driver.disconnectLink();
    QVERIFY(!server.sim()->trace().contains(QByteArray("T01")));
    QVERIFY(!server.sim()->trace().contains(QByteArray("T11")));
}

void TestSetup::coldBootWakeThenRead()
{
    MockRt21Server server;
    server.sim()->setBenchModuleLines(true);
    server.sim()->setColdBootTcp(true);
    server.sim()->setModuleValue("HTTP_PASS", "s3cret-not-shown");
    server.sim()->setHeading(35.1);
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
    QTRY_VERIFY_WITH_TIMEOUT(server.sim()->trace().contains(QByteArray("BI1")), 1000);
    QVERIFY(!server.sim()->trace().contains(QByteArray("T11")));

    SetupPanel panel;
    panel.setDriver(&driver);
    qint64 logMark = 0;
    {
        QFile prior(TrafficLog::instance().filePath());
        if (prior.open(QIODevice::ReadOnly))
            logMark = prior.size();
    }
    QSignalSpy done(&driver, &Rt21Driver::networkFinished);
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 40000);
    QCOMPARE(done.at(0).at(0).toBool(), true);

    QWidget *page = panel.networkPage();
    QVERIFY(page);
    auto text = [&](const char *name) {
        auto *edit = page->findChild<QLineEdit *>(QString::fromLatin1(name));
        return edit ? edit->text() : QStringLiteral("<missing>");
    };
    QCOMPARE(text("net-ip"), QStringLiteral("192.168.1.106"));
    QCOMPARE(text("net-gw"), QStringLiteral("192.168.2.1"));
    QCOMPARE(text("net-agw"), QStringLiteral("192.168.2.1"));
    QCOMPARE(text("net-id"), QStringLiteral("RT-21"));
    QCOMPARE(text("net-serialPort"), QStringLiteral("6555"));
    auto *pass = page->findChild<QLineEdit *>(QStringLiteral("net-pass"));
    QVERIFY(pass);
    QCOMPARE(pass->echoMode(), QLineEdit::Password);
    QVERIFY(!pass->text().contains(QStringLiteral("s3cret")));

    const QList<QByteArray> trace = server.sim()->trace();
    const int t11 = trace.indexOf(QByteArray("T11"));
    QVERIFY(t11 > 0);
    int firstAt = -1;
    int atAfterWake = -1;
    for (int i = 0; i < trace.size(); ++i) {
        if (trace.at(i) != QByteArray("AT"))
            continue;
        if (firstAt < 0)
            firstAt = i;
        if (i > t11)
            atAfterWake = i;
    }
    QVERIFY(firstAt >= 0 && firstAt < t11);
    QVERIFY(atAfterWake > t11);
    QVERIFY(trace.at(atAfterWake - 1) != QByteArray("T11"));
    bool escAfter = false;
    bool pollAfter = false;
    for (int i = t11 + 1; i < atAfterWake; ++i) {
        if (trace.at(i) == QByteArray("ESC"))
            escAfter = true;
        if (trace.at(i).startsWith("R2") || trace.at(i).startsWith("R0"))
            pollAfter = true;
    }
    QVERIFY(escAfter);
    QVERIFY(pollAfter);
    QCOMPARE(trace.count(QByteArray("T11")), 1);

    QFile logFile(TrafficLog::instance().filePath());
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    QVERIFY(logFile.seek(logMark));
    const QString log = QString::fromUtf8(logFile.readAll());
    QVERIFY(log.contains(QStringLiteral("network cold-boot wake: T11; -> ESC ESC -> ;")));
    QVERIFY(log.contains(QStringLiteral("network echo late bytes none")));
    QVERIFY(!log.contains(QStringLiteral("network next AT; waits")));
    QVERIFY(log.contains(QStringLiteral("network success variant D")));

    driver.disconnectLink();
    QTRY_COMPARE_WITH_TIMEOUT(server.sim()->trace().count(QByteArray("T01")), 1, 1000);
    QCOMPARE(server.sim()->trace().last(), QByteArray("T01"));
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
    QCOMPARE(status.at(0).at(0).toString(), QStringLiteral("Writing Gateway (try 1 of 3)…"));
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
    QCOMPARE(serialProbeBauds(), QList<int>({4800, 9600}));
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
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    QSignalSpy done(&driver, &Rt21Driver::networkFinished);
    QSignalSpy status(&driver, &Rt21Driver::networkStatus);
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 180000);
    QVERIFY(!done.at(0).at(0).toBool());
    QCOMPARE(done.at(0).at(1).toString(), QStringLiteral("No network board responded after 3 tries"));
    QStringList tries;
    for (int i = 0; i < status.count(); ++i)
        tries.append(status.at(i).at(0).toString());
    QVERIFY(tries.contains(QStringLiteral("Reading network (try 1 of 3)…")));
    QVERIFY(tries.contains(QStringLiteral("Reading network (try 2 of 3)…")));
    QVERIFY(tries.contains(QStringLiteral("Reading network (try 3 of 3)…")));
    QVERIFY(!tries.contains(QStringLiteral("Reading network (try 4 of 3)…")));
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
            QVERIFY(trace.at(i - 1) != QByteArray("T11"));
            QVERIFY(trace.at(i - 1).startsWith("R2") || trace.at(i - 1).startsWith("R0")
                    || trace.at(i - 1).startsWith("BI"));
            for (int j = 0; j < i; ++j)
                QVERIFY(trace.at(j) != QByteArray("T11"));
        }
        QVERIFY(cmd != "T01");
        QVERIFY(!cmd.startsWith("WW"));
        QVERIFY(!cmd.startsWith("W0"));
    }
    QCOMPARE(queries, 0);
    // Each of the 3 tries sends two lead-in ESC bytes plus the exit ESC.
    // Connect does not send T11, so there is no extra route ESC.
    QCOMPARE(exits, 9);
    QCOMPARE(enters, 3);
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
    QCOMPARE(statusTone(QStringLiteral("Reading network (try 3 of 3)…")), StatusTone::Busy);
    QCOMPARE(statusTone(QStringLiteral("Writing DHCP (try 1 of 3)…")), StatusTone::Busy);
    QCOMPARE(statusTone(QStringLiteral("Network module answered. Nothing was written.")), StatusTone::Success);
    QCOMPARE(statusTone(QStringLiteral("No network board responded after 3 tries")), StatusTone::Failure);
    QCOMPARE(statusTone(QStringLiteral("Network Connection is off.")), StatusTone::Failure);
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
