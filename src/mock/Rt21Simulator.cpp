/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Simulator.h"

#include "core/Bearing.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_UNIX
#include <QSocketNotifier>

#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#endif

Rt21Simulator::Rt21Simulator(QObject *parent)
    : QObject(parent)
{
    restoreDefaults();
}

void Rt21Simulator::restoreDefaults()
{
    ee.clear();
    const QList<QPair<QChar, int>> defaults = {
        {'A', 0},   {'B', 950}, {'C', 0},   {'D', 180}, {'E', 3},   {'F', 3},   {'G', 10},
        {'H', 180}, {'I', 180}, {'K', 3960}, {'L', 360}, {'N', 3},  {'O', 8},   {'P', 4},
        {'R', 361}, {'S', 361}, {'T', 0},   {'U', 90},  {'V', 10},  {'X', 1},   {'Y', 40},
    };
    for (const auto &item : defaults)
        ee.insert(item.first, item.second);
}

void Rt21Simulator::setHeading(double azimuth)
{
    heading_ = clampRt21Azimuth(azimuth);
}

void Rt21Simulator::setSpeed(double degreesPerSecond)
{
    speed_ = std::max(0.0, degreesPerSecond);
}

void Rt21Simulator::setFrozen(bool frozen)
{
    frozen_ = frozen;
    if (frozen_)
        moving_ = false;
}

void Rt21Simulator::reply(const QByteArray &payload, bool soh)
{
    QByteArray out;
    if (soh)
        out.append(char(0x01));
    out.append(payload);
    if (!out.endsWith(';'))
        out.append(';');
    emit output(out);
}

void Rt21Simulator::startMotion()
{
    target_ = preset_;
    moving_ = !frozen_ && std::abs(std::remainder(target_ - heading_, 360.0)) > 0.05;
}

void Rt21Simulator::feed(const QByteArray &bytes)
{
    for (unsigned char c : bytes) {
        if (c == '\n')
            continue;
        if (c == '\r') {
            // CR inside AP1xxx.y\r; is what makes the turn immediate. The ';'
            // still terminates the command, so it is not a separate stop.
            sawCr = true;
            continue;
        }
        if (c == ';') {
            handle(buffer, sawCr);
            buffer.clear();
            sawCr = false;
            continue;
        }
        buffer.append(char(c));
    }
}

void Rt21Simulator::handle(const QByteArray &command, bool immediate)
{
    const QByteArray cmd = command.toUpper().trimmed();
    if (cmd.isEmpty() || cmd.startsWith("ST")) {
        moving_ = false;
        return;
    }
    if (cmd.startsWith("BI")) {
        reply(QString::asprintf("%5.1f", heading_).toLatin1(), false);
        return;
    }
    if (cmd.startsWith("AI")) {
        reply(QString::asprintf("%03.0f", std::floor(heading_ + 0.05)).toLatin1(), false);
        return;
    }
    if (cmd.startsWith("AP")) {
        const int split = cmd.startsWith("AP") ? 2 : 0;
        QByteArray rest = cmd.mid(split);
        if (!rest.isEmpty() && rest.at(0) >= '0' && rest.at(0) <= '9')
            rest = rest.mid(1);
        bool ok = false;
        const double az = QString::fromLatin1(rest.trimmed()).toDouble(&ok);
        if (ok) {
            preset_ = clampRt21Azimuth(az);
            if (immediate)
                startMotion();
        }
        return;
    }
    if (cmd.startsWith("AM")) {
        startMotion();
        return;
    }
    if (cmd.startsWith("R2")) {
        const char status = moving_ ? '1' : '0';
        QByteArray payload;
        payload.append('0');
        payload.append(status);
        payload.append('>');
        payload.append(QString::number(heading_, 'f', 1).toLatin1());
        reply(payload, true);
        return;
    }
    if (cmd.startsWith("R1")) {
        reply(QByteArray("RT-21 Version 4.13"), true);
        return;
    }
    if (cmd.startsWith("R0")) {
        reply(QString::asprintf("%04d", pot).toLatin1(), true);
        return;
    }
    if (cmd.startsWith("R4")) {
        reply(QByteArray("12"), true);
        return;
    }
    if (cmd.size() >= 3 && cmd.at(0) == 'R' && cmd.at(1) >= 'A' && cmd.at(1) <= 'Z') {
        const QChar id(cmd.at(1));
        if (id == QLatin1Char('J')) {
            reply(QByteArray("Pot"), true);
            return;
        }
        if (id == QLatin1Char('M')) {
            reply(QByteArray("Normal"), true);
            return;
        }
        reply(QByteArray::number(ee.value(id, 0)), true);
        return;
    }
    if (cmd == "W00") {
        restoreDefaults();
        moving_ = false;
        return;
    }
    if (cmd.size() >= 4 && cmd.at(0) == 'W' && cmd.at(1) >= 'A' && cmd.at(1) <= 'Z') {
        const QChar id(cmd.at(1));
        bool ok = false;
        const int value = QString::fromLatin1(cmd.mid(3)).toInt(&ok);
        if (ok)
            ee.insert(id, value);
        return;
    }
    if (cmd.startsWith("AW")) {
        QByteArray rest = cmd.mid(2);
        if (!rest.isEmpty() && rest.at(0) >= '0' && rest.at(0) <= '9')
            rest = rest.mid(1);
        bool ok = false;
        const double az = QString::fromLatin1(rest).toDouble(&ok);
        if (ok)
            heading_ = clampRt21Azimuth(az);
        return;
    }
    if (cmd.startsWith("C0") || cmd.startsWith("C1"))
        return;
    if (cmd.startsWith("AA")) {
        heading_ = clampRt21Azimuth(heading_ - 1.0);
        return;
    }
    if (cmd.startsWith("AB")) {
        heading_ = clampRt21Azimuth(heading_ + 1.0);
        return;
    }
}

