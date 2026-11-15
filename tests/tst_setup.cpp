/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
    void screenSaverIsOnlyInTheDisplayGroup();
    void firmwareGate();
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
    QCOMPARE(plan.first().command, QByteArray("WO18;"));
    QCOMPARE(plan.first().readBack, QByteArray("RO1;"));
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
    QCOMPARE(sequence.first().command, QByteArray("T11;"));
    QVERIFY(sequence.last().command == QByteArray("T01;"));
    bool sawKey = false;
    for (const auto &query : sequence) {
        if (query.key == QLatin1String("key")) {
            sawKey = true;
            QCOMPARE(query.token, QStringLiteral("KEY="));
        }
    }
    QVERIFY(sawKey);
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
    QVERIFY(server.listenTcp(0));
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("127.0.0.1");
    endpoint.tcpPort = int(server.port());
    driver.connectEndpoint(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);

    QMap<QString, QString> got;
    connect(&driver, &Rt21Driver::networkField, &driver, [&](const QString &key, const QString &value) { got.insert(key, value); });
    QSignalSpy done(&driver, &Rt21Driver::networkFinished);
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 8000);
    QCOMPARE(got.value(QStringLiteral("id")), QStringLiteral("Tower"));
    QCOMPARE(got.value(QStringLiteral("dhcp")), QStringLiteral("0"));
    QCOMPARE(got.value(QStringLiteral("serialPort")), QStringLiteral("6555"));
    QCOMPARE(got.value(QStringLiteral("security")), QStringLiteral("6"));
    QCOMPARE(got.value(QStringLiteral("key")), QStringLiteral("prairie-not-logged"));
    QCOMPARE(done.at(0).at(0).toBool(), true);
}

void TestSetup::screenSaverIsOnlyInTheDisplayGroup()
{
    QCOMPARE(Rt21::recommendedScreenSaverMinutes(), 1);
    QVERIFY(!Rt21::screenSaverMinuteAllowed(90));
    QVERIFY(Rt21::screenSaverRecommendation().contains(QStringLiteral("90 seconds")));
    QVERIFY(Rt21::screenSaverRecommendation().contains(QStringLiteral("30 seconds")));

    QMap<QString, QString> fields;
    fields.insert(QStringLiteral("screenSaver"), QStringLiteral("1"));
    fields.insert(QStringLiteral("brightness"), QStringLiteral("8"));
    QList<Rt21::PlannedWrite> plan;
    QVERIFY(Rt21::planGroup(QStringLiteral("display"), fields, &plan).isEmpty());
    QByteArray joined;
    for (const auto &step : plan)
        joined += step.command;
    QVERIFY(joined.contains("WW11;"));
    QVERIFY(joined.contains("WO18;"));

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
    QCOMPARE(Rt21::classifyFirmware(QStringLiteral("RT-21 4.14.g1")), Rt21::FirmwareClass::Tested);
    QCOMPARE(Rt21::classifyFirmware(QStringLiteral("RT-21 4.13.2")), Rt21::FirmwareClass::ExpectedCompatible);
    QCOMPARE(Rt21::classifyFirmware(QStringLiteral("RT-21 3.08")), Rt21::FirmwareClass::Untested);
    QVERIFY(Rt21::firmwareNotice(QStringLiteral("RT-21 4.14.g1")).isEmpty());
    QVERIFY(Rt21::firmwareNotice(QStringLiteral("RT-21 4.13.2")).contains(QStringLiteral("4.13.2")));
    QVERIFY(Rt21::firmwareWritesAllowed(QStringLiteral("RT-21 4.14.g1")));
    QVERIFY(!Rt21::firmwareWritesAllowed(QStringLiteral("RT-21 4.13.2")));
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

QTEST_MAIN(TestSetup)
#include "tst_setup.moc"
