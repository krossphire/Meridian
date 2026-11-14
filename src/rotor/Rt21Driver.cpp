/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Driver.h"

#include "Reconnect.h"
#include "TrafficLog.h"
#include "core/Bearing.h"

#include <QDateTime>
#include <QSerialPort>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <cmath>

#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#endif

namespace {

int g_alive = 0;
int g_transports = 0;
int g_transportHighWater = 0;

void trackTransport(int delta)
{
    g_transports += delta;
    if (g_transports > g_transportHighWater)
        g_transportHighWater = g_transports;
}

} // namespace

int Rt21Driver::instanceCount()
{
    return g_alive;
}

int Rt21Driver::transportHighWater()
{
    return g_transportHighWater;
}

int Rt21Driver::transportsOpen()
{
    return g_transports;
}

void Rt21Driver::resetDiagnostics()
{
    g_transportHighWater = g_transports;
}

bool Rt21Driver::serves(const RotorEndpoint &next) const
{
    if (!bound || retired)
        return false;
    if (endpoint.transport != next.transport)
        return false;
    if (endpoint.transport == RotorEndpoint::Transport::Tcp)
        return endpoint.host == next.host && endpoint.tcpPort == next.tcpPort;
    return endpoint.serialPort == next.serialPort && endpoint.baud == next.baud;
}

Rt21Driver::Rt21Driver(QObject *parent)
    : QObject(parent)
{
    ++g_alive;
    replyTimer = new QTimer(this);
    replyTimer->setSingleShot(true);
    connect(replyTimer, &QTimer::timeout, this, [this]() {
        if (!inflight)
            return;
        const Kind kind = inflight->kind;
        const QChar id = inflight->id;
        inflight.reset();
        if (kind == Kind::Motion || kind == Kind::Write) {
            if (kind == Kind::Write && queue.isEmpty() && configMode) {
                configMode = false;
                emit writeFinished(true, QStringLiteral("Settings sent."));
                startPoll();
            }
            pump();
            return;
        }
        if (kind == Kind::Read || kind == Kind::Note) {
            ++configTimeouts;
            if (kind == Kind::Read)
                emit configField(id, QStringLiteral("timeout"), false, 0);
            else
                emit configNote(QStringLiteral("No reply"));
            if (configTimeouts >= 3) {
                queue.clear();
                configMode = false;
                emit configFinished(false, QStringLiteral("The rotor stopped answering configuration reads."));
                startPoll();
                return;
            }
            pump();
            if (configMode && !inflight && queue.isEmpty()) {
                configMode = false;
                emit configFinished(true, QString());
                startPoll();
            }
            return;
        }
        ++failures;
        if (failures >= 3)
            failLink(QStringLiteral("No reply from the RT-21."));
        else
            pump();
    });

    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &Rt21Driver::pollTick);

    reconnectTimer = new QTimer(this);
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, [this]() {
        if (retired || !wantLink)
            return;
        connectEndpoint(endpoint);
    });
}

Rt21Driver::~Rt21Driver()
{
    retire();
    --g_alive;
}

void Rt21Driver::setLink(Link state, const QString &detail)
{
    if (link_ == state && detail_ == detail)
        return;
    link_ = state;
    detail_ = detail;
    emit linkChanged(state, detail);
}

void Rt21Driver::tearDownSocket()
{
    closeIo(QStringLiteral("teardown"));
}

void Rt21Driver::closeIo(const QString &reason)
{
    ++suppress;
    replyTimer->stop();
    pollTimer->stop();
    inflight.reset();
    queue.clear();
    framer.clear();
    if (tcp) {
        QTcpSocket *socket = tcp;
        tcp = nullptr;
        if (io == socket)
            io = nullptr;
        socket->disconnect(this);
        TrafficLog::instance().note(QStringLiteral("close %1 state=%2 error=%3")
                                        .arg(reason)
                                        .arg(int(socket->state()))
                                        .arg(socket->errorString()));
        const qintptr fd = socket->socketDescriptor();
        if (fd >= 0) {
#if defined(Q_OS_WIN)
            ::shutdown(static_cast<SOCKET>(fd), SD_BOTH);
#else
            ::shutdown(static_cast<int>(fd), SHUT_RDWR);
#endif
        }
        if (socket->state() != QAbstractSocket::UnconnectedState)
            socket->disconnectFromHost();
        socket->close();
        socket->setParent(nullptr);
        socket->deleteLater();
        trackTransport(-1);
    }
    if (serial) {
        QSerialPort *port = serial;
        serial = nullptr;
        if (io == port)
            io = nullptr;
        port->disconnect(this);
        TrafficLog::instance().note(QStringLiteral("close %1 serial error=%2").arg(reason, port->errorString()));
        if (port->isOpen())
            port->close();
        port->setParent(nullptr);
        port->deleteLater();
        trackTransport(-1);
    }
    io = nullptr;
    --suppress;
}