void Rt21Simulator::advance(double seconds)
{
    if (!moving_ || frozen_ || seconds <= 0)
        return;
    double delta = std::remainder(target_ - heading_, 360.0);
    const double step = std::clamp(delta, -speed_ * seconds, speed_ * seconds);
    heading_ = clampRt21Azimuth(heading_ + step);
    if (std::abs(std::remainder(target_ - heading_, 360.0)) < 0.15) {
        heading_ = target_;
        moving_ = false;
    }
}

MockRt21Server::MockRt21Server(QObject *parent)
    : QObject(parent)
    , server(new QTcpServer(this))
{
    motion = new QTimer(this);
    motion->setInterval(50);
    connect(motion, &QTimer::timeout, this, [this]() { sim_.advance(0.05); });
    motion->start();
    connect(server, &QTcpServer::newConnection, this, &MockRt21Server::acceptOne);
    connect(&sim_, &Rt21Simulator::output, this, [this](const QByteArray &bytes) {
        if (client)
            client->write(bytes);
#ifdef Q_OS_UNIX
        if (ptyMaster >= 0) {
            const ssize_t wrote = ::write(ptyMaster, bytes.constData(), size_t(bytes.size()));
            Q_UNUSED(wrote);
        }
#endif
    });
}

bool MockRt21Server::listenTcp(quint16 port)
{
    if (server->isListening())
        server->close();
    if (!server->listen(QHostAddress::LocalHost, port))
        return false;
    tcpPort = server->serverPort();
    return true;
}

quint16 MockRt21Server::port() const
{
    return tcpPort;
}

void MockRt21Server::acceptOne()
{
    if (client) {
        while (server->hasPendingConnections()) {
            QTcpSocket *extra = server->nextPendingConnection();
            extra->abort();
            extra->deleteLater();
        }
        return;
    }
    client = server->nextPendingConnection();
    server->close();
    connect(client, &QTcpSocket::readyRead, this, [this]() {
        if (client)
            sim_.feed(client->readAll());
    });
    connect(client, &QTcpSocket::disconnected, this, &MockRt21Server::clientGone);
}

void MockRt21Server::clientGone()
{
    if (client) {
        client->deleteLater();
        client = nullptr;
    }
    if (!server->listen(QHostAddress::LocalHost, tcpPort))
        server->listen(QHostAddress::LocalHost, 0);
    tcpPort = server->serverPort();
}

QString MockRt21Server::listenPty()
{
#ifdef Q_OS_UNIX
    if (ptyMaster >= 0)
        return ptyName;
    int master = -1;
    int slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0)
        return {};
    termios tio{};
    if (tcgetattr(master, &tio) == 0) {
        cfmakeraw(&tio);
        tcsetattr(master, TCSANOW, &tio);
    }
    ptyMaster = master;
    ptyName = QString::fromLocal8Bit(ttyname(slave));
    ::close(slave);
    ptyNotifier = new QSocketNotifier(ptyMaster, QSocketNotifier::Read, this);
    connect(ptyNotifier, &QSocketNotifier::activated, this, [this]() {
        char buf[512];
        const ssize_t n = ::read(ptyMaster, buf, sizeof buf);
        if (n > 0)
            sim_.feed(QByteArray(buf, int(n)));
    });
    return ptyName;
#else
    return {};
#endif
}
