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
#include <QFileInfo>
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
#  include <mstcpip.h>
#else
#  include <netinet/in.h>
#  include <netinet/tcp.h>
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
    if (endpoint.transport == RotorEndpoint::Transport::Serial)
        return endpoint.serialPort == next.serialPort && endpoint.baud == next.baud;
    return false;
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
        const QString moduleKey = inflight->moduleKey;
        const bool moduleOptional = inflight->moduleOptional;
        const bool moduleExit = inflight->moduleExit;
        const bool moduleRetried = inflight->moduleRetried;
        const QString expectToken = inflight->expectToken;
        const QByteArray bytes = inflight->bytes;
        const QString note = inflight->note;
        inflight.reset();
        if (kind == Kind::Module) {
            const bool handshake = expectToken == QLatin1String("OK") && moduleKey.isEmpty() && !moduleOptional && !moduleExit;
            if (handshake && networkTries < 2) {
                ++networkTries;
                TrafficLog::instance().recordSession("NOTE", QByteArray("network handshake retry"));
                startNetworkPass();
                return;
            }
            if (!moduleOptional && !moduleKey.isEmpty() && !moduleRetried && !moduleExit) {
                Job again;
                again.kind = Kind::Module;
                again.bytes = bytes;
                again.moduleKey = moduleKey;
                again.expectToken = expectToken;
                again.moduleRetried = true;
                QList<Job> rest;
                while (!queue.isEmpty())
                    rest.append(queue.dequeue());
                queue.enqueue(again);
                for (const Job &job : rest)
                    queue.enqueue(job);
                pump();
                return;
            }
            if (!moduleOptional && !moduleKey.isEmpty())
                emit networkField(moduleKey, QString());
            if (networkMode && queue.isEmpty())
                finishNetwork(!writeCompareFailed, writeCompareFailed
                                                         ? QStringLiteral("A setting did not read back as written.")
                                                         : QString());
            else
                pump();
            return;
        }
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
                emit configNote(note + QStringLiteral("no reply"));
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
        TrafficLog::instance().note(QStringLiteral("query timeout consecutive=%1").arg(failures));
        if (!lastQueryWasAi)
            useAiNext = true;
        if (failures >= 3) {
            failLink(QStringLiteral("No reply from the RT-21."));
            return;
        }
        if (awaitPrePoll) {
            if (QDateTime::currentMSecsSinceEpoch() >= commandDeadlineMs)
                dropCommand(QStringLiteral("RT-21 not connected — check settings and connections"));
            else
                sendQuery();
            return;
        }
        if (fastPace)
            sendQuery();
        else if (pollTimer)
            pollTimer->start(idlePollMs);
    });

    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &Rt21Driver::pollTick);

    reconnectTimer = new QTimer(this);
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, [this]() {
        if (retired || !wantLink)
            return;
        commandArmed = false;
        connectEndpoint(endpoint);
    });

    gapTimer = new QTimer(this);
    gapTimer->setSingleShot(true);
    connect(gapTimer, &QTimer::timeout, this, [this]() { sendArmedGoto(); });

    commandTimer = new QTimer(this);
    commandTimer->setSingleShot(true);
    connect(commandTimer, &QTimer::timeout, this, [this]() {
        if (pendingGoto && (awaitPrePoll || commandArmed || !transportOpen()))
            dropCommand(QStringLiteral("RT-21 not connected — check settings and connections"));
    });

    noteTimer = new QTimer(this);
    noteTimer->setSingleShot(true);
    connect(noteTimer, &QTimer::timeout, this, [this]() {
        note_.clear();
        emit statusNote(QString());
    });

    connectTimer = new QTimer(this);
    connectTimer->setSingleShot(true);
    connectTimer->setInterval(3000);
    connect(connectTimer, &QTimer::timeout, this, &Rt21Driver::onConnectTimeout);
}

int Rt21Driver::currentPollMs() const
{
    return fastPace ? fastPollMs : idlePollMs;
}

void Rt21Driver::setIdlePollSeconds(int seconds)
{
    idlePollMs = std::clamp(seconds, 30, 60) * 1000;
    if (!fastPace && pollTimer && pollTimer->isActive())
        pollTimer->start(idlePollMs);
}

void Rt21Driver::postNote(const QString &note)
{
    showNote(note);
}

