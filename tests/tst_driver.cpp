/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mock/Rt21Simulator.h"
#include "rotor/Rt21Driver.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <cmath>

#ifdef Q_OS_UNIX
#include <QSerialPort>
#endif

class TestDriver : public QObject
{
    Q_OBJECT
private slots:
    void tcpMoveAndStop();
    void secondClientRefused();
    void hardwareReplyBytes();
    void prePollThenOneGoto();
    void dropCommandWithoutReplay();
#ifdef Q_OS_UNIX
    void serialPty();
#endif
};

void TestDriver::tcpMoveAndStop()
{
    MockRt21Server server;
    server.sim()->setHeading(10);
    server.sim()->setSpeed(400);
    QVERIFY(server.listenTcp(0));

    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("127.0.0.1");
    endpoint.tcpPort = server.port();
    driver.connectEndpoint(endpoint);

    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    QVERIFY(std::abs(*driver.heading() - 10.0) < 1.0);

    driver.goTo(80);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading() && std::abs(*driver.heading() - 80.0) < 1.5, 4000);

    server.sim()->setSpeed(30);
    driver.goTo(10);
    QTRY_VERIFY_WITH_TIMEOUT(driver.link() == Rt21Driver::Link::Moving || driver.target().has_value(), 2000);
    driver.stopMotion();
    QTRY_COMPARE_WITH_TIMEOUT(server.sim()->moving(), false, 2000);
}

void TestDriver::hardwareReplyBytes()
{
    Rt21Simulator sim;
    QByteArray got;
    QObject::connect(&sim, &Rt21Simulator::output, &sim, [&](const QByteArray &bytes) { got = bytes; });
    sim.setHeading(131.9);
    for (int i = 0; i < 4; ++i) {
        got.clear();
        sim.feed("BI1;");
        QCOMPARE(got, QByteArray("131.9;"));
    }
    got.clear();
    sim.feed("BI1;");
    QCOMPARE(got, QByteArray::fromHex("0130003E3133312E393B"));

    Rt21Simulator moving;
    QObject::connect(&moving, &Rt21Simulator::output, &moving, [&](const QByteArray &bytes) { got = bytes; });
    moving.setHeading(122.6);
    moving.setSpeed(0);
    moving.feed("AP1100.0\r;");
    QVERIFY(moving.moving());
    for (int i = 0; i < 4; ++i) {
        got.clear();
        moving.feed("BI1;");
        QCOMPARE(got, QByteArray("122.6;"));
    }
    got.clear();
    moving.feed("BI1;");
    QCOMPARE(got, QByteArray::fromHex("0130013E3132322E363B"));
}

void TestDriver::prePollThenOneGoto()
{
    MockRt21Server server;
    server.sim()->setHeading(10);
    server.sim()->setSpeed(400);
    QVERIFY(server.listenTcp(0));
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("127.0.0.1");
    endpoint.tcpPort = server.port();
    driver.connectEndpoint(endpoint);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    server.sim()->clearTrace();
    driver.goTo(80);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading() && std::abs(*driver.heading() - 80.0) < 1.5, 4000);
    const QList<QByteArray> trace = server.sim()->trace();
    int query = -1;
    int go = -1;
    int goCount = 0;
    for (int i = 0; i < trace.size(); ++i) {
        if (query < 0 && (trace.at(i).startsWith("BI") || trace.at(i).startsWith("AI")))
            query = i;
        if (trace.at(i).startsWith("AP")) {
            if (go < 0)
                go = i;
            ++goCount;
        }
    }
    QVERIFY(query >= 0);
    QVERIFY(go > query);
    QCOMPARE(goCount, 1);
}

void TestDriver::dropCommandWithoutReplay()
{
    MockRt21Server server;
    server.sim()->setHeading(20);
    QVERIFY(server.listenTcp(0));
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("127.0.0.1");
    endpoint.tcpPort = server.port();
    driver.connectEndpoint(endpoint);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    server.sim()->setIgnoreQueries(true);
    server.sim()->clearTrace();
    driver.goTo(40);
    QTRY_VERIFY_WITH_TIMEOUT(driver.transientNote().contains(QStringLiteral("not connected")), 4000);
    QVERIFY(!driver.target().has_value());
    int goCount = 0;
    for (const QByteArray &command : server.sim()->trace()) {
        if (command.startsWith("AP"))
            ++goCount;
    }
    QCOMPARE(goCount, 0);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::Failed, 8000);
    QTimer *backoff = nullptr;
    const auto timers = driver.findChildren<QTimer *>();
    for (QTimer *timer : timers) {
        if (timer->isActive() && timer->isSingleShot() && timer->interval() >= 15000)
            backoff = timer;
    }
    QVERIFY(backoff);
    QCOMPARE(backoff->interval(), 20000);
    goCount = 0;
    for (const QByteArray &command : server.sim()->trace()) {
        if (command.startsWith("AP"))
            ++goCount;
    }
    QCOMPARE(goCount, 0);
}

void TestDriver::secondClientRefused()
{
    MockRt21Server server;
    QVERIFY(server.listenTcp(0));
    QTcpSocket first;
    first.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QVERIFY(first.waitForConnected(2000));

    QTcpSocket second;
    QSignalSpy errors(&second, &QTcpSocket::errorOccurred);
    second.connectToHost(QStringLiteral("127.0.0.1"), server.port());
    QTRY_VERIFY_WITH_TIMEOUT(second.state() == QAbstractSocket::UnconnectedState || !errors.isEmpty(), 3000);
    QCOMPARE(second.error(), QAbstractSocket::ConnectionRefusedError);
}

#ifdef Q_OS_UNIX
void TestDriver::serialPty()
{
    MockRt21Server server;
    server.sim()->setHeading(33);
    server.sim()->setSpeed(500);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");

    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Serial;
    endpoint.serialPort = pty;
    endpoint.baud = 4800;
    driver.connectEndpoint(endpoint);
    if (driver.link() == Rt21Driver::Link::Failed)
        QSKIP(qPrintable(driver.detail()));

    QTRY_VERIFY_WITH_TIMEOUT(driver.link() == Rt21Driver::Link::On || driver.link() == Rt21Driver::Link::Failed, 3000);
    if (driver.link() == Rt21Driver::Link::Failed)
        QSKIP(qPrintable(driver.detail()));
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    QVERIFY(std::abs(*driver.heading() - 33.0) < 1.5);
    driver.goTo(70);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading() && std::abs(*driver.heading() - 70.0) < 2.0, 4000);
}
#endif

QTEST_GUILESS_MAIN(TestDriver)
#include "tst_driver.moc"
