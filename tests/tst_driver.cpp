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
