/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Gridsquare.h"
#include "rotor/AppConfig.h"
#include "sources/CredentialStore.h"
#include "sources/QrzXml.h"
#include "sources/WsjtSource.h"
#include "sources/WsjtStatus.h"

#include <QDataStream>

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class TestSources : public QObject
{
    Q_OBJECT
private slots:
    void qrzXmlUsesLatLonNotBearing();
    void passwordStaysOutOfIni();
    void wsjtStatusFixture();
};

void TestSources::qrzXmlUsesLatLonNotBearing()
{
    const QByteArray login = R"(<?xml version="1.0" ?>
<QRZDatabase version="1.34">
  <Session>
    <Key>abc123session</Key>
    <Count>1</Count>
  </Session>
</QRZDatabase>)";
    const QrzXml session = parseQrzXml(login);
    QCOMPARE(session.sessionKey, QStringLiteral("abc123session"));
    QVERIFY(session.error.isEmpty());
    QVERIFY(!session.sawBearing);

    const QByteArray lookup = R"(<?xml version="1.0" ?>
<QRZDatabase version="1.34">
  <Callsign>
    <call>C91RU</call>
    <lat>-25.9</lat>
    <lon>32.6</lon>
    <grid>KG54</grid>
    <land>Mozambique</land>
  </Callsign>
</QRZDatabase>)";
    const QrzXml record = parseQrzXml(lookup);
    QCOMPARE(record.call, QStringLiteral("C91RU"));
    QCOMPARE(record.grid, QStringLiteral("KG54"));
    QVERIFY(record.haveLatitude && record.haveLongitude);
    QVERIFY(!record.sawBearing);
    double fromGrid = 0;
    QVERIFY(Gridsquare::bearingTo(QStringLiteral("EM26"), record.grid, fromGrid));
    QCOMPARE(QString::number(fromGrid, 'f', 1), QStringLiteral("92.4"));
    double fromPoint = 0;
    QVERIFY(Gridsquare::bearingTo(36.5, -95.0, record.latitude, record.longitude, fromPoint));
    QVERIFY(fromPoint > 80.0 && fromPoint < 110.0);

    const QrzXml hostile = parseQrzXml(QByteArray("<Callsign><bearing>1</bearing><grid>FM18</grid></Callsign>"));
    QVERIFY(hostile.sawBearing);
    QCOMPARE(hostile.grid, QStringLiteral("FM18"));
}

void TestSources::passwordStaysOutOfIni()
{
    CredentialStore::useMemoryForTests(true);
    QVERIFY(CredentialStore::store(QStringLiteral("K5PO"), QStringLiteral("s3cret-not-for-ini")));
    QCOMPARE(CredentialStore::load(QStringLiteral("K5PO")), QStringLiteral("s3cret-not-for-ini"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("meridian.ini")), QSettings::IniFormat);
    AppConfig config = AppConfig::defaults();
    config.qrzUsername = QStringLiteral("K5PO");
    config.save(settings);
    settings.sync();

    QFile file(dir.filePath(QStringLiteral("meridian.ini")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(file.readAll());
    QVERIFY(body.contains(QStringLiteral("K5PO")));
    QVERIFY(!body.contains(QStringLiteral("s3cret-not-for-ini")));
    QVERIFY(!body.contains(QStringLiteral("password"), Qt::CaseInsensitive));
    CredentialStore::forget(QStringLiteral("K5PO"));
    QVERIFY(CredentialStore::load(QStringLiteral("K5PO")).isEmpty());
    CredentialStore::useMemoryForTests(false);
}

void TestSources::wsjtStatusFixture()
{
    QVERIFY(!wsjtListenAllowed(0, QString()));
    QVERIFY(!wsjtListenAllowed(2237, QString()));
    QVERIFY(wsjtListenAllowed(2238, QString()));
    QVERIFY(wsjtListenAllowed(2237, QStringLiteral("239.255.0.1")));
    QCOMPARE(AppConfig::defaults().wsjtPort, 0);

    QByteArray datagram;
    QDataStream stream(&datagram, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(0xadbccbda) << quint32(3) << quint32(1);
    stream << QByteArray("WSJT-X") << quint64(14074000) << QByteArray("FT8");
    stream << QByteArray("C91RU") << QByteArray("-10") << QByteArray("FT8");
    stream << true << false << false;
    stream << quint32(1200) << quint32(1200);
    stream << QByteArray("K5PO") << QByteArray("EM26") << QByteArray("KG54");
    const WsjtStatus status = parseWsjtStatus(datagram);
    QVERIFY(status.isStatus);
    QCOMPARE(status.dxCall, QStringLiteral("C91RU"));
    QCOMPARE(status.dxGrid, QStringLiteral("KG54"));

    QByteArray heartbeat;
    QDataStream heart(&heartbeat, QIODevice::WriteOnly);
    heart.setByteOrder(QDataStream::BigEndian);
    heart << quint32(0xadbccbda) << quint32(3) << quint32(0);
    QVERIFY(!parseWsjtStatus(heartbeat).isStatus);
}

QTEST_MAIN(TestSources)
#include "tst_sources.moc"