void Rt21Driver::showNote(const QString &note)
{
    note_ = note;
    emit statusNote(note);
    noteTimer->start(4000);
}

void Rt21Driver::dropCommand(const QString &note)
{
    pendingGoto.reset();
    awaitPrePoll = false;
    commandArmed = false;
    gapTimer->stop();
    commandTimer->stop();
    if (target_) {
        target_.reset();
        emit targetChanged(0, false);
    }
    if (!note.isEmpty())
        showNote(note);
    TrafficLog::instance().note(QStringLiteral("dropped goto (%1)").arg(note));
}

bool Rt21Driver::transportOpen() const
{
    return (tcp && tcp->state() == QAbstractSocket::ConnectedState) || (serial && serial->isOpen());
}

void Rt21Driver::enableKeepAlive()
{
    if (!tcp)
        return;
    tcp->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    const qintptr fd = tcp->socketDescriptor();
    if (fd < 0)
        return;
#if defined(Q_OS_WIN)
    tcp_keepalive alive{};
    alive.onoff = 1;
    alive.keepalivetime = 5000;
    alive.keepaliveinterval = 2000;
    DWORD returned = 0;
    WSAIoctl(static_cast<SOCKET>(fd), SIO_KEEPALIVE_VALS, &alive, sizeof alive, nullptr, 0, &returned, nullptr,
             nullptr);
#else
    const int idle = 5;
    const int interval = 2;
    const int count = 3;
    ::setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    ::setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
    ::setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof count);
#endif
}

void Rt21Driver::sendQuery()
{
    if (!io || inflight || configMode || retired)
        return;
    Job job;
    job.kind = Kind::Heading;
    lastQueryWasAi = useAiNext;
    job.bytes = useAiNext ? Rt21::queryInteger() : Rt21::queryTenths();
    useAiNext = false;
    enqueue(job);
}

void Rt21Driver::sendArmedGoto()
{
    if (!pendingGoto || !transportOpen() || retired)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now >= commandDeadlineMs) {
        dropCommand(QStringLiteral("RT-21 not connected — check settings and connections"));
        return;
    }
    const qint64 wait = 500 - (now - lastApMs);
    if (lastApMs > 0 && wait > 0) {
        gapTimer->start(int(wait));
        return;
    }
    const double azimuth = *pendingGoto;
    pendingGoto.reset();
    awaitPrePoll = false;
    commandArmed = false;
    lastApMs = now;
    Job job;
    job.kind = Kind::Motion;
    job.bytes = Rt21::gotoImmediate(azimuth);
    enqueue(job);
    startFastPoll();
}

void Rt21Driver::startFastPoll()
{
    fastPace = true;
    fastSinceMs = QDateTime::currentMSecsSinceEpoch();
    stablePolls = 0;
    stableHeading = heading_.value_or(-999);
    if (pollTimer && !configMode)
        pollTimer->start(fastPollMs);
}

void Rt21Driver::startIdlePoll()
{
    fastPace = false;
    fastSinceMs = -1;
    if (pollTimer && io && !configMode)
        pollTimer->start(idlePollMs);
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
    if (gapTimer)
        gapTimer->stop();
    if (commandTimer)
        commandTimer->stop();
    if (connectTimer)
        connectTimer->stop();
    inflight.reset();
    queue.clear();
    framer.clear();
    if (tcp) {
        QTcpSocket *socket = tcp;
        tcp = nullptr;
        if (io == socket)
            io = nullptr;
        socket->disconnect(this);
        const auto state = socket->state();
        TrafficLog::instance().note(QStringLiteral("close %1 state=%2 error=%3")
                                        .arg(reason)
                                        .arg(int(state))
                                        .arg(socket->errorString()));
        const bool pending = state == QAbstractSocket::ConnectingState || state == QAbstractSocket::HostLookupState;
        if (pending) {
            // The handshake never finished. Abort cancels the SYN. An
            // established session is still shut down and closed, not aborted.
            socket->abort();
        } else {
            const qintptr fd = socket->socketDescriptor();
            if (fd >= 0) {
#if defined(Q_OS_WIN)
                ::shutdown(static_cast<SOCKET>(fd), SD_BOTH);
#else
                ::shutdown(static_cast<int>(fd), SHUT_RDWR);
#endif
            }
            if (state != QAbstractSocket::UnconnectedState)
                socket->disconnectFromHost();
            socket->close();
        }
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
    awaitPrePoll = false;
    commandArmed = false;
    reconnectTimer->stop();
    closeIo(QStringLiteral("driver retired"));
    setLink(Link::Off, QStringLiteral("Disconnected"));
}

void Rt21Driver::connectEndpoint(const RotorEndpoint &next)
{
    if (retired)
        return;
    if (!next.configured()) {
        wantLink = false;
        reconnectTimer->stop();
        closeIo(QStringLiteral("not configured"));
        setLink(Link::Off, QStringLiteral("Set up your RT-21 in settings"));
        return;
    }
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
            endAttempt(QStringLiteral("peer closed"));
            closeIo(QStringLiteral("peer closed"));
            failLink(err.isEmpty() ? QStringLiteral("The rotor closed the connection.") : err, false);
        });
        beginAttempt();
        tcp->connectToHost(endpoint.host, quint16(std::clamp(endpoint.tcpPort, 1, 65535)));
        if (tcp && connectTimer)
            connectTimer->start(3000);
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
    beginAttempt();
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
    backoffStep = 0;
    pendingGoto.reset();
    target_.reset();
    emit targetChanged(0, false);
    closeIo(QStringLiteral("disconnect"));
    moving = false;
    setLink(Link::Off, QStringLiteral("Disconnected"));
}

