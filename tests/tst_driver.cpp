/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mock/Rt21Simulator.h"
#include "rotor/Rt21Driver.h"
#include "rotor/Rt21Finder.h"
#include "rotor/TrafficLog.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <optional>

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
    void unreachableConnectIsQuick();
    void readNetworkWithoutAPortFinishes();
    void failureNoteClearsOnConnect();
    void abortClearsTheTarget();
    void trafficLogWritesAreLocked();
#ifdef Q_OS_UNIX
    void serialPty();
    void serialConnectCancelsIpReconnect();
    void findHitConnectsWhileIpReconnectIsArmed();
    void repeatedFindHandoffWithReconnectArmed();
    void stopOnALiveSerialLink();
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
    for (const QByteArray &cmd : server.sim()->trace())
        QVERIFY(cmd != "T11");
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
    moving.feed("AP1100.0;");
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

void TestDriver::unreachableConnectIsQuick()
{
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("192.0.2.1");
    endpoint.tcpPort = 9;
    QElapsedTimer timer;
    timer.start();
    driver.connectEndpoint(endpoint);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::Failed, 4000);
    QVERIFY2(timer.elapsed() <= 4000, qPrintable(QString::number(timer.elapsed())));
    QTimer *backoff = nullptr;
    for (QTimer *candidate : driver.findChildren<QTimer *>()) {
        if (candidate->isActive() && candidate->isSingleShot() && candidate->interval() >= 10000)
            backoff = candidate;
    }
    QVERIFY(backoff);
    QCOMPARE(backoff->interval(), 15000);
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
    QElapsedTimer clock;
    clock.start();
    driver.connectEndpoint(endpoint);
    if (driver.link() == Rt21Driver::Link::Failed)
        QSKIP(qPrintable(driver.detail()));

    QTRY_VERIFY_WITH_TIMEOUT(driver.link() == Rt21Driver::Link::On || driver.link() == Rt21Driver::Link::Failed, 3000);
    if (driver.link() == Rt21Driver::Link::Failed)
        QSKIP(qPrintable(driver.detail()));
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 1000);
    QVERIFY(clock.elapsed() < 1000);
    QVERIFY(std::abs(*driver.heading() - 33.0) < 1.5);
    const QList<QByteArray> trace = server.sim()->trace();
    const int bi = trace.indexOf(QByteArray("BI1"));
    const int r11 = trace.indexOf(QByteArray("R11"));
    QCOMPARE(bi, 0);
    QVERIFY(r11 > bi);
    QVERIFY(!trace.contains(QByteArray("T11")));
    QVERIFY(!trace.contains(QByteArray("R01")));
    QVERIFY(!trace.contains(QByteArray("AT")));
    driver.goTo(70);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading() && std::abs(*driver.heading() - 70.0) < 2.0, 4000);
}

void TestDriver::serialConnectCancelsIpReconnect()
{
    Rt21Driver driver;
    RotorEndpoint tcp;
    tcp.transport = RotorEndpoint::Transport::Tcp;
    tcp.host = QStringLiteral("192.0.2.1");
    tcp.tcpPort = 9;
    driver.connectEndpoint(tcp);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::Failed, 5000);
    bool armed = false;
    for (QTimer *candidate : driver.findChildren<QTimer *>()) {
        if (candidate->isActive() && candidate->isSingleShot() && candidate->interval() >= 10000)
            armed = true;
    }
    QVERIFY(armed);

    MockRt21Server server;
    server.sim()->setHeading(50);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");
    RotorEndpoint serial;
    serial.transport = RotorEndpoint::Transport::Serial;
    serial.serialPort = pty;
    serial.baud = 4800;
    driver.connectEndpoint(serial);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);
    for (QTimer *candidate : driver.findChildren<QTimer *>()) {
        if (candidate->isActive() && candidate->isSingleShot() && candidate->interval() >= 10000)
            QFAIL("IP reconnect timer still armed after the serial port connected");
    }
}

