/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Driver.h"
#include "rotor/Rt21Protocol.h"
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
    QVERIFY(text.contains(QStringLiteral("D=180")));
    QVERIFY(text.contains(QStringLiteral("key=secret-phrase")));
    QVERIFY(text.contains(QStringLiteral("DNS is not")));
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

QTEST_MAIN(TestSetup)
#include "tst_setup.moc"