void Rt21Driver::beginAttempt()
{
    attemptStartedMs = QDateTime::currentMSecsSinceEpoch();
    const QString where = endpoint.transport == RotorEndpoint::Transport::Tcp
                              ? QStringLiteral("%1:%2").arg(endpoint.host).arg(endpoint.tcpPort)
                              : endpoint.serialPort;
    TrafficLog::instance().note(QStringLiteral("connect attempt start %1").arg(where));
}

void Rt21Driver::endAttempt(const QString &why)
{
    if (connectTimer)
        connectTimer->stop();
    if (attemptStartedMs <= 0)
        return;
    const qint64 duration = QDateTime::currentMSecsSinceEpoch() - attemptStartedMs;
    attemptStartedMs = 0;
    TrafficLog::instance().note(QStringLiteral("connect attempt ended after %1 ms (%2)").arg(duration).arg(why));
}

void Rt21Driver::onConnectTimeout()
{
    if (suppress || retired || !wantLink)
        return;
    if (!tcp || tcp->state() == QAbstractSocket::ConnectedState)
        return;
    endAttempt(QStringLiteral("connect timeout"));
    closeIo(QStringLiteral("connect timeout"));
    failLink(QStringLiteral("Connection timed out"), true);
}

void Rt21Driver::onConnected()
{
    endAttempt(QStringLiteral("connected"));
    failures = 0;
    downSinceMs = -1;
    backoffStep = 0;
    linkDownOutage = false;
    const QString where = endpoint.transport == RotorEndpoint::Transport::Tcp
                              ? QStringLiteral("Connected to %1:%2").arg(endpoint.host).arg(endpoint.tcpPort)
                              : QStringLiteral("Connected on %1").arg(endpoint.serialPort);
    TrafficLog::instance().note(where);
    enableKeepAlive();
    setLink(Link::On, where);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (commandArmed && pendingGoto && now < commandDeadlineMs) {
        // This is the attempt the click started. Ask for a heading first,
        // then send the goto once. A later reconnect will not find it armed.
        commandArmed = false;
        awaitPrePoll = true;
    } else {
        pendingGoto.reset();
        commandArmed = false;
        awaitPrePoll = false;
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
    const bool linkDown = !tcp || tcp->error() != QAbstractSocket::RemoteHostClosedError;
    TrafficLog::instance().note(QStringLiteral("socket error %1").arg(message));
    endAttempt(message);
    closeIo(message);
    failLink(message, linkDown);
}

void Rt21Driver::failLink(const QString &message, bool linkDown)
{
    const bool droppedNetwork = networkMode;
    pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    if (droppedNetwork) {
        networkMode = false;
        configMode = false;
        framer.setLineMode(false);
        emit networkFinished(false, QStringLiteral(
            "The link dropped while talking to the network module. T11 leaves TCP mode, so use USB if you can."));
    }
    pendingGoto.reset();
    awaitPrePoll = false;
    commandArmed = false;
    if (gapTimer)
        gapTimer->stop();
    if (commandTimer)
        commandTimer->stop();
    fastPace = false;
    moving = false;
    if (link_ != Link::Failed || detail_ != message)
        setLink(Link::Failed, message);
    linkDownOutage = linkDown;
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
    const int delay = reconnectDelayMs(down, backoffStep, linkDownOutage);
    const bool fastLink = linkDownOutage && down < 2LL * 60 * 1000;
    const bool firstQuiet = !linkDownOutage && backoffStep <= 0;
    if (firstQuiet)
        backoffStep = 1;
    else if (!fastLink)
        ++backoffStep;
    TrafficLog::instance().note(QStringLiteral("reconnect in %1 ms after %2 ms down (%3, %4)")
                                    .arg(delay)
                                    .arg(down)
                                    .arg(linkDownOutage ? QStringLiteral("link down") : QStringLiteral("session closed"))
                                    .arg(detail_));
    reconnectTimer->start(delay);
}

void Rt21Driver::onReadyRead()
{
    if (!io)
        return;
    const QByteArray received = io->readAll();
    if (networkMode)
        TrafficLog::instance().recordSession("NET-RX", received);
    else
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
        TrafficLog::instance().note(QStringLiteral("unparsed %1").arg(QString::fromLatin1(frame.toHex(' '))));
        return;
    }

    if (inflight && inflight->kind == Kind::Module) {
        QString line;
        for (unsigned char c : frame) {
            if (c >= 32 && c < 127)
                line.append(QChar(c));
            else if (!line.isEmpty() && !line.endsWith(QLatin1Char(' ')))
                line.append(QLatin1Char(' '));
        }
        line = line.trimmed();
        if (line.isEmpty())
            return;
        const bool okLine = line.contains(QStringLiteral("OK"), Qt::CaseInsensitive);
        const int tokenAt = inflight->expectToken.isEmpty() || inflight->expectToken == QLatin1String("OK")
                                ? -1
                                : line.indexOf(inflight->expectToken, 0, Qt::CaseInsensitive);
        const bool token = tokenAt >= 0;
        const bool matched = inflight->expectToken == QLatin1String("OK") ? okLine : token;
        if (!matched) {
            if (line.startsWith(QLatin1String("AT"), Qt::CaseInsensitive))
                return;
            return;
        }
        QString value = line;
        if (token)
            value = line.mid(tokenAt + inflight->expectToken.size()).trimmed().section(QLatin1Char(' '), 0, 0);
        if (!inflight->expectValue.isEmpty() && value != inflight->expectValue) {
            writeCompareFailed = true;
            for (int i = queue.size() - 1; i >= 0; --i) {
                if (!queue.at(i).moduleExit)
                    queue.removeAt(i);
            }
        } else if (!inflight->moduleKey.isEmpty()) {
            emit networkField(inflight->moduleKey, value);
        }
        const bool done = networkMode && queue.isEmpty();
        finishInflight();
        if (done && !inflight)
            finishNetwork(!writeCompareFailed, writeCompareFailed
                                                   ? QStringLiteral("A setting did not read back as written.")
                                                   : QString());
        return;
    }

    if (inflight && inflight->kind == Kind::Read) {
        failures = 0;
        configTimeouts = 0;
        const QString raw = Rt21::payloadAfterSoh(frame);
        const auto number = Rt21::parseIntegerPayload(frame);
        const QString wanted = inflight->expectValue;
        const QChar readId = inflight->id;
        if (!wanted.isEmpty()) {
            const QString got = number ? QString::number(*number) : raw;
            if (got != wanted) {
                writeCompareFailed = true;
                queue.clear();
                finishInflight();
                configMode = false;
                emit writeFinished(false, QStringLiteral("Read-back for %1 was %2, not %3.").arg(QString(readId), got, wanted));
                startPoll();
                return;
            }
        }
        emit configField(readId, raw, number.has_value(), number.value_or(0));
        finishInflight();
        if (configMode && !inflight && queue.isEmpty()) {
            configMode = false;
            if (!wanted.isEmpty())
                emit writeFinished(true, QStringLiteral("Read back the same value."));
            else
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
    else if (const auto heading = Rt21::parseHeadingFrame(frame))
        noteArrival(*heading, false, -1);
    else if (!Rt21::isCommandEcho(frame) && !frame.trimmed().isEmpty())
        TrafficLog::instance().note(QStringLiteral("unparsed %1").arg(QString::fromLatin1(frame.toHex(' '))));
}

void Rt21Driver::finishInflight()
{
    replyTimer->stop();
    inflight.reset();
    pump();
}

void Rt21Driver::noteArrival(double azimuth, bool haveStatus, int status)
{
    failures = 0;
    lastReplyMs = QDateTime::currentMSecsSinceEpoch();
    const bool changed = !heading_ || std::abs(*heading_ - azimuth) > 0.05;
    const bool delta = heading_ && std::abs(*heading_ - azimuth) > 0.15;
    heading_ = azimuth;
    if (changed)
        emit headingChanged(azimuth);

    const bool turning = haveStatus && status == 1;
    const bool stopped = haveStatus && status == 0;
    bool nowMoving = haveStatus ? turning : delta;
    if (target_) {
        const double gap = std::abs(std::remainder(azimuth - *target_, 360.0));
        if (gap <= 0.8 && !turning) {
            target_.reset();
            emit targetChanged(0, false);
            nowMoving = false;
        } else if (!stopped) {
            nowMoving = true;
        }
    }
    moving = nowMoving;
    if (link_ == Link::On || link_ == Link::Moving)
        setLink(moving ? Link::Moving : Link::On, detail_);
    lastHeading = azimuth;

    const bool launching = awaitPrePoll && pendingGoto;
    if (launching)
        sendArmedGoto();
    if (launching || !fastPace)
        return;
    if (turning) {
        stablePolls = 0;
        stableHeading = azimuth;
    } else if (std::abs(azimuth - stableHeading) <= 0.5) {
        ++stablePolls;
    } else {
        stablePolls = 1;
        stableHeading = azimuth;
    }
    const bool stable = stablePolls >= 3;
    const bool expired = fastSinceMs >= 0 && lastReplyMs - fastSinceMs > 120000;
    if (stopped || stable || expired)
        startIdlePoll();
}

void Rt21Driver::startPoll()
{
    if (!io || configMode)
        return;
    pollsSinceStatus = 0;
    pollTimer->start(fastPace ? fastPollMs : idlePollMs);
    pollTick();
}

void Rt21Driver::pollTick()
{
    if (!io || inflight || configMode)
        return;
    sendQuery();
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
    if (networkMode)
        TrafficLog::instance().recordSession("NET-TX", inflight->bytes);
    else
        TrafficLog::instance().record("TX", inflight->bytes);
    io->write(inflight->bytes);
    int wait = 1200;
    if (inflight->kind == Kind::Motion || inflight->kind == Kind::Write)
        wait = 60;
    else if (inflight->kind == Kind::Module)
        wait = inflight->moduleOptional ? 250 : 800;
    replyTimer->start(wait);
}

void Rt21Driver::goTo(double azimuth)
{
    const double clamped = clampRt21Azimuth(azimuth);
    pendingGoto = clamped;
    target_ = clamped;
    emit targetChanged(clamped, true);
    moving = true;
    commandDeadlineMs = QDateTime::currentMSecsSinceEpoch() + 3000;
    commandTimer->start(3000);
    if (link_ == Link::On || link_ == Link::Moving)
        setLink(Link::Moving, detail_);
    if (!transportOpen()) {
        if (!endpoint.configured()) {
            showNote(QStringLiteral("Set up your RT-21 in settings"));
            pendingGoto.reset();
            target_.reset();
            emit targetChanged(0, false);
            return;
        }
        showNote(QStringLiteral("RT-21 not connected — check settings and connections"));
        commandArmed = true;
        awaitPrePoll = false;
        if (wantLink && link_ != Link::Connecting)
            connectEndpoint(endpoint);
        return;
    }
    commandArmed = false;
    awaitPrePoll = true;
    if (!inflight)
        sendQuery();
}

void Rt21Driver::stopMotion()
{
    pendingGoto.reset();
    awaitPrePoll = false;
    commandArmed = false;
    gapTimer->stop();
    commandTimer->stop();
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
    if (link_ == Link::Moving || link_ == Link::On)
        setLink(Link::On, detail_);
    if (transportOpen())
        startFastPoll();
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
    Job magnetic;
    magnetic.kind = Kind::Read;
    magnetic.id = QLatin1Char('[');
    magnetic.bytes = Rt21::readEe(QLatin1Char('['));
    queue.enqueue(magnetic);
    pump();
}

void Rt21Driver::enqueueModule(const Rt21::ModuleQuery &query)
{
    Job job;
    job.kind = Kind::Module;
    job.bytes = query.command;
    job.moduleKey = query.key;
    job.expectToken = query.token;
    job.moduleOptional = query.optional;
    job.moduleExit = query.exitStep;
    queue.enqueue(job);
}

void Rt21Driver::finishNetwork(bool ok, const QString &message)
{
    const bool compared = writeCompareFailed;
    networkMode = false;
    configMode = false;
    writeCompareFailed = false;
    framer.setLineMode(false);
    emit networkFinished(ok, message);
    if (compared)
        emit writeFinished(false, message.isEmpty() ? QStringLiteral("A setting did not read back as written.") : message);
    startPoll();
}

void Rt21Driver::startNetworkPass()
{
    queue.clear();
    if (inflight && inflight->kind == Kind::Module)
        inflight.reset();
    replyTimer->stop();
    framer.clear();
    framer.setLineMode(true);
    Job clear;
    clear.kind = Kind::Module;
    clear.bytes = QByteArray(";");
    clear.moduleOptional = true;
    queue.enqueue(clear);
    for (const Rt21::ModuleQuery &query : Rt21::networkReadSequence())
        enqueueModule(query);
    pump();
}

void Rt21Driver::readNetwork()
{
    if (!io) {
        emit networkFinished(false, QStringLiteral("Connect to a rotor before reading the network module."));
        return;
    }
    configMode = true;
    networkMode = true;
    networkTries = 0;
    writeCompareFailed = false;
    configTimeouts = 0;
    pollTimer->stop();
    TrafficLog::instance().recordSession("NOTE", QByteArray("network read start"));
    startNetworkPass();
}

void Rt21Driver::applySetupGroup(const QString &group, const QMap<QString, QString> &fields, const QString &backupPath)
{
#if MERIDIAN_ALPHA_READONLY
    Q_UNUSED(group);
    Q_UNUSED(fields);
    Q_UNUSED(backupPath);
    emit writeFinished(false, QStringLiteral("Writes are off in this alpha. Nothing was sent."));
#else
    if (!Rt21::firmwareWritesAllowed(fields.value(QStringLiteral("firmware")))) {
        emit writeFinished(false, QStringLiteral(
            "Writes stay off unless the rotor reports 4.14.g1, the only firmware this program has been tested with."));
        return;
    }
    if (!QFileInfo::exists(backupPath)) {
        emit writeFinished(false, QStringLiteral("Save a copy of the settings before writing."));
        return;
    }
    QList<Rt21::PlannedWrite> plan;
    const QString error = Rt21::planGroup(group, fields, &plan);
    if (!error.isEmpty()) {
        emit writeFinished(false, error);
        return;
    }
    if (!io) {
        emit writeFinished(false, QStringLiteral("Connect to a rotor before writing."));
        return;
    }
    const bool module = !plan.isEmpty() && plan.first().module;
    configMode = true;
    networkMode = module;
    writeCompareFailed = false;
    if (module)
        framer.setLineMode(true);
    pollTimer->stop();
    queue.clear();
    if (module) {
        for (const Rt21::ModuleQuery &query : Rt21::networkReadSequence()) {
            if (!query.exitStep && query.key.isEmpty())
                enqueueModule(query);
        }
    }
    for (const Rt21::PlannedWrite &step : plan) {
        Job write;
        write.kind = step.module ? Kind::Module : Kind::Write;
        write.bytes = step.command;
        write.expectToken = step.module ? QStringLiteral("OK") : QString();
        queue.enqueue(write);
        Job read;
        read.kind = step.module ? Kind::Module : Kind::Read;
        read.bytes = step.readBack;
        read.expectToken = step.token;
        read.expectValue = step.expectValue;
        if (!step.module && step.readBack.size() >= 2)
            read.id = QChar::fromLatin1(step.readBack.at(1));
        queue.enqueue(read);
    }
    if (module) {
        for (const Rt21::ModuleQuery &query : Rt21::networkReadSequence()) {
            if (query.exitStep)
                enqueueModule(query);
        }
    }
    pump();
#endif
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