void TestDriver::findHitConnectsWhileIpReconnectIsArmed()
{
    Rt21Driver driver;
    RotorEndpoint tcp;
    tcp.transport = RotorEndpoint::Transport::Tcp;
    tcp.host = QStringLiteral("192.0.2.1");
    tcp.tcpPort = 9;
    driver.connectEndpoint(tcp);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::Failed, 5000);
    QTimer *backoff = nullptr;
    for (QTimer *candidate : driver.findChildren<QTimer *>()) {
        if (candidate->isActive() && candidate->isSingleShot() && candidate->interval() >= 10000)
            backoff = candidate;
    }
    QVERIFY(backoff);

    MockRt21Server server;
    server.sim()->setHeading(36.7);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");

    // The GUI must not open the port until this thread has exited. Opening
    // COM6 while the probe's QSerialPort was still winding down is the crash.
    QThread *thread = QThread::create([&]() {
        Rt21Finder::scanSerialPorts(1000, {}, {}, {pty});
    });
    QEventLoop loop;
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    thread->start();
    loop.exec();
    QVERIFY(thread->isFinished());
    thread->deleteLater();

    RotorEndpoint serial;
    serial.transport = RotorEndpoint::Transport::Serial;
    serial.serialPort = pty;
    serial.baud = 4800;
    driver.connectEndpoint(serial);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::On, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    for (QTimer *candidate : driver.findChildren<QTimer *>()) {
        if (candidate->isActive() && candidate->isSingleShot() && candidate->interval() >= 10000)
            QFAIL("IP reconnect timer still armed after Find's serial hit connected");
    }
    backoff->start(0);
    QCoreApplication::processEvents();
    QCOMPARE(driver.link(), Rt21Driver::Link::On);
    QVERIFY(driver.detail().contains(pty));

    const QList<QByteArray> trace = server.sim()->trace();
    QCOMPARE(trace.indexOf(QByteArray("BI1")), 0);
    QVERIFY(trace.indexOf(QByteArray("R11")) > 0);
    QVERIFY(!trace.contains(QByteArray("T11")));
}

void TestDriver::repeatedFindHandoffWithReconnectArmed()
{
    MockRt21Server server;
    server.sim()->setHeading(41);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");
    Rt21Driver driver;
    for (int pass = 0; pass < 4; ++pass) {
        server.sim()->clearTrace();
        RotorEndpoint tcp;
        tcp.transport = RotorEndpoint::Transport::Tcp;
        tcp.host = QStringLiteral("192.0.2.1");
        tcp.tcpPort = 9;
        driver.connectEndpoint(tcp);
        QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::Failed, 5000);
        bool armed = false;
        for (QTimer *candidate : driver.findChildren<QTimer *>()) {
            if (candidate->isActive() && candidate->isSingleShot() && candidate->interval() >= 10000)
                armed = true;
        }
        QVERIFY(armed);
        driver.disconnectLink();
        QCOMPARE(driver.link(), Rt21Driver::Link::Off);
        for (QTimer *candidate : driver.findChildren<QTimer *>()) {
            if (candidate->isActive() && candidate->isSingleShot() && candidate->interval() >= 10000)
                QFAIL("reconnect timer still armed after Find released the link");
        }

        std::optional<FoundRotor> hit;
        QThread *thread = QThread::create([&]() {
            hit = Rt21Finder::probeSerial(pty, QStringLiteral("serial"), 4800, 800);
        });
        QEventLoop loop;
        QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        thread->start();
        loop.exec();
        QVERIFY(thread->isFinished());
        thread->deleteLater();
        QCoreApplication::processEvents();
        QVERIFY(hit.has_value());

        QElapsedTimer clock;
        clock.start();
        RotorEndpoint serial;
        serial.transport = RotorEndpoint::Transport::Serial;
        serial.serialPort = pty;
        serial.baud = 4800;
        driver.connectEndpoint(serial);
        QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 1000);
        QVERIFY(clock.elapsed() < 1000);
        QVERIFY(!server.sim()->trace().contains(QByteArray("T11")));
        QCOMPARE(server.sim()->trace().indexOf(QByteArray("BI1")), 0);
        driver.disconnectLink();
    }
}