void Rt21Driver::retire()
{
    if (retired)
        return;
    retired = true;
    wantLink = false;
    pendingGoto.reset();
    reconnectTimer->stop();
    closeIo(QStringLiteral("driver retired"));
    setLink(Link::Off, QStringLiteral("Disconnected"));
}

void Rt21Driver::connectEndpoint(const RotorEndpoint &next)
{
    if (retired)
        return;
    reconnectTimer->stop();
    endpoint = next;
    bound = true;
    wantLink = true;
    closeIo(QStringLiteral("new connection"));
    failures = 0;
    setLink(Link::Connecting, endpoint.transport == RotorEndpoint::Transport::Tcp
                                  ? QStringLiteral("Connecting to %1:%2").arg(endpoint.host).arg(endpoint.tcpPort)
                                  : QStringLiteral("Opening %1").arg(endpoint.serialPort));

    if (endpoint.transport == RotorEndpoint::Transport::Tcp) {
        tcp = new QTcpSocket(this);
        trackTransport(1);
        io = tcp;
        tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(tcp, &QTcpSocket::connected, this, &Rt21Driver::onConnected);
        connect(tcp, &QTcpSocket::readyRead, this, &Rt21Driver::onReadyRead);
        connect(tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { onSocketError(); });
        connect(tcp, &QTcpSocket::disconnected, this, [this]() {
            if (suppress || retired || !wantLink || !tcp)
                return;
            const QString err = tcp->errorString();
            closeIo(QStringLiteral("peer closed"));
            failLink(err.isEmpty() ? QStringLiteral("The rotor closed the connection.") : err);
        });
        tcp->connectToHost(endpoint.host, quint16(std::clamp(endpoint.tcpPort, 1, 65535)));
        return;
    }

    serial = new QSerialPort(this);
    trackTransport(1);
    io = serial;
    serial->setPortName(endpoint.serialPort);
    serial->setBaudRate(endpoint.baud > 0 ? endpoint.baud : 4800);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);
    connect(serial, &QSerialPort::readyRead, this, &Rt21Driver::onReadyRead);
    connect(serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError)
            return;
        onSocketError();
    });
    const bool opened = serial && serial->open(QIODevice::ReadWrite);
    if (!opened || !serial) {
        const QString message = (serial && !serial->errorString().isEmpty())
                                    ? serial->errorString()
                                    : QStringLiteral("Couldn't open %1.").arg(endpoint.serialPort);
        if (serial)
            closeIo(message);
        failLink(message);
        return;
    }
    onConnected();
}

void Rt21Driver::disconnectLink()
{
    if (retired)
        return;
    wantLink = false;
    reconnectTimer->stop();
    downSinceMs = -1;
    postGrace = 0;
    pendingGoto.reset();
    target_.reset();
    emit targetChanged(0, false);
    closeIo(QStringLiteral("disconnect"));
    moving = false;
    setLink(Link::Off, QStringLiteral("Disconnected"));
}

void Rt21Driver::onConnected()
{
    failures = 0;
    downSinceMs = -1;
    postGrace = 0;
    const QString where = endpoint.transport == RotorEndpoint::Transport::Tcp
                              ? QStringLiteral("Connected to %1:%2").arg(endpoint.host).arg(endpoint.tcpPort)
                              : QStringLiteral("Connected on %1").arg(endpoint.serialPort);
    TrafficLog::instance().note(where);
    setLink(Link::On, where);
    if (pendingGoto) {
        // One shot, for a turn the user asked for while this connect was
        // already in progress. Cleared here so a later reconnect cannot
        // send the same AP again.
        Job job;
        job.kind = Kind::Motion;
        job.bytes = Rt21::gotoImmediate(*pendingGoto);
        pendingGoto.reset();
        queue.prepend(job);
    }
    startPoll();
    pump();
}

void Rt21Driver::onSocketError()
{
    if (suppress || retired || !wantLink)
        return;
    QString message;
    if (tcp) {
        if (tcp->error() == QAbstractSocket::ConnectionRefusedError) {
            message = QStringLiteral(
                "Connection refused. The RT-21 TCP port accepts one client; another program may already be connected.");
        } else {
            message = tcp->errorString();
        }
    } else if (serial) {
        message = serial->errorString();
    }
    if (message.isEmpty())
        message = QStringLiteral("Connection failed.");
    TrafficLog::instance().note(QStringLiteral("socket error %1").arg(message));
    closeIo(message);
    failLink(message);
}

void Rt21Driver::failLink(const QString &message)
{
    pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    pendingGoto.reset();
    moving = false;
    if (link_ != Link::Failed || detail_ != message)
        setLink(Link::Failed, message);
    if (wantLink)
        scheduleReconnect();
}

