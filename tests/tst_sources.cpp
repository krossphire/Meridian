/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Gridsquare.h"
#include "rotor/AppConfig.h"
#include "sources/CredentialStore.h"
#include "sources/DxKeeperSource.h"
#include "sources/QlogNotice.h"
#include "sources/N1mmXml.h"
#include "sources/QrzXml.h"
#include "sources/WsjtSource.h"
#include "sources/WsjtStatus.h"

#include <QHostAddress>
#include <QSignalSpy>
#include <QUdpSocket>

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
    void wsjtFakeDatagram();
    void n1mmFixtures();
    void dxKeeperItems();
    void qlogCurrentContact();
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
    const QByteArray nearby = R"(<?xml version="1.0" ?>
<QRZDatabase version="1.34">
  <Callsign>
    <call>K5KAC</call>
    <lat>36.430000</lat>
    <lon>-94.270000</lon>
    <grid>EM26ul</grid>
  </Callsign>
</QRZDatabase>)";
    const QrzXml local = parseQrzXml(nearby);
    QCOMPARE(local.grid, QStringLiteral("EM26UL"));
    QVERIFY(local.haveLatitude && local.haveLongitude);
    QCOMPARE(local.latitude, 36.43);
    QCOMPARE(local.longitude, -94.27);
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
    QVERIFY(wsjtListenAllowed(2237, QStringLiteral("224.0.0.1")));
    QCOMPARE(AppConfig::defaults().wsjtPort, 0);
    QCOMPARE(AppConfig::defaults().wsjtMulticast, QString());

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

void TestSources::n1mmFixtures()
{
    QCOMPARE(AppConfig::defaults().n1mmPort, 0);
    const QByteArray lookup = R"(<?xml version="1.0" encoding="utf-8"?>
<lookupinfo>
<app>N1MM</app>
<call>C91RU</call>
<gridsquare>KG54</gridsquare>
</lookupinfo>)";
    const N1mmPacket found = parseN1mmPacket(lookup);
    QCOMPARE(int(found.kind), int(N1mmPacket::Kind::Lookup));
    QCOMPARE(found.call, QStringLiteral("C91RU"));
    QCOMPARE(found.grid, QStringLiteral("KG54"));

    const QByteArray contact = R"(<contactinfo><call>C91RU</call><gridsquare>KG54</gridsquare></contactinfo>)";
    const N1mmPacket logged = parseN1mmPacket(contact);
    QCOMPARE(int(logged.kind), int(N1mmPacket::Kind::Contact));
    QCOMPARE(logged.call, QStringLiteral("C91RU"));

    const N1mmPacket radio = parseN1mmPacket(QByteArray("<RadioInfo><Freq>1400000</Freq></RadioInfo>"));
    QCOMPARE(int(radio.kind), int(N1mmPacket::Kind::None));

    const N1mmPacket grid4 = parseN1mmPacket(QByteArray("<lookupinfo><call>K1ABC</call><grid4>FN42</grid4></lookupinfo>"));
    QCOMPARE(grid4.grid, QStringLiteral("FN42"));
}

void TestSources::dxKeeperItems()
{
    QVERIFY(!AppConfig::defaults().dxKeeper);
    const DxKeeperReading first = interpretDxKeeper(QString(), QString(), QStringLiteral("c91ru"), QStringLiteral("kg54"));
    QCOMPARE(int(first.action), int(DxKeeperAction::Offer));
    QCOMPARE(first.call, QStringLiteral("C91RU"));
    QCOMPARE(first.grid, QStringLiteral("KG54"));
    const DxKeeperReading same = interpretDxKeeper(first.call, first.grid, QStringLiteral("C91RU"), QStringLiteral("KG54"));
    QCOMPARE(int(same.action), int(DxKeeperAction::None));
    const DxKeeperReading noGrid = interpretDxKeeper(QString(), QString(), QStringLiteral("ZL1ABC"), QString());
    QCOMPARE(int(noGrid.action), int(DxKeeperAction::Offer));
    QVERIFY(noGrid.grid.isEmpty());
    const DxKeeperReading gone = interpretDxKeeper(QStringLiteral("ZL1ABC"), QString(), QString(), QString());
    QCOMPARE(int(gone.action), int(DxKeeperAction::Clear));
}

void TestSources::qlogCurrentContact()
{
    QCOMPARE(AppConfig::defaults().qlogPort, 0);
    const QByteArray packet = R"({"appid":"QLog","msgtype":"currentcontact","time":1,"data":{"callsign":"C91RU","grid":"KG54","azimuth":92.4}})";
    const QlogContact contact = parseQlogNotice(packet);
    QVERIFY(contact.isCurrentContact);
    QCOMPARE(contact.call, QStringLiteral("C91RU"));
    QCOMPARE(contact.grid, QStringLiteral("KG54"));
    QVERIFY(contact.haveAzimuth);
    QCOMPARE(QString::number(contact.azimuth, 'f', 1), QStringLiteral("92.4"));
    QVERIFY(!parseQlogNotice(QByteArray(R"({"msgtype":"qso","data":{}})")).isCurrentContact);
    const QlogContact cleared = parseQlogNotice(QByteArray(R"({"msgtype":"currentcontact","data":{"callsign":""}})"));
    QVERIFY(cleared.isCurrentContact);
    QVERIFY(cleared.call.isEmpty());
}

QByteArray wsjtStatusDatagram()
{
    QByteArray datagram;
    QDataStream stream(&datagram, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(0xadbccbda) << quint32(3) << quint32(1);
    stream << QByteArray("WSJT-X") << quint64(14074000) << QByteArray("FT8");
    stream << QByteArray("C91RU") << QByteArray("-10") << QByteArray("FT8");
    stream << true << false << false;
    stream << quint32(1200) << quint32(1200);
    stream << QByteArray("K5PO") << QByteArray("EM26") << QByteArray("KG54");
    return datagram;
}

void TestSources::wsjtFakeDatagram()
{
    QUdpSocket holder;
    QVERIFY(holder.bind(QHostAddress::AnyIPv4, 0,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint));
    const quint16 port = holder.localPort();
    holder.close();

    WsjtSource source;
    QSignalSpy dx(&source, &WsjtSource::dxStation);
    source.listen(int(port), QString());
    QUdpSocket sender;
    QCOMPARE(sender.writeDatagram(wsjtStatusDatagram(), QHostAddress::LocalHost, port), wsjtStatusDatagram().size());
    QTRY_COMPARE_WITH_TIMEOUT(dx.size(), 1, 2000);
    QCOMPARE(dx.at(0).at(0).toString(), QStringLiteral("C91RU"));
    QCOMPARE(dx.at(0).at(1).toString(), QStringLiteral("KG54"));

    QSignalSpy notes(&source, &WsjtSource::note);
    dx.clear();
    source.listen(int(port), QStringLiteral("224.0.0.1"));
    QUdpSocket multi;
    multi.setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    const QByteArray packet = wsjtStatusDatagram();
    multi.writeDatagram(packet, QHostAddress(QStringLiteral("224.0.0.1")), port);
    QTRY_COMPARE_WITH_TIMEOUT(dx.size() + notes.size(), 1, 2000);
    if (dx.size() == 1) {
        QCOMPARE(dx.at(0).at(0).toString(), QStringLiteral("C91RU"));
        QCOMPARE(notes.size(), 0);
    } else {
        QVERIFY(notes.at(0).at(0).toString().contains(QStringLiteral("multicast")));
    }
}

QTEST_MAIN(TestSources)
#include "tst_sources.moc"