void TestDriver::stopOnALiveSerialLink()
{
    MockRt21Server server;
    server.sim()->setHeading(22);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Serial;
    endpoint.serialPort = pty;
    endpoint.baud = 4800;
    driver.connectEndpoint(endpoint);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    server.sim()->clearTrace();
    driver.stopMotion();
    QTRY_VERIFY_WITH_TIMEOUT(server.sim()->trace().contains(QByteArray(";")), 2000);
}
#endif

void TestDriver::readNetworkWithoutAPortFinishes()
{
    Rt21Driver driver;
    QSignalSpy done(&driver, &Rt21Driver::networkFinished);
    driver.readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 2000);
    QVERIFY(!done.at(0).at(0).toBool());
    QVERIFY(done.at(0).at(1).toString().contains(QStringLiteral("Connect to a rotor")));
}

void TestDriver::failureNoteClearsOnConnect()
{
    Rt21Driver driver;
    RotorEndpoint down;
    down.transport = RotorEndpoint::Transport::Tcp;
    down.host = QStringLiteral("192.0.2.1");
    down.tcpPort = 9;
    driver.connectEndpoint(down);
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::Failed, 5000);
    driver.goTo(15);
    QTRY_VERIFY_WITH_TIMEOUT(driver.transientNote().contains(QStringLiteral("not connected")), 4000);

    MockRt21Server server;
    server.sim()->setHeading(11);
    QVERIFY(server.listenTcp(0));
    RotorEndpoint up;
    up.transport = RotorEndpoint::Transport::Tcp;
    up.host = QStringLiteral("127.0.0.1");
    up.tcpPort = int(server.port());
    driver.connectEndpoint(up);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    QCOMPARE(driver.transientNote(), QString());
}

void TestDriver::abortClearsTheTarget()
{
    auto *server = new MockRt21Server;
    server->sim()->setHeading(16);
    QVERIFY(server->listenTcp(0));
    Rt21Driver driver;
    RotorEndpoint endpoint;
    endpoint.transport = RotorEndpoint::Transport::Tcp;
    endpoint.host = QStringLiteral("127.0.0.1");
    endpoint.tcpPort = int(server->port());
    driver.connectEndpoint(endpoint);
    QTRY_VERIFY_WITH_TIMEOUT(driver.heading().has_value(), 3000);
    driver.goTo(40);
    QTRY_VERIFY_WITH_TIMEOUT(driver.target().has_value(), 2000);
    delete server;
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(driver.link(), Rt21Driver::Link::Failed, 3000);
    QVERIFY(!driver.target().has_value());
    QVERIFY(!driver.detail().contains(QStringLiteral("Unknown error"), Qt::CaseInsensitive));
}

void TestDriver::trafficLogWritesAreLocked()
{
    TrafficLog &log = TrafficLog::instance();
    log.setEnabled(true);
    constexpr int count = 40;
    auto *left = QThread::create([&log]() {
        for (int i = 0; i < count; ++i)
            log.recordSession("NOTE", QByteArray("lock-left"));
    });
    auto *right = QThread::create([&log]() {
        for (int i = 0; i < count; ++i)
            log.recordSession("NOTE", QByteArray("lock-right"));
    });
    left->start();
    right->start();
    QVERIFY(left->wait(3000));
    QVERIFY(right->wait(3000));
    log.flush();
    QFile file(log.filePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QCOMPARE(text.count(QStringLiteral("lock-left")), count);
    QCOMPARE(text.count(QStringLiteral("lock-right")), count);
    delete left;
    delete right;
}

QTEST_GUILESS_MAIN(TestDriver)
#include "tst_driver.moc"
