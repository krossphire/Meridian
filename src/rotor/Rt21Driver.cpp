/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Driver.h"

#include "StatusTone.h"

#include "Reconnect.h"
#include "SerialLines.h"
#include "TrafficLog.h"
#include "core/Bearing.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHostAddress>
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

QString hexOf(const QByteArray &bytes)
{
    QString out;
    for (unsigned char c : bytes) {
        if (!out.isEmpty())
            out += QLatin1Char(' ');
        out += QString::asprintf("%02X", c);
    }
    return out;
}

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
        const bool brightnessJob = inflight->brightness;
        inflight.reset();
        if (kind == Kind::Probe) {
            queue.clear();
            const QString message = noReplyMessage();
            const bool wasNetwork = probeForNetwork;
            configMode = false;
            networkMode = false;
            probeForNetwork = false;
            networkPhase = NetworkPhase::Idle;
            if (wasNetwork) {
                if (networkWriting) {
                    const QString name = networkWriteName;
                    networkWriting = false;
                    networkWriteName.clear();
                    emit writeFinished(false, QStringLiteral("Write not confirmed: %1").arg(name));
                } else {
                    emit networkFinished(false, message);
                }
            } else
                emit configFinished(false, message);
            startPoll();
            return;
        }
        if (kind == Kind::Module) {
            const bool handshake = note.isEmpty() && expectToken == QLatin1String("OK") && moduleKey.isEmpty()
                                   && !moduleOptional && !moduleExit;
            if (handshake && networkTries < 2) {
                ++networkTries;
                TrafficLog::instance().recordSession("NOTE", QByteArray("network handshake retry"));
                startNetworkPass();
                return;
            }
            if (handshake) {
                countingHandshakeBytes = false;
                if (!handshakeSawBytes) {
                    TrafficLog::instance().recordSession("NOTE", QByteArray("network handshake got no bytes"));
                    sendNetworkExit();
                    return;
                }
                networkHandshakeDone = true;
                enqueueNetworkQueries();
                pump();
                return;
            }
            if (note == QLatin1String("netwrite") && !moduleRetried) {
                Job again;
                again.kind = Kind::Module;
                again.bytes = bytes;
                again.expectToken = expectToken;
                again.note = note;
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
            if (note == QLatin1String("netwrite")) {
                dropNetworkQueries();
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
            if (!moduleOptional && !moduleKey.isEmpty() && !networkGotField && !networkWriteOk) {
                dropNetworkQueries();
                return;
            }
            if (networkMode && queue.isEmpty()) {
                const QString message = networkHandshakeDone
                                            ? (writeCompareFailed ? QStringLiteral("A setting did not read back as written.")
                                                                  : QString())
                                            : (handshakeSawBytes
                                                   ? QStringLiteral("The network module answered, but not with OK.")
                                                   : QStringLiteral("The network module did not answer."));
                finishNetwork(networkHandshakeDone && !writeCompareFailed, message);
            } else {
                pump();
            }
            return;
        }
        if (kind == Kind::Motion || kind == Kind::Write) {
            if (kind == Kind::Write && brightnessJob) {
                TrafficLog::instance().recordSession("NOTE", QByteArray("brightness ack missing"));
                QTimer::singleShot(150, this, [this]() { sendBrightnessRead(); });
                return;
            }
            if (kind == Kind::Write && queue.isEmpty() && configMode) {
                configMode = false;
                emit writeFinished(true, QStringLiteral("Settings sent."));
                startPoll();
            }
            pump();
            return;
        }
        if (kind == Kind::Read && brightnessJob) {
            beginBrightnessWatch(false, false, QString());
            return;
        }
        if (kind == Kind::Read && id == QLatin1Char('[')) {
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("magnetic variation not answered; not present on 4.13.2"));
            emit configField(id, QString(), false, 0);
            pump();
            if (configMode && !inflight && queue.isEmpty()) {
                configMode = false;
                emit configFinished(true, QString());
                startPoll();
            }
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
        if (brightnessWatching && (kind == Kind::Heading || kind == Kind::Status)) {
            TrafficLog::instance().recordSession("NOTE", QByteArray("brightness watch poll timed out"));
            return;
        }
        if (awaitHeadingAfterNetwork && note == QLatin1String("post-network")) {
            if (!networkHeadingRetried) {
                networkHeadingRetried = true;
                TrafficLog::instance().recordSession("NOTE", QByteArray("one missed reply after network exit, retrying quietly"));
                QTimer::singleShot(250, this, [this]() {
                    if (!awaitHeadingAfterNetwork || !io)
                        return;
                    Job again;
                    again.kind = Kind::Heading;
                    again.bytes = Rt21::queryTenths();
                    again.note = QStringLiteral("post-network");
                    queue.enqueue(again);
                    pump();
                });
                return;
            }
            afterNetworkHeading(false);
            return;
        }
        ++failures;
        if (failures == 1 && !fastPace && !networkMode && !configMode) {
            TrafficLog::instance().recordSession("NOTE", QByteArray("one missed reply, retrying quietly"));
            QTimer::singleShot(250, this, [this]() {
                if (!inflight && io && !networkMode && !configMode && !awaitHeadingAfterNetwork && !brightnessCapture)
                    sendQuery();
            });
            return;
        }
        TrafficLog::instance().note(QStringLiteral("query timeout consecutive=%1").arg(failures));
        if (!lastQueryWasAi)
            useAiNext = true;
        if (failures >= 3) {
            const QString saverHint = firmwareReport.contains(QStringLiteral("4.14"))
                                          ? QStringLiteral(" RT-21 not answering. If its screen saver is on, power-cycle it.")
                                          : QString();
            const QString message = endpoint.transport == RotorEndpoint::Transport::Serial
                                        ? QStringLiteral("%1 open, no reply from RT-21 — check port/baud or run Find.%2")
                                              .arg(endpoint.serialPort, saverHint)
                                        : QStringLiteral("No reply from the RT-21.");
            failLink(message);
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
    if (statusTone(note) == StatusTone::Failure)
        noteTimer->stop();
    else
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
    if (!io || inflight || configMode || networkMode || awaitHeadingAfterNetwork || retired)
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
    portCycling = false;
    networkPhase = NetworkPhase::Idle;
    if (networkTimer)
        networkTimer->stop();
    const bool networkBusy = networkMode || networkAnotherCycle || networkWriting || networkWritePending;
    const bool networkBusyWrite = networkWriting || networkWritePending;
    const QString networkBusyName = networkWriteName;
    networkReadOkThisSession = false;
    networkCancel = true;
    networkMode = false;
    networkAnotherCycle = false;
    networkWriting = false;
    networkWritePending = false;
    networkEntered = false;
    awaitHeadingAfterNetwork = false;
    pendingConfigAfterNetwork = false;
    networkWriteName.clear();
    if (networkBusy) {
        if (networkBusyWrite)
            emit writeFinished(false, QStringLiteral("Write not confirmed: %1").arg(networkBusyName));
        else
            emit networkFinished(false, QStringLiteral("The link closed during the network read."));
    }
    replyTimer->stop();
    pollTimer->stop();
    if (brightnessDeferTimer)
        brightnessDeferTimer->stop();
    if (brightnessQueued >= 0 && !brightnessCapture && !brightnessWatching && !brightnessHold) {
        brightnessQueued = -1;
        emit writeFinished(false, QStringLiteral("The link closed before the brightness write."));
    }
    brightnessHold = false;
    if (brightnessWatching || brightnessCapture)
        finishBrightnessWatch(true);
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
    // Match Setup Utility 4.B12: Handshake stays None, and both DTR and RTS
    // are turned on. .NET leaves them false; the utility sets both true
    // (InitializeComponent: set_DtrEnable 1, set_RtsEnable 1). With them low,
    // an Edgeport port opens and transmits but the RT-21 sends nothing back.
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
    bool dtr = false;
    bool rts = false;
    raiseDtrAndRts(serial, &dtr, &rts);
    serial->clear(QSerialPort::AllDirections);
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("serial open ") + endpoint.serialPort.toLatin1()
                    + " baud " + QByteArray::number(serial->baudRate())
                    + " 8N1 DTR " + (dtr ? "on" : "off") + " RTS " + (rts ? "on" : "off"));
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
    if (serial && (portCycling || networkMode
                   || QDateTime::currentMSecsSinceEpoch() < ignoreSerialErrorUntil)) {
        TrafficLog::instance().note(QStringLiteral("serial error during network pass ignored: %1").arg(serial->errorString()));
        return;
    }
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
    const bool droppedNetwork = networkMode || networkAnotherCycle || networkWriting || networkWritePending;
    const bool droppedWrite = networkWriting || networkWritePending;
    const QString droppedName = networkWriteName;
    pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    if (droppedNetwork) {
        networkMode = false;
        configMode = false;
        networkAnotherCycle = false;
        networkWriting = false;
        networkWritePending = false;
        networkReadOkThisSession = false;
        networkWriteName.clear();
        framer.setLineMode(false);
        if (droppedWrite)
            emit writeFinished(false, QStringLiteral("Write not confirmed: %1").arg(droppedName));
        else
            emit networkFinished(false, QStringLiteral("The link dropped while talking to the network module."));
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
    if (!io || (serial && !serial->isOpen()))
        return;
    const QByteArray received = io->readAll();
    if (countingHandshakeBytes && !received.isEmpty())
        handshakeSawBytes = true;
    if (brightnessCapture && !received.isEmpty())
        TrafficLog::instance().recordSession("WRITE-RX", received);
    if (serial)
        TrafficLog::instance().recordSession("SER-RX", received);
    else if (networkMode)
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

    if (handleNetworkFrame(frame))
        return;

    if (inflight && inflight->kind == Kind::Probe) {
        const auto status = Rt21::parseStatusFrame(frame);
        const auto heading = status ? std::optional<double>(status->azimuth) : Rt21::parseHeadingFrame(frame);
        if (heading) {
            failures = 0;
            noteArrival(*heading, status && status->haveStatus, status ? status->status : -1);
            const bool network = probeForNetwork;
            replyTimer->stop();
            inflight.reset();
            if (network)
                startNetworkPass();
            else {
                enqueueConfiguration();
                pump();
            }
            return;
        }
        return;
    }

    if (inflight && inflight->kind == Kind::Write && inflight->brightness) {
        const bool ack = frame.size() >= 2 && static_cast<unsigned char>(frame.at(0)) == 0x06
                         && frame.at(1) == writeLetter.toLatin1();
        if (!ack)
            return;
        writeAck = QByteArray(1, char(0x06)) + QByteArray(1, writeLetter.toLatin1()) + QByteArray(";");
        const qint64 latency = writeSentMs < 0 ? -1 : QDateTime::currentMSecsSinceEpoch() - writeSentMs;
        TrafficLog::instance().recordSession(
            "WRITE", QByteArray("ack ") + hexOf(writeAck).toLatin1() + " latency " + QByteArray::number(latency) + " ms");
        replyTimer->stop();
        inflight.reset();
        QTimer::singleShot(150, this, [this]() { sendBrightnessRead(); });
        return;
    }

    if (inflight && (inflight->kind == Kind::Heading || inflight->kind == Kind::Status)) {
        const bool postNetwork = inflight->note == QLatin1String("post-network");
        if (const auto status = Rt21::parseStatusFrame(frame)) {
            failures = 0;
            noteArrival(status->azimuth, status->haveStatus, status->status);
            finishInflight();
            if (postNetwork)
                resumePollsAfterNetwork();
            return;
        }
        if (const auto heading = Rt21::parseHeadingFrame(frame)) {
            failures = 0;
            noteArrival(*heading, false, -1);
            finishInflight();
            if (postNetwork)
                resumePollsAfterNetwork();
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
        const bool handshakeOk = matched && inflight->expectToken == QLatin1String("OK") && inflight->moduleKey.isEmpty()
                                 && inflight->note != QLatin1String("netwrite") && !inflight->moduleOptional
                                 && !inflight->moduleExit && !networkHandshakeDone;
        if (handshakeOk) {
            networkHandshakeDone = true;
            countingHandshakeBytes = false;
            enqueueNetworkQueries();
        }
        if (!inflight->expectValue.isEmpty() && value != inflight->expectValue) {
            writeCompareFailed = true;
            for (int i = queue.size() - 1; i >= 0; --i) {
                if (!queue.at(i).moduleExit)
                    queue.removeAt(i);
            }
        } else if (!inflight->moduleKey.isEmpty()) {
            if (!value.isEmpty())
                networkGotField = true;
            if (networkWriting && inflight->moduleKey == networkWriteKey && value == networkWriteExpect)
                networkWriteOk = true;
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

    if (inflight && inflight->kind == Kind::Read && inflight->brightness) {
        failures = 0;
        configTimeouts = 0;
        const QString raw = Rt21::payloadAfterSoh(frame);
        const auto number = Rt21::parseIntegerPayload(frame);
        const QString got = number ? QString::number(*number) : raw;
        const bool matched = number && *number == brightnessWrote;
        TrafficLog::instance().recordSession(
            "WRITE", QByteArray("read-back ") + (number ? QByteArray::number(*number) : raw.toLatin1()));
        emit configField(inflight->id, raw, number.has_value(), number.value_or(0));
        finishInflight();
        beginBrightnessWatch(true, matched, got);
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
        const QString payload = Rt21::payloadAfterSoh(frame);
        if (inflight->note == QLatin1String("Version "))
            firmwareReport = payload;
        emit configNote(inflight->note + payload);
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
    if (brightnessQueued >= 0 || networkWritePending)
        scheduleBrightnessWrite();
}

void Rt21Driver::noteArrival(double azimuth, bool haveStatus, int status)
{
    failures = 0;
    if (brightnessWatching)
        ++brightnessPollReplies;
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
    if (!io || configMode || networkMode || awaitHeadingAfterNetwork)
        return;
    pollsSinceStatus = 0;
    pollTimer->start(fastPace ? fastPollMs : idlePollMs);
    pollTick();
}

void Rt21Driver::pollTick()
{
    if (!io || inflight || configMode || networkMode || awaitHeadingAfterNetwork || brightnessCapture)
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
    if (inflight->brightness) {
        writeSentMs = QDateTime::currentMSecsSinceEpoch();
        writeTx = inflight->bytes;
        TrafficLog::instance().recordSession("WRITE", QByteArray("tx ") + hexOf(inflight->bytes).toLatin1());
        TrafficLog::instance().recordSession("WRITE-TX", inflight->bytes);
    }
    if (inflight->kind == Kind::Module || inflight->bytes.contains("AT")) {
        lastModuleTxMs = QDateTime::currentMSecsSinceEpoch();
        TrafficLog::instance().recordSession("NOTE", QByteArray("network tx ") + inflight->bytes);
    }
    if (inflight->bytes.contains('\x1b'))
        TrafficLog::instance().recordSession("NOTE", QByteArray("network ESC"));
    if (serial)
        TrafficLog::instance().recordSession("SER-TX", inflight->bytes);
    else if (networkMode)
        TrafficLog::instance().recordSession("NET-TX", inflight->bytes);
    else
        TrafficLog::instance().record("TX", inflight->bytes);
    io->write(inflight->bytes);
    int wait = 1200;
    if (inflight->kind == Kind::Write && inflight->brightness)
        wait = 1200;
    else if (inflight->kind == Kind::Motion || inflight->kind == Kind::Write)
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

QString Rt21Driver::noReplyMessage() const
{
    if (endpoint.transport == RotorEndpoint::Transport::Serial) {
        const QString saverHint = firmwareReport.contains(QStringLiteral("4.14"))
                                      ? QStringLiteral(" RT-21 not answering. If its screen saver is on, power-cycle it.")
                                      : QString();
        return QStringLiteral("No reply from RT-21 on %1 — check port/baud or run Find.%2")
            .arg(endpoint.serialPort, saverHint);
    }
    return QStringLiteral("No reply from RT-21 on %1:%2 — check port/baud or run Find")
        .arg(endpoint.host)
        .arg(endpoint.tcpPort);
}

void Rt21Driver::beginGuardedRead(bool network)
{
    if (!network && awaitHeadingAfterNetwork) {
        pendingConfigAfterNetwork = true;
        return;
    }
    if (!io) {
        const QString message = QStringLiteral("Connect to a rotor before reading its setup.");
        if (network)
            emit networkFinished(false, message);
        else
            emit configFinished(false, message);
        return;
    }
    pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    configMode = true;
    networkMode = network;
    probeForNetwork = network;
    networkHandshakeDone = false;
    countingHandshakeBytes = false;
    handshakeSawBytes = false;
    networkTries = 0;
    configTimeouts = 0;
    Job probe;
    probe.kind = Kind::Probe;
    probe.bytes = Rt21::queryTenths();
    queue.enqueue(probe);
    pump();
}

void Rt21Driver::enqueueConfiguration()
{
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
}

void Rt21Driver::readConfiguration()
{
    beginGuardedRead(false);
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
    Q_UNUSED(ok);
    Q_UNUSED(message);
    networkPhase = NetworkPhase::Idle;
    networkExitAfterCycle = false;
    if (networkTimer)
        networkTimer->stop();
    const bool good = networkWriting ? networkWriteOk : networkGotField;
    const bool stop = networkCancel || good || networkCycle >= 8;
    const QString name = networkWriteName.isEmpty() ? QStringLiteral("network") : networkWriteName;
    networkMode = false;
    configMode = false;
    writeCompareFailed = false;
    framer.setLineMode(false);
    networkEntered = false;
    if (!stop) {
        networkAnotherCycle = true;
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network cycle ") + QByteArray::number(networkCycle) + " had no usable reply");
        holdPollsForNetworkExit();
        return;
    }
    networkAnotherCycle = false;
    QString finalText;
    bool success = good && !networkCancel;
    if (networkCancel)
        finalText = networkWriting ? QStringLiteral("Write not confirmed: %1").arg(name)
                                   : QStringLiteral("Network read cancelled.");
    else if (networkWriting)
        finalText = success ? QStringLiteral("Saved %1").arg(name)
                            : QStringLiteral("Write not confirmed: %1").arg(name);
    else if (success) {
        networkReadOkThisSession = true;
        finalText = QStringLiteral("Network module answered. Nothing was written.");
    } else {
        finalText = QStringLiteral("Network module didn't answer after 8 tries");
    }
    TrafficLog::instance().recordSession("NOTE", finalText.toLatin1());
    const bool writing = networkWriting;
    networkWriting = false;
    networkWriteName.clear();
    networkWriteOk = false;
    holdPollsForNetworkExit();
    if (writing)
        emit writeFinished(success, finalText);
    else
        emit networkFinished(success, finalText);
}

void Rt21Driver::dropNetworkQueries()
{
    QList<Job> exits;
    while (!queue.isEmpty()) {
        const Job job = queue.dequeue();
        if (job.moduleExit)
            exits.append(job);
    }
    for (const Job &job : exits)
        queue.enqueue(job);
    if (exits.isEmpty())
        sendNetworkExit();
    else if (!inflight)
        pump();
}

QString Rt21Driver::networkTryText() const
{
    if (networkWriting)
        return QStringLiteral("Writing %1 (try %2 of 8)…").arg(networkWriteName).arg(networkCycle);
    return QStringLiteral("Reading network (try %1 of 8)…").arg(networkCycle);
}

void Rt21Driver::beginNetworkCycle()
{
    networkGotField = false;
    networkWriteOk = false;
    networkEntered = false;
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network cycle ") + QByteArray::number(networkCycle) + " of 8");
    emit networkStatus(networkTryText());
    networkMode = true;
    configMode = true;
    if (pollTimer)
        pollTimer->stop();
    startNetworkPass();
}

void Rt21Driver::cancelNetwork()
{
    if (retired)
        return;
    const bool pending = networkMode || networkAnotherCycle || awaitHeadingAfterNetwork || networkWritePending
                         || networkWriting;
    networkCancel = true;
    networkAnotherCycle = false;
    networkWritePending = false;
    TrafficLog::instance().recordSession("NOTE", QByteArray("network cancelled"));
    if (networkPhase != NetworkPhase::Idle && networkPhase != NetworkPhase::Exiting) {
        beginNetworkExit();
        return;
    }
    if (networkPhase == NetworkPhase::Exiting || portCycling)
        return;
    if (!networkMode) {
        if (!pending)
            return;
        const bool writing = networkWriting;
        const QString name = networkWriteName;
        networkWriting = false;
        networkWriteName.clear();
        const QString text = writing ? QStringLiteral("Write not confirmed: %1").arg(name)
                                     : QStringLiteral("Network read cancelled.");
        if (writing)
            emit writeFinished(false, text);
        else
            emit networkFinished(false, text);
        return;
    }
    networkMode = false;
    configMode = false;
    const QString name = networkWriteName;
    const bool writing = networkWriting;
    networkWriting = false;
    networkWriteName.clear();
    const QString text = writing ? QStringLiteral("Write not confirmed: %1").arg(name)
                                 : QStringLiteral("Network read cancelled.");
    if (writing)
        emit writeFinished(false, text);
    else
        emit networkFinished(false, text);
    startPoll();
}

void Rt21Driver::enqueueNetworkQueries()
{
    if (networkWriting) {
        Job write;
        write.kind = Kind::Module;
        write.bytes = networkWriteCommand.toLatin1();
        write.expectToken = QStringLiteral("OK");
        write.note = QStringLiteral("netwrite");
        queue.enqueue(write);
        Job read;
        read.kind = Kind::Module;
        read.bytes = networkReadBackCommand;
        read.moduleKey = networkWriteKey;
        read.expectToken = networkReadBackToken;
        queue.enqueue(read);
        sendNetworkExit();
        return;
    }
    for (const Rt21::ModuleQuery &query : Rt21::networkReadSequence()) {
        if (!query.exitStep && query.command.contains("AT*"))
            enqueueModule(query);
    }
    sendNetworkExit();
}

void Rt21Driver::sendNetworkExit()
{
    for (const Rt21::ModuleQuery &query : Rt21::networkReadSequence()) {
        if (query.exitStep)
            enqueueModule(query);
    }
    if (!inflight)
        pump();
}

void Rt21Driver::armNetworkTimer(int ms)
{
    if (!networkTimer) {
        networkTimer = new QTimer(this);
        networkTimer->setSingleShot(true);
        connect(networkTimer, &QTimer::timeout, this, [this]() { onNetworkTimeout(); });
    }
    networkTimer->start(ms);
}

void Rt21Driver::sendNetworkBytes(const QByteArray &bytes)
{
    if (!io || (serial && !serial->isOpen()))
        return;
    if (bytes.contains("AT"))
        lastModuleTxMs = QDateTime::currentMSecsSinceEpoch();
    TrafficLog::instance().recordSession("NOTE", QByteArray("network tx ") + bytes.toHex(' '));
    if (serial)
        TrafficLog::instance().recordSession("SER-TX", bytes);
    else
        TrafficLog::instance().recordSession("NET-TX", bytes);
    io->write(bytes);
    if (serial)
        serial->flush();
}

void Rt21Driver::cycleSerialPort(const std::function<void(bool)> &done)
{
    if (!serial) {
        done(false);
        return;
    }
    portCycling = true;
    ignoreSerialErrorUntil = QDateTime::currentMSecsSinceEpoch() + 1500;
    dropDtr(serial);
    serial->clear(QSerialPort::AllDirections);
    TrafficLog::instance().recordSession("NOTE", QByteArray("network port cycle: DTR low, purge, close"));
    serial->close();
    QTimer::singleShot(200, this, [this, done]() {
        if (retired || !serial || !wantLink) {
            portCycling = false;
            return;
        }
        serial->setBaudRate(endpoint.baud > 0 ? endpoint.baud : 4800);
        serial->setDataBits(QSerialPort::Data8);
        serial->setParity(QSerialPort::NoParity);
        serial->setStopBits(QSerialPort::OneStop);
        serial->setFlowControl(QSerialPort::NoFlowControl);
        const bool opened = serial->open(QIODevice::ReadWrite);
        bool dtr = false;
        bool rts = false;
        if (opened) {
            raiseDtrAndRts(serial, &dtr, &rts);
            serial->clear(QSerialPort::AllDirections);
        }
        framer.clear();
        portCycling = false;
        ignoreSerialErrorUntil = QDateTime::currentMSecsSinceEpoch() + 1500;
        TrafficLog::instance().recordSession(
            "NOTE", opened ? QByteArray("network port reopened DTR ") + (dtr ? "on" : "off") + " RTS " + (rts ? "on" : "off")
                           : QByteArray("network port reopen failed"));
        done(opened);
    });
}

void Rt21Driver::networkReopenFailed()
{
    networkPhase = NetworkPhase::Idle;
    portCycling = false;
    networkEntered = false;
    networkAnotherCycle = false;
    networkMode = false;
    configMode = false;
    const bool writing = networkWriting;
    const QString name = networkWriteName.isEmpty() ? QStringLiteral("network") : networkWriteName;
    networkWriting = false;
    networkWriteName.clear();
    const QString text = writing ? QStringLiteral("Write not confirmed: %1").arg(name)
                                 : QStringLiteral("The serial port didn't reopen.");
    if (writing)
        emit writeFinished(false, text);
    else
        emit networkFinished(false, text);
    failLink(QStringLiteral("The serial port didn't reopen."));
}

void Rt21Driver::beginNetworkExit()
{
    if (networkPhase == NetworkPhase::Exiting && !portCycling)
        return;
    if (networkTimer)
        networkTimer->stop();
    if (portCycling) {
        networkPhase = NetworkPhase::Exiting;
        networkExitAfterCycle = true;
        return;
    }
    networkPhase = NetworkPhase::Exiting;
    if (networkEntered && serial && serial->isOpen()) {
        sendNetworkBytes(QByteArray(1, '\x1b'));
        networkEntered = false;
        TrafficLog::instance().recordSession("NOTE", QByteArray("network ESC"));
        QTimer::singleShot(10, this, [this]() {
            if (retired || networkPhase != NetworkPhase::Exiting)
                return;
            cycleSerialPort([this](bool ok) {
                if (!ok) {
                    networkReopenFailed();
                    return;
                }
                finishNetworkExit();
            });
        });
        return;
    }
    networkEntered = false;
    finishNetworkExit();
}

void Rt21Driver::finishNetworkExit()
{
    networkEntered = false;
    networkExitAfterCycle = false;
    finishNetwork(false, QString());
}

void Rt21Driver::sendEcho()
{
    if (networkPhase == NetworkPhase::Exiting || networkCancel || retired)
        return;
    networkPhase = NetworkPhase::WaitEcho;
    framer.setLineMode(true);
    sendNetworkBytes(QByteArray("AT*SERIAL_ECHO=0\n"));
    armNetworkTimer(700);
}

void Rt21Driver::sendNextQuery()
{
    if (networkCancel || networkPhase == NetworkPhase::Exiting) {
        beginNetworkExit();
        return;
    }
    const QList<Rt21::ModuleQuery> rows = Rt21::networkReadSequence();
    if (networkQueryIndex >= rows.size()) {
        beginNetworkExit();
        return;
    }
    networkPhase = NetworkPhase::Query;
    framer.setLineMode(true);
    sendNetworkBytes(rows.at(networkQueryIndex).command);
    armNetworkTimer(300);
}

void Rt21Driver::sendNetworkWrite()
{
    if (networkCancel) {
        beginNetworkExit();
        return;
    }
    networkPhase = NetworkPhase::WriteAck;
    framer.setLineMode(true);
    sendNetworkBytes(networkWriteCommand.toLatin1());
    armNetworkTimer(700);
}

void Rt21Driver::onNetworkTimeout()
{
    if (networkPhase == NetworkPhase::WaitUart || networkPhase == NetworkPhase::WaitEcho) {
        TrafficLog::instance().recordSession(
            "NOTE", networkPhase == NetworkPhase::WaitUart ? QByteArray("network uart2 timeout")
                                                          : QByteArray("network echo timeout"));
        beginNetworkExit();
        return;
    }
    if (networkPhase == NetworkPhase::WriteAck || networkPhase == NetworkPhase::WriteRead) {
        TrafficLog::instance().recordSession("NOTE", QByteArray("network write timeout"));
        beginNetworkExit();
        return;
    }
    if (networkPhase == NetworkPhase::Query) {
        const QList<Rt21::ModuleQuery> rows = Rt21::networkReadSequence();
        if (networkQueryIndex < rows.size())
            emit networkField(rows.at(networkQueryIndex).key, QString());
        ++networkQueryIndex;
        sendNextQuery();
    }
}

bool Rt21Driver::handleNetworkFrame(const QByteArray &frame)
{
    if (networkPhase == NetworkPhase::Idle)
        return false;
    if (networkPhase == NetworkPhase::Exiting || networkPhase == NetworkPhase::AfterUart)
        return true;

    QString line;
    for (unsigned char c : frame) {
        if (c >= 32 && c < 127)
            line.append(QChar(c));
        else if (!line.isEmpty() && !line.endsWith(QLatin1Char(' ')))
            line.append(QLatin1Char(' '));
    }
    line = line.trimmed();
    if (line.isEmpty())
        return true;

    if (networkPhase == NetworkPhase::WaitUart) {
        if (line.compare(QStringLiteral("<uart2>"), Qt::CaseInsensitive) != 0)
            return true;
        if (networkTimer)
            networkTimer->stop();
        networkPhase = NetworkPhase::AfterUart;
        TrafficLog::instance().recordSession("NOTE", QByteArray("network uart2"));
        QTimer::singleShot(520, this, [this]() {
            if (retired || networkPhase != NetworkPhase::AfterUart)
                return;
            cycleSerialPort([this](bool ok) {
                if (!ok) {
                    networkReopenFailed();
                    return;
                }
                if (networkExitAfterCycle || networkCancel || networkPhase == NetworkPhase::Exiting) {
                    networkExitAfterCycle = false;
                    networkPhase = NetworkPhase::Idle;
                    beginNetworkExit();
                    return;
                }
                QTimer::singleShot(150, this, [this]() {
                    if (retired || networkPhase != NetworkPhase::AfterUart)
                        return;
                    sendEcho();
                });
            });
        });
        return true;
    }

    const bool okLine = line.contains(QStringLiteral("OK"), Qt::CaseInsensitive);
    if (networkPhase == NetworkPhase::WaitEcho) {
        if (!okLine)
            return true;
        if (networkTimer)
            networkTimer->stop();
        TrafficLog::instance().recordSession("NOTE", QByteArray("network echo ok"));
        if (!networkWriting)
            networkGotField = true;
        if (networkWriting)
            sendNetworkWrite();
        else {
            networkQueryIndex = 0;
            sendNextQuery();
        }
        return true;
    }

    auto takeValue = [&](const QString &token) {
        const int at = line.indexOf(token, 0, Qt::CaseInsensitive);
        if (at < 0)
            return QString();
        return line.mid(at + token.size()).trimmed().section(QLatin1Char(' '), 0, 0);
    };

    if (networkPhase == NetworkPhase::Query) {
        const QList<Rt21::ModuleQuery> rows = Rt21::networkReadSequence();
        if (networkQueryIndex >= rows.size())
            return true;
        const Rt21::ModuleQuery query = rows.at(networkQueryIndex);
        if (!line.contains(query.token, Qt::CaseInsensitive))
            return true;
        if (networkTimer)
            networkTimer->stop();
        const QString value = takeValue(query.token);
        if (!value.isEmpty())
            networkGotField = true;
        emit networkField(query.key, value);
        ++networkQueryIndex;
        sendNextQuery();
        return true;
    }

    if (networkPhase == NetworkPhase::WriteAck) {
        if (!okLine)
            return true;
        if (networkTimer)
            networkTimer->stop();
        networkPhase = NetworkPhase::WriteRead;
        sendNetworkBytes(networkReadBackCommand);
        armNetworkTimer(300);
        return true;
    }

    if (networkPhase == NetworkPhase::WriteRead) {
        if (!line.contains(networkReadBackToken, Qt::CaseInsensitive))
            return true;
        if (networkTimer)
            networkTimer->stop();
        const QString value = takeValue(networkReadBackToken);
        networkWriteOk = value == networkWriteExpect;
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network read-back ") + value.toLatin1()
                        + (networkWriteOk ? " match" : " mismatch"));
        emit networkField(networkWriteKey, value);
        beginNetworkExit();
        return true;
    }
    return true;
}

void Rt21Driver::startNetworkPass()
{
    queue.clear();
    if (inflight && inflight->kind == Kind::Module)
        inflight.reset();
    replyTimer->stop();
    framer.clear();
    networkExitAfterCycle = false;
    networkQueryIndex = 0;
    networkGotField = false;
    networkWriteOk = false;
    networkPhase = NetworkPhase::WaitUart;
    const QByteArray enter("AT;");
    networkEntered = true;
    TrafficLog::instance().recordSession("NOTE", QByteArray("network enter AT; expecting <uart2>;"));
    sendNetworkBytes(enter);
    armNetworkTimer(500);
}

void Rt21Driver::readNetwork()
{
    if (networkMode || networkAnotherCycle) {
        cancelNetwork();
        return;
    }
    if (endpoint.transport == RotorEndpoint::Transport::Tcp && endpoint.configured()) {
        emit networkFinished(false, QStringLiteral(
            "Needs a USB or serial connection; reading over IP drops the link."));
        return;
    }
    if (brightnessQueued >= 0 || brightnessCapture || brightnessWatching || brightnessHold) {
        TrafficLog::instance().recordSession("NOTE", QByteArray("network read held for brightness write"));
        emit networkFinished(false, QStringLiteral("Network read waits until the brightness write finishes."));
        return;
    }
    networkWriting = false;
    networkCancel = false;
    networkCycle = 1;
    networkGotField = false;
    networkEntered = false;
    networkReadOkThisSession = false;
    emit networkStatus(networkTryText());
    TrafficLog::instance().recordSession("NOTE", QByteArray("network cycle 1 of 8"));
    beginGuardedRead(true);
}

void Rt21Driver::writeBrightness(int value)
{
    writeRegister(QLatin1Char('O'), value, QStringLiteral("Brightness"));
}

void Rt21Driver::writeRegister(QChar letter, int value, const QString &settingName)
{
    if (retired)
        return;
    const QChar upper = letter.toUpper();
    const QString allowed = QStringLiteral("DEGHIKLNOPRSUVWY");
    if (!allowed.contains(upper) || Rt21::registerWriteCommand(upper, value).isEmpty()) {
        emit writeFinished(false, QStringLiteral("Write not confirmed: %1").arg(settingName));
        return;
    }
    if (upper == QLatin1Char('O') && (value < 0 || value > 15)) {
        emit writeFinished(false, QStringLiteral("Brightness is 0 through 15."));
        return;
    }
    if (upper == QLatin1Char('W') && !Rt21::screenSaverMinuteAllowed(value)) {
        emit writeFinished(false, QStringLiteral("Screen saver is Off or 1, 2, 5, 10, 20, 30, 40, 50, or 60 minutes."));
        return;
    }
    if (firmwareReport.contains(QStringLiteral("4.14"))
        || (!firmwareReport.isEmpty() && !firmwareReport.contains(QStringLiteral("4.13.2")))) {
        emit writeFinished(false, Rt21::firmwareNotice(firmwareReport));
        return;
    }
    if (!transportOpen()) {
        emit writeFinished(false, QStringLiteral("Connect to a rotor before writing."));
        return;
    }
    if (brightnessWatching || brightnessCapture || brightnessHold || brightnessQueued >= 0 || networkWritePending) {
        emit writeFinished(false, QStringLiteral("A write is already running."));
        return;
    }
    writeLetter = upper;
    writeSettingName = settingName;
    brightnessQueued = value;
    TrafficLog::instance().recordSession("NOTE", QByteArray("write requested, waiting for an idle poll"));
    scheduleBrightnessWrite();
}

void Rt21Driver::scheduleBrightnessWrite()
{
    if ((brightnessQueued < 0 && !networkWritePending) || brightnessCapture || brightnessWatching || brightnessHold || retired)
        return;
    if (!transportOpen()) {
        brightnessQueued = -1;
        networkWritePending = false;
        emit writeFinished(false, QStringLiteral("Connect to a rotor before writing."));
        return;
    }
    if (configMode || networkMode || awaitHeadingAfterNetwork || inflight) {
        TrafficLog::instance().recordSession("NOTE", QByteArray("brightness write waiting for the link to go idle"));
        return;
    }
    if (failures > 0 || lastReplyMs < 0) {
        TrafficLog::instance().recordSession("NOTE", QByteArray("brightness write waiting for a heading reply"));
        if (pollTimer && !pollTimer->isActive())
            startPoll();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastModuleTxMs >= 0 && now - lastModuleTxMs < 5000) {
        const int wait = int(5000 - (now - lastModuleTxMs));
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("brightness write waiting ") + QByteArray::number(wait) + " ms after network traffic");
        if (!brightnessDeferTimer) {
            brightnessDeferTimer = new QTimer(this);
            brightnessDeferTimer->setSingleShot(true);
            connect(brightnessDeferTimer, &QTimer::timeout, this, [this]() { scheduleBrightnessWrite(); });
        }
        brightnessDeferTimer->start(wait);
        return;
    }
    if (networkWritePending) {
        networkWritePending = false;
        networkWriting = true;
        networkCancel = false;
        networkCycle = 1;
        networkGotField = false;
        networkWriteOk = false;
        networkEntered = false;
        emit networkStatus(networkTryText());
        TrafficLog::instance().recordSession("NOTE", QByteArray("network cycle 1 of 8"));
        beginGuardedRead(true);
        return;
    }
    const int value = brightnessQueued;
    brightnessQueued = -1;
    brightnessWrote = value;
    brightnessCapture = true;
    brightnessHold = true;
    pollTimer->stop();
    const QByteArray command = Rt21::registerWriteCommand(writeLetter, value);
    Job write;
    write.kind = Kind::Write;
    write.brightness = true;
    write.bytes = command;
    queue.enqueue(write);
    pump();
}

void Rt21Driver::writeNetworkSetting(const QString &key, const QString &value, const QString &settingName)
{
    if (retired)
        return;
    if (endpoint.transport == RotorEndpoint::Transport::Tcp && endpoint.configured()) {
        emit writeFinished(false, QStringLiteral(
            "Needs a USB or serial connection; changing network settings over IP can drop the link."));
        return;
    }
    if (!networkReadOkThisSession) {
        emit writeFinished(false, QStringLiteral("Read the network module before writing it."));
        return;
    }
    if (firmwareReport.contains(QStringLiteral("4.14"))
        || (!firmwareReport.isEmpty() && !firmwareReport.contains(QStringLiteral("4.13.2")))) {
        emit writeFinished(false, Rt21::firmwareNotice(firmwareReport));
        return;
    }
    QString command;
    const QString trimmed = value.trimmed();
    if (key == QLatin1String("dhcp")) {
        QString code;
        if (trimmed == QLatin1String("0") || trimmed.startsWith(QStringLiteral("DHCP"), Qt::CaseInsensitive))
            code = QStringLiteral("0");
        else if (trimmed == QLatin1String("1") || trimmed.startsWith(QStringLiteral("Static"), Qt::CaseInsensitive))
            code = QStringLiteral("1");
        else {
            emit writeFinished(false, QStringLiteral("DHCP is 0 or 1."));
            return;
        }
        command = QStringLiteral("AT*DHCP=%1\n").arg(code);
        networkWriteExpect = code;
        networkReadBackCommand = QByteArray("AT*DHCP?\n");
        networkReadBackToken = QStringLiteral("DHCP=");
    } else if (key == QLatin1String("aip") || key == QLatin1String("anmask") || key == QLatin1String("agw")) {
        QHostAddress address;
        if (!address.setAddress(trimmed) || address.protocol() != QAbstractSocket::IPv4Protocol) {
            emit writeFinished(false, QStringLiteral("%1 needs an IPv4 address.").arg(settingName));
            return;
        }
        const char *id = key == QLatin1String("aip") ? "AIP" : key == QLatin1String("anmask") ? "ANMASK" : "AGW";
        command = QStringLiteral("AT*%1=%2\n").arg(QLatin1String(id), address.toString());
        networkWriteExpect = address.toString();
        networkReadBackCommand = QByteArray("AT*") + id + "?\n";
        networkReadBackToken = QString::fromLatin1(id) + QStringLiteral("=");
    } else if (key == QLatin1String("http") || key == QLatin1String("udp") || key == QLatin1String("serialPort")) {
        bool ok = false;
        const int port = trimmed.toInt(&ok);
        if (!ok || port < 1 || port > 65535) {
            emit writeFinished(false, QStringLiteral("%1 needs a port from 1 to 65535.").arg(settingName));
            return;
        }
        const char *id = key == QLatin1String("http") ? "HTTP" : key == QLatin1String("udp") ? "UDP" : "NVREG16_0";
        command = QStringLiteral("AT*%1=%2\n").arg(QLatin1String(id)).arg(port);
        networkWriteExpect = QString::number(port);
        networkReadBackCommand = QByteArray("AT*") + id + "?\n";
        networkReadBackToken = QString::fromLatin1(id) + QStringLiteral("=");
    } else {
        emit writeFinished(false, QStringLiteral("Write not confirmed: %1").arg(settingName));
        return;
    }
    if (command.contains(QLatin1Char(';')) || command.startsWith(QStringLiteral("W0"))) {
        emit writeFinished(false, QStringLiteral("Write not confirmed: %1").arg(settingName));
        return;
    }
    if (!transportOpen()) {
        emit writeFinished(false, QStringLiteral("Connect to a rotor before writing."));
        return;
    }
    if (brightnessWatching || brightnessCapture || brightnessHold || brightnessQueued >= 0 || networkWritePending) {
        emit writeFinished(false, QStringLiteral("A write is already running."));
        return;
    }
    networkWritePending = true;
    networkWriteOk = false;
    networkWriteCommand = command;
    networkWriteName = settingName;
    networkWriteKey = key;
    TrafficLog::instance().recordSession("NOTE", QByteArray("network write requested, waiting for an idle poll"));
    scheduleBrightnessWrite();
}

void Rt21Driver::sendBrightnessRead()
{
    if (!brightnessHold || !io)
        return;
    brightnessHold = false;
    Job read;
    read.kind = Kind::Read;
    read.brightness = true;
    read.id = writeLetter;
    read.bytes = Rt21::registerReadCommand(writeLetter);
    read.expectValue = QString::number(brightnessWrote);
    queue.enqueue(read);
    pump();
}

void Rt21Driver::holdPollsForNetworkExit()
{
    awaitHeadingAfterNetwork = true;
    networkHeadingRetried = false;
    TrafficLog::instance().recordSession("NOTE", QByteArray("network exit, waiting for a heading before polls"));
    if (!io) {
        awaitHeadingAfterNetwork = false;
        return;
    }
    Job job;
    job.kind = Kind::Heading;
    job.bytes = Rt21::queryTenths();
    job.note = QStringLiteral("post-network");
    queue.enqueue(job);
    pump();
}

void Rt21Driver::resumePollsAfterNetwork()
{
    if (!awaitHeadingAfterNetwork)
        return;
    afterNetworkHeading(true);
}

void Rt21Driver::afterNetworkHeading(bool headingSeen)
{
    awaitHeadingAfterNetwork = false;
    TrafficLog::instance().recordSession(
        "NOTE", headingSeen ? QByteArray("heading resumed after network")
                            : QByteArray("no heading after network exit"));
    if (networkAnotherCycle && !networkCancel && io && networkCycle < 8) {
        if (!headingSeen) {
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("starting the next network cycle without a heading"));
        }
        networkAnotherCycle = false;
        ++networkCycle;
        beginNetworkCycle();
        return;
    }
    networkAnotherCycle = false;
    startPoll();
    if (brightnessQueued >= 0 || networkWritePending)
        scheduleBrightnessWrite();
    if (pendingConfigAfterNetwork) {
        pendingConfigAfterNetwork = false;
        beginGuardedRead(false);
    }
}

void Rt21Driver::beginBrightnessWatch(bool sawRead, bool matched, const QString &readText)
{
    brightnessCapture = false;
    brightnessWatching = true;
    brightnessSawRead = sawRead;
    brightnessMatched = matched;
    brightnessReadText = readText;
    brightnessPollReplies = 0;
    QByteArray note = QByteArray("brightness watch start wrote ") + QByteArray::number(brightnessWrote);
    note += sawRead ? QByteArray(" read ") + readText.toLatin1() : QByteArray(" no read-back");
    TrafficLog::instance().recordSession("NOTE", note);
    if (!brightnessWatchTimer) {
        brightnessWatchTimer = new QTimer(this);
        brightnessWatchTimer->setSingleShot(true);
        connect(brightnessWatchTimer, &QTimer::timeout, this, [this]() { finishBrightnessWatch(false); });
        brightnessPollTimer = new QTimer(this);
        connect(brightnessPollTimer, &QTimer::timeout, this, [this]() { sendQuery(); });
    }
    brightnessWatchTimer->start(10000);
    brightnessPollTimer->start(2000);
    sendQuery();
}

void Rt21Driver::finishBrightnessWatch(bool portClosed)
{
    if (!brightnessWatching && !brightnessCapture)
        return;
    const int replies = brightnessPollReplies;
    const bool sawRead = brightnessSawRead;
    const bool matched = brightnessMatched;
    const int wrote = brightnessWrote;
    const QString readText = brightnessReadText;
    brightnessWatching = false;
    brightnessCapture = false;
    if (brightnessWatchTimer)
        brightnessWatchTimer->stop();
    if (brightnessPollTimer)
        brightnessPollTimer->stop();
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("brightness watch heading replies ") + QByteArray::number(replies));
    QString message;
    bool ok = false;
    Q_UNUSED(wrote);
    Q_UNUSED(readText);
    if (portClosed || replies <= 0 || !sawRead || !matched)
        message = QStringLiteral("Write not confirmed: %1").arg(writeSettingName);
    else {
        message = QStringLiteral("Saved %1").arg(writeSettingName);
        ok = true;
    }
    if (!portClosed && io)
        startPoll();
    emit writeFinished(ok, message);
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
            "Writes stay off unless the rotor reports 4.13.2."));
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