void Rt21Driver::scheduleReconnect()
{
    if (retired || !wantLink)
        return;
    if (downSinceMs < 0)
        downSinceMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 down = QDateTime::currentMSecsSinceEpoch() - downSinceMs;
    const int delay = reconnectDelayMs(down, postGrace);
    if (down >= 5LL * 60 * 1000)
        ++postGrace;
    TrafficLog::instance().note(QStringLiteral("reconnect in %1 ms after %2 ms down (%3)")
                                    .arg(delay)
                                    .arg(down)
                                    .arg(detail_));
    reconnectTimer->start(delay);
}

void Rt21Driver::onReadyRead()
{
    if (!io)
        return;
    const QByteArray received = io->readAll();
    TrafficLog::instance().record("RX", received);
    framer.append(received);
    const QList<QByteArray> frames = framer.take();
    for (const QByteArray &frame : frames)
        handleFrame(frame);
}

void Rt21Driver::handleFrame(const QByteArray &frame)
{
    const QByteArray upper = frame.toUpper();
    if (upper == "BI1" || upper == "AI1" || upper == "R21" || upper == "R11" || upper == "R01" || upper == "R41")
        return;

    if (inflight && (inflight->kind == Kind::Heading || inflight->kind == Kind::Status)) {
        if (const auto status = Rt21::parseStatusFrame(frame)) {
            failures = 0;
            noteArrival(status->azimuth, status->haveStatus, status->status);
            finishInflight();
            return;
        }
        if (const auto heading = Rt21::parseHeadingFrame(frame)) {
            failures = 0;
            noteArrival(*heading, false, -1);
            finishInflight();
            return;
        }
        if (Rt21::isCommandEcho(frame))
            return;
        return;
    }

    if (inflight && inflight->kind == Kind::Read) {
        failures = 0;
        configTimeouts = 0;
        const QString raw = Rt21::payloadAfterSoh(frame);
        const auto number = Rt21::parseIntegerPayload(frame);
        emit configField(inflight->id, raw, number.has_value(), number.value_or(0));
        finishInflight();
        if (configMode && !inflight && queue.isEmpty()) {
            configMode = false;
            emit configFinished(true, QString());
            startPoll();
        }
        return;
    }

    if (inflight && inflight->kind == Kind::Note) {
        failures = 0;
        configTimeouts = 0;
        emit configNote(inflight->note + Rt21::payloadAfterSoh(frame));
        finishInflight();
        return;
    }

    if (const auto status = Rt21::parseStatusFrame(frame))
        noteArrival(status->azimuth, status->haveStatus, status->status);
    else if (const auto heading = Rt21::parseHeading(frame))
        noteArrival(*heading, false, -1);
}

void Rt21Driver::finishInflight()
{
    replyTimer->stop();
    inflight.reset();
    pump();
}

void Rt21Driver::noteArrival(double azimuth, bool haveStatus, int status)
{
    const bool changed = !heading_ || std::abs(*heading_ - azimuth) > 0.05;
    const bool delta = heading_ && std::abs(*heading_ - azimuth) > 0.15;
    heading_ = azimuth;
    if (changed)
        emit headingChanged(azimuth);

    bool nowMoving = delta;
    if (haveStatus)
        nowMoving = status == 1;
    if (target_) {
        const double gap = std::abs(std::remainder(azimuth - *target_, 360.0));
        if (gap <= 0.8 && !delta && !(haveStatus && status == 1)) {
            target_.reset();
            emit targetChanged(0, false);
            nowMoving = false;
        } else if (gap > 0.8) {
            nowMoving = true;
        }
    }
    moving = nowMoving;
    if (link_ == Link::On || link_ == Link::Moving)
        setLink(moving ? Link::Moving : Link::On, detail_);
    lastHeading = azimuth;

    const int interval = moving ? 250 : 2000;
    if (pollTimer->interval() != interval && pollTimer->isActive())
        pollTimer->start(interval);
}

void Rt21Driver::startPoll()
{
    if (!io || configMode)
        return;
    pollsSinceStatus = 0;
    pollTimer->start(moving || target_ ? 250 : 2000);
    pollTick();
}

void Rt21Driver::pollTick()
{
    if (!io || inflight || configMode)
        return;
    Job job;
    if (!statusDisabled && ++pollsSinceStatus >= 4) {
        pollsSinceStatus = 0;
        job.kind = Kind::Status;
        job.bytes = Rt21::queryStatus();
    } else {
        job.kind = Kind::Heading;
        job.bytes = Rt21::queryTenths();
    }
    enqueue(job);
}

