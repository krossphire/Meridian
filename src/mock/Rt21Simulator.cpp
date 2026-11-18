/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Simulator.h"

#include "core/Bearing.h"
#include "rotor/NetCycleProbe.h"

#include <QDateTime>
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
    module.clear();
    const QList<QPair<QByteArray, QByteArray>> net = {
        {"HWTYPE", "2"},
        {"DHCP", "0"},
        {"IP", "192.168.1.106"},
        {"AIP", "192.168.1.106"},
        {"NMASK", "255.255.255.0"},
        {"ANMASK", "255.255.255.0"},
        {"GW", "192.168.2.1"},
        {"AGW", "192.168.2.1"},
        {"ID", "RT-21"},
        {"HTTP", "801"},
        {"UDP", "23073"},
        {"TFTP", "69"},
        {"NVREG16_0", "6555"},
        {"WIFI_SECURITY", "6"},
        {"WIFI_ASSID", "shack-2g"},
        {"WIFI_KEY", "prairie-not-logged"},
        {"HTTP_USER", ""},
        {"HTTP_PASS", ""},
        {"LINK", "1"},
    };
    for (const auto &item : net)
        module.insert(item.first, item.second);
}

void Rt21Simulator::replyLine(const QByteArray &line)
{
    emit output(line + QByteArray("\r\n"));
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

void Rt21Simulator::setIgnoreQueries(bool ignore)
{
    ignoreQueries = ignore;
}

void Rt21Simulator::setIgnoreModule(bool ignore)
{
    ignoreModule = ignore;
}

void Rt21Simulator::clearTrace()
{
    trace_.clear();
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
        if (c == 0x1b) {
            atMode = false;
            trace_.append(QByteArray("ESC"));
            buffer.clear();
            sawCr = false;
            continue;
        }
        if (c == '\n') {
            if (buffer.startsWith("AT")) {
                handle(buffer, false);
                buffer.clear();
                sawCr = false;
            }
            continue;
        }
        if (c == '\r') {
            if (buffer.startsWith("AT")) {
                handle(buffer, false);
                buffer.clear();
                sawCr = false;
                continue;
            }
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
    if (!cmd.isEmpty())
        trace_.append(immediate ? cmd + QByteArray("\r") : cmd);
    if (ignoreModule && (cmd.startsWith("AT") || cmd.startsWith("T0") || cmd.startsWith("T1")))
        return;
    if ((cmd.startsWith("BI") || cmd.startsWith("AI")) && ignoreQueries)
        return;
    if (cmd.isEmpty() || cmd.startsWith("ST")) {
        moving_ = false;
        return;
    }
    if (cmd.startsWith("BI")) {
        // Healthy hardware replies are plain "NNN.N;" with no padding.
        // About every fifth frame the LAN module prefixes 01 30 xx 3E,
        // xx = 0x00 stopped, 0x01 rotating.
        const QByteArray heading = QString::number(heading_, 'f', 1).toLatin1();
        ++biReplies;
        if (biReplies % 5 == 0) {
            QByteArray frame;
            frame.append(char(0x01));
            frame.append(char(0x30));
            frame.append(char(moving_ ? 0x01 : 0x00));
            frame.append(char(0x3E));
            frame.append(heading);
            frame.append(';');
            emit output(frame);
            return;
        }
        reply(heading, false);
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
    auto echo = [&](char mark, const QByteArray &value) {
        // Same shape as firmware 4.14.g1: <echo><SOH><value>;
        QByteArray payload;
        payload.append(mark);
        payload.append(char(0x01));
        payload.append(value);
        reply(payload, false);
    };
    if (cmd.startsWith("R1")) {
        echo('1', QByteArray("RT-21 4.13.2"));
        return;
    }
    if (cmd.startsWith("R0")) {
        echo('0', QByteArray("0334"));
        return;
    }
    if (cmd.startsWith("R4")) {
        echo('4', QByteArray("0"));
        return;
    }
    if (cmd.startsWith("R[")) {
        echo('[', QByteArray("0"));
        return;
    }
    if (cmd.size() >= 2 && cmd.at(0) == 'R' && cmd.at(1) >= 'A' && cmd.at(1) <= 'Z') {
        const QChar id(cmd.at(1));
        echo(cmd.at(1), QByteArray::number(ee.value(id, 0)));
        return;
    }
    if (cmd.startsWith("W0")) {
        // W0...; is the message command. Meridian does not send it.
        return;
    }
    if (cmd.size() >= 3 && cmd.at(0) == 'W' && cmd.at(1) >= 'A' && cmd.at(1) <= 'Z'
        && cmd.at(2) >= '0' && cmd.at(2) <= '9') {
        const QChar id(cmd.at(1));
        QByteArray ack;
        ack.append(char(0x06));
        ack.append(cmd.at(1));
        ack.append(';');
        emit output(ack);
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
    if (cmd == "T11" || cmd == "T01" || cmd == "AT") {
        if (cmd == "AT") {
            atMode = true;
            uart2AtMs = QDateTime::currentMSecsSinceEpoch();
            echoSends = 0;
            echoErrorAtMs = 0;
            reply(QByteArray("<uart2>"), false);
            return;
        }
        if (cmd == "T01")
            atMode = false;
        replyLine(QByteArray("OK"));
        return;
    }
    if (cmd.startsWith("AT*")) {
        const int eq = cmd.indexOf('=');
        const QByteArray name = eq < 0 ? cmd.mid(3) : cmd.mid(3, eq - 3);
        QByteArray key = name;
        if (key.endsWith('?'))
            key.chop(1);
        auto stored = [&](const char *id) { return module.value(QByteArray(id)); };
        if (eq >= 0 && !cmd.endsWith('?')) {
            module.insert(key, cmd.mid(eq + 1));
            if (key == "SERIAL_ECHO") {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const qint64 sinceUart = now - uart2AtMs;
                const qint64 rose = networkDtrRoseProbeMs().load();
                const qint64 sinceRise = rose > 0 ? now - rose : 9999;
                ++echoSends;
                // Variant D echoes as soon as the port is open, about 520 ms plus
                // the close/open, so the first byte can land well before 700 ms.
                // A and B still land in the 700–760 ms window. Either way the
                // first echo is within a few milliseconds of DTR rising.
                const bool prompt = uart2AtMs > 0 && rose > 0 && sinceRise <= 40 && sinceUart >= 480
                                    && sinceUart <= 800;
                if (echoSends == 1 && prompt) {
                    echoErrorAtMs = now;
                    replyLine(QByteArray("ERROR 1"));
                } else if (echoSends >= 2 && echoSends <= 3 && echoErrorAtMs > 0 && now - echoErrorAtMs <= 250)
                    replyLine(QByteArray("OK"));
                else if (echoSends >= 2 && uart2AtMs > 0 && sinceUart >= 700 && sinceUart <= 900)
                    replyLine(QByteArray("OK"));
                return;
            }
            replyLine(QByteArray("OK"));
            return;
        }
        const QByteArray value = stored(key.constData());
        QByteArray prefix = key;
        if (key == "WIFI_SECURITY")
            prefix = "SECURITY";
        else if (key == "WIFI_ASSID")
            prefix = "ASSID";
        else if (key == "WIFI_KEY")
            prefix = "KEY";
        replyLine(prefix + "=" + value);
        return;
    }
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

bool MockRt21Server::hasClient() const
{
    return client && client->state() == QAbstractSocket::ConnectedState;
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
    ++accepts_;
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
        if (n > 0) {
            sim_.feed(QByteArray(buf, int(n)));
            return;
        }
        // Closing the slave makes the master readable with EIO. Park the
        // notifier briefly so the close/reopen in a network read cannot spin.
        ptyNotifier->setEnabled(false);
        QTimer::singleShot(40, this, [this]() {
            if (ptyNotifier)
                ptyNotifier->setEnabled(true);
        });
    });
    return ptyName;
#else
    return {};
#endif
}