void Rt21Driver::enqueue(Job job)
{
    if (job.kind == Kind::Motion) {
        for (int i = queue.size() - 1; i >= 0; --i) {
            if (queue.at(i).kind == Kind::Motion)
                queue.removeAt(i);
        }
    }
    queue.enqueue(job);
    pump();
}

void Rt21Driver::pump()
{
    if (inflight || queue.isEmpty() || !io)
        return;
    inflight = queue.dequeue();
    TrafficLog::instance().record("TX", inflight->bytes);
    io->write(inflight->bytes);
    const int wait = (inflight->kind == Kind::Motion || inflight->kind == Kind::Write) ? 60 : 1200;
    replyTimer->start(wait);
}

void Rt21Driver::goTo(double azimuth)
{
    const double clamped = clampRt21Azimuth(azimuth);
    pendingGoto = clamped;
    target_ = clamped;
    emit targetChanged(clamped, true);
    moving = true;
    if (link_ == Link::On || link_ == Link::Moving) {
        setLink(Link::Moving, detail_);
        if (pollTimer->isActive())
            pollTimer->start(250);
    }
    const bool transportOpen = (tcp && tcp->state() == QAbstractSocket::ConnectedState)
                               || (serial && serial->isOpen());
    if (!transportOpen) {
        if (wantLink && link_ != Link::Connecting && link_ != Link::On && link_ != Link::Moving)
            connectEndpoint(endpoint);
        return;
    }
    pendingGoto.reset();
    Job job;
    job.kind = Kind::Motion;
    job.bytes = Rt21::gotoImmediate(clamped);
    enqueue(job);
}

void Rt21Driver::stopMotion()
{
    pendingGoto.reset();
    target_.reset();
    emit targetChanged(0, false);
    moving = false;
    queue.clear();
    if (io) {
        const QByteArray stop = Rt21::stopCommand();
        TrafficLog::instance().record("TX", stop);
        io->write(stop);
        framer.clear();
        inflight.reset();
        replyTimer->stop();
    }
    if (link_ == Link::Moving)
        setLink(Link::On, detail_);
}

void Rt21Driver::readConfiguration()
{
    if (!io) {
        emit configFinished(false, QStringLiteral("Connect to a rotor before reading its setup."));
        return;
    }
    configMode = true;
    configTimeouts = 0;
    pollTimer->stop();
    queue.clear();

    Job version;
    version.kind = Kind::Note;
    version.bytes = Rt21::queryVersion();
    version.note = QStringLiteral("Version ");
    queue.enqueue(version);

    Job pot;
    pot.kind = Kind::Note;
    pot.bytes = Rt21::queryPot();
    pot.note = QStringLiteral("Pot A/D ");
    queue.enqueue(pot);

    Job reset;
    reset.kind = Kind::Note;
    reset.bytes = Rt21::queryReset();
    reset.note = QStringLiteral("Last reset ");
    queue.enqueue(reset);

    const QString letters = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    for (const QChar letter : letters) {
        Job job;
        job.kind = Kind::Read;
        job.id = letter;
        job.bytes = Rt21::readEe(letter);
        queue.enqueue(job);
    }
    pump();
}

void Rt21Driver::writeConfiguration(const QList<QPair<QChar, int>> &changes)
{
#if MERIDIAN_ALPHA_READONLY
    Q_UNUSED(changes);
    emit writeFinished(false, QStringLiteral("EEPROM writes are off in this alpha."));
    return;
#else
    if (!io) {
        emit writeFinished(false, QStringLiteral("Not connected."));
        return;
    }
    if (changes.isEmpty()) {
        emit writeFinished(true, QStringLiteral("Nothing to change."));
        return;
    }
    configMode = true;
    pollTimer->stop();
    for (const auto &change : changes) {
        Job job;
        job.kind = Kind::Write;
        job.id = change.first;
        job.bytes = Rt21::writeEe(change.first, change.second);
        queue.enqueue(job);
    }
    pump();
#endif
}

void Rt21Driver::sendCalibrate(bool clockwise)
{
#if MERIDIAN_ALPHA_READONLY
    Q_UNUSED(clockwise);
    TrafficLog::instance().note(QStringLiteral("ignored calibration write (alpha read-only)"));
    return;
#else
    if (!io)
        return;
    Job job;
    job.kind = Kind::Motion;
    job.bytes = Rt21::calibrateEndpoint(clockwise);
    enqueue(job);
#endif
}

void Rt21Driver::sendPulseHeading(double azimuth)
{
#if MERIDIAN_ALPHA_READONLY
    Q_UNUSED(azimuth);
    TrafficLog::instance().note(QStringLiteral("ignored pulse-heading write (alpha read-only)"));
    return;
#else
    if (!io)
        return;
    Job job;
    job.kind = Kind::Motion;
    job.bytes = Rt21::setPulseHeading(azimuth);
    enqueue(job);
#endif
}
