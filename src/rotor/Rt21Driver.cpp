/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Driver.h"

#include "StatusTone.h"

#include "NetCycleProbe.h"
#include "Reconnect.h"
#include "Rt21Finder.h"
#include "SerialLines.h"
#include "TrafficLog.h"
#include "UserFiles.h"
#include "core/Bearing.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QSerialPort>
#include <QSettings>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <mstcpip.h>
#  include <windows.h>
#  include <mmsystem.h>
#  include <QtSerialPort/private/qserialport_p.h>
#else
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <termios.h>
#endif

#if defined(Q_OS_WIN)
struct WinRawSerial
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE readEvent = nullptr;
    HANDLE writeEvent = nullptr;
    OVERLAPPED readOv{};
    OVERLAPPED writeOv{};
    bool readPending = false;
    char buf[512]{};
};
#endif

namespace {

int g_alive = 0;

// Captured IOCTL times, both successes: CLR_DTR is 519-527 ms after <uart2>;,
// close follows about 9 ms later, open about 40 ms after close, and DTR stays
// low for 190-197 ms. ECHO is 3-14 ms after the final SET_DTR. uart2->ECHO is
// 730 ms. A 150 ms wait after DTR rose was the miss.
constexpr int kUartToDtrLowMs = 520;
constexpr int kDtrLowMs = 195;
constexpr int kOpenAfterCloseMs = 40;
constexpr int kNetworkTries = 12;
constexpr int kWarmupFirstMs = 3000;
constexpr int kWarmupRetryMs = 5000;
constexpr int kWarmupGapMs = 400;
constexpr int kWarmupCapMs = 15000;
constexpr int kColdWarmupCapMs = 35000;
// Green Heron 4.B12, both OK captures: T11;, then ESC ESC 215 ms later.
constexpr int kColdT11ToEscMs = 215;
// ';' is 1510 ms after that ESC pair. Polls start 1180 ms after ';'.
constexpr int kColdSemiToPollMs = 1180;
constexpr int kPassThroughQuietMs = 35000;
constexpr int kPreAtQuietMs = 650;
// Utility open: ESC, ESC 0.7 ms apart, then ';' about 1.5 s later.
constexpr int kLeadInMs = 1510;
// R21's reply ends, R01 follows within about 20 ms, and the next R21 is
// about 210 ms after R01's reply. That is the pair in both utility successes.
constexpr int kPairAfterR21Ms = 20;
constexpr int kPairAfterR01Ms = 210;
// The utility waits for the line (ERROR 1 near 47 ms, OK near 63 ms) and
// resends only after ERROR. It does not write the echo again into silence.
constexpr int kEchoReplyMs = 700;
// DTR is low for about 20 ms at the utility's open, before any AT;.
constexpr int kOpenDtrLowMs = 20;
// Cap9's RW1/WW0n/RW1 write-back matched the utility and the module still
// stayed silent. It is an EEPROM write. Left in place, not sent.
constexpr bool kRearmRegisterW = false;

QByteArray takeLeadingOk(QByteArray *received)
{
    if (!received || !received->startsWith("OK"))
        return {};
    int end = 2;
    if (end < received->size() && received->at(end) == '\r')
        ++end;
    if (end < received->size() && received->at(end) == '\n')
        ++end;
    const QByteArray ok = received->left(end);
    *received = received->mid(end);
    return ok;
}

void preciseSleepMs(int ms)
{
    if (ms <= 0)
        return;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
#if defined(Q_OS_WIN)
    timeBeginPeriod(1);
    if (ms > 2)
        ::Sleep(static_cast<DWORD>(ms - 2));
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
        ;
    timeEndPeriod(1);
#else
    if (ms > 2)
        QThread::msleep(static_cast<unsigned long>(ms - 2));
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
        ;
#endif
}

void preciseSleepUntil(qint64 deadlineMs)
{
    preciseSleepMs(int(deadlineMs - QDateTime::currentMSecsSinceEpoch()));
}

#if defined(Q_OS_WIN)
HANDLE portHandle(QSerialPort *port)
{
    if (!port || !port->isOpen())
        return INVALID_HANDLE_VALUE;
    return reinterpret_cast<HANDLE>(port->handle());
}

void winLine(HANDLE handle, DWORD code)
{
    if (handle && handle != INVALID_HANDLE_VALUE)
        ::EscapeCommFunction(handle, code);
}

void winPurge(HANDLE handle)
{
    if (handle && handle != INVALID_HANDLE_VALUE)
        ::PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

DCB networkDcb(int baud)
{
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    dcb.BaudRate = baud > 0 ? DWORD(baud) : 4800;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    return dcb;
}
#endif

#if defined(Q_OS_WIN)
// QSerialPort::close waits out a pending WaitCommEvent. CancelIo from this
// thread does not always wake that wait, which is what stretched close to
// hundreds of milliseconds on the Edgeport. Arm short timeouts, drop the
// comm mask, and cancel the overlapped I/O before close.
class PortLeaveLines final : public QSerialPort
{
public:
    static void apply(QSerialPort *port)
    {
        if (!port)
            return;
        auto *self = static_cast<PortLeaveLines *>(port);
        if (!self->d_ptr)
            return;
        static_cast<QSerialPortPrivate *>(self->d_ptr.data())->settingsRestoredOnClose = false;
    }
};

void wakeSerialForClose(QSerialPort *port)
{
    if (!port || !port->isOpen())
        return;
    const HANDLE handle = reinterpret_cast<HANDLE>(port->handle());
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return;
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 1;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 10;
    ::SetCommTimeouts(handle, &timeouts);
    ::SetCommMask(handle, 0);
    ::CancelIoEx(handle, nullptr);
    ::PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
}

void closeWinRaw(WinRawSerial *&raw)
{
    if (!raw)
        return;
    if (raw->handle != INVALID_HANDLE_VALUE) {
        ::CancelIoEx(raw->handle, nullptr);
        if (raw->readPending && raw->readEvent)
            ::WaitForSingleObject(raw->readEvent, 50);
        ::CloseHandle(raw->handle);
        raw->handle = INVALID_HANDLE_VALUE;
    }
    if (raw->readEvent)
        ::CloseHandle(raw->readEvent);
    if (raw->writeEvent)
        ::CloseHandle(raw->writeEvent);
    delete raw;
    raw = nullptr;
}

// Overlapped, like .NET SerialPort. A synchronous ReadFile on this thread
// blocks inside the Edgeport driver for the 2000 ms timeout.
bool writeWinRaw(WinRawSerial *raw, const QByteArray &bytes)
{
    if (!raw || raw->handle == INVALID_HANDLE_VALUE || bytes.isEmpty() || !raw->writeEvent)
        return false;
    ::ResetEvent(raw->writeEvent);
    ::ZeroMemory(&raw->writeOv, sizeof raw->writeOv);
    raw->writeOv.hEvent = raw->writeEvent;
    DWORD wrote = 0;
    const BOOL started = ::WriteFile(raw->handle, bytes.constData(), DWORD(bytes.size()), &wrote, &raw->writeOv);
    if (!started && ::GetLastError() != ERROR_IO_PENDING) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("raw write failed err ") + QByteArray::number(int(::GetLastError())));
        return false;
    }
    if (!started || int(wrote) != bytes.size()) {
        if (::WaitForSingleObject(raw->writeEvent, 300) != WAIT_OBJECT_0) {
            TrafficLog::instance().recordSession("NOTE", QByteArray("raw write still pending after 300 ms"));
            return false;
        }
        if (!::GetOverlappedResult(raw->handle, &raw->writeOv, &wrote, FALSE)) {
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("raw write failed err ") + QByteArray::number(int(::GetLastError())));
            return false;
        }
    }
    if (int(wrote) != bytes.size()) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("raw write short ") + QByteArray::number(int(wrote)) + "/"
                        + QByteArray::number(bytes.size()));
        return false;
    }
    return true;
}
#endif
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
        if (note == QLatin1String("warmup")) {
            onWarmupMiss();
            return;
        }
        if (note == QLatin1String("route-version") || note == QLatin1String("route-pot")) {
            TrafficLog::instance().recordSession("NOTE", QByteArray("serial route no reply"));
            serialRoute = false;
            // R01 (or R11) never answered. If T11 was sent, ESC before any poll
            // or the front panel stays in the bridge and the next open sends T11 again.
            startPoll();
            return;
        }
        if (note == QLatin1String("bridge")) {
            abortNetworkRead(QStringLiteral("Register W did not answer, so the network read was not started."));
            return;
        }
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
        if (note == QLatin1String("post-quiet")
            || (awaitHeadingAfterNetwork && note == QLatin1String("post-network"))) {
            recordHeadingMiss();
            if (powerCycleDue()) {
                abortNetworkRead(QStringLiteral("Controller stopped answering. Power-cycle the RT-21."));
                return;
            }
            if (note == QLatin1String("post-network") && !networkHeadingRetried) {
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
            if (!heading_)
                showNote(QStringLiteral("Waiting for a heading from the RT-21…"));
            QTimer::singleShot(250, this, [this]() {
                if (!inflight && io && !networkMode && !configMode && !awaitHeadingAfterNetwork && !brightnessCapture)
                    sendQuery();
            });
            return;
        }
        TrafficLog::instance().note(QStringLiteral("query timeout consecutive=%1").arg(failures));
        if (!lastQueryWasAi)
            useAiNext = true;
        // No heading yet: stay on the fast poll and keep asking. Two misses
        // used to arm the 30 s idle timer and the link went silent.
        if (!heading_) {
            showNote(QStringLiteral("Waiting for a heading from the RT-21…"));
            fastPace = true;
            if (pollTimer)
                pollTimer->start(fastPollMs);
            return;
        }
        showNote(QStringLiteral("Waiting for the RT-21…"));
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
        // connectEndpoint bumps linkGeneration and stops this timer. A timeout
        // already queued from the failed IP attempt must not open the port again.
        if (linkGeneration != reconnectGeneration)
            return;
        if (link_ == Link::Connecting || link_ == Link::On || link_ == Link::Moving)
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
    if (tcp && tcp->state() == QAbstractSocket::ConnectedState)
        return true;
    if (serial && serial->isOpen())
        return true;
    // The post-uart2 handle is the live link. A map click must not treat it as down.
    if (winRaw)
        return true;
    return false;
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
    // ESC while the handle that sent T11 is still open. A later open must not
    // send T11 again if this write could not land.
    if (bridgeMayBeOpen())
        releaseBridge();
    ++suppress;
    portCycling = false;
    networkPhase = NetworkPhase::Idle;
    restoreQtSerial();
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
    networkColdBootPending = false;
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
        if (port->isOpen() && sentT11InSession) {
            // Cap2 line 289: T01; goes out immediately before CLOSE, and only
            // after this session actually sent T11;. Map polls do not send it.
            const QByteArray restore("T01;");
            TrafficLog::instance().recordSession("SER-TX", restore);
            TrafficLog::instance().recordSession("NOTE", QByteArray("network restore TCP mode T01;"));
            port->write(restore);
            port->flush();
            preciseSleepMs(15);
        }
        sentT11InSession = false;
        networkWakeSent = false;
        networkColdBootPending = false;
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
    const quint64 generation = ++linkGeneration;
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
        connect(tcp, &QTcpSocket::errorOccurred, this, [this, generation](QAbstractSocket::SocketError) {
            if (generation != linkGeneration)
                return;
            onSocketError();
        });
        connect(tcp, &QTcpSocket::disconnected, this, [this, generation]() {
            if (generation != linkGeneration || suppress || retired || !wantLink || !tcp)
                return;
            const QString err = plainLinkFailure(tcp->errorString());
            endAttempt(QStringLiteral("peer closed"));
            closeIo(QStringLiteral("peer closed"));
            failLink(err, false);
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
    connect(serial, &QSerialPort::errorOccurred, this, [this, generation](QSerialPort::SerialPortError error) {
        if (generation != linkGeneration)
            return;
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
    // The IP reconnect timer is armed before this open. A serial success
    // must not be followed by that timer calling connectEndpoint again.
    if (reconnectTimer)
        reconnectTimer->stop();
    if (!note_.isEmpty()) {
        note_.clear();
        if (noteTimer)
            noteTimer->stop();
        emit statusNote(QString());
    }
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
    if (endpoint.transport == RotorEndpoint::Transport::Serial) {
        beginSerialRoute();
        return;
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

void Rt21Driver::failLink(const QString &messageIn, bool linkDown)
{
    const QString message = plainLinkFailure(messageIn);
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
    if (target_) {
        target_.reset();
        emit targetChanged(0, false);
    }
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
    reconnectGeneration = linkGeneration;
    reconnectTimer->start(delay);
}

void Rt21Driver::ingestReceived(const QByteArray &receivedIn, bool fromSerial)
{
    QByteArray received = receivedIn;
    if ((serialRouteT11 || networkT11Watch)) {
        const QByteArray ok = takeLeadingOk(&received);
        if (!ok.isEmpty()) {
            TrafficLog::instance().recordSession(
                "NOTE", (serialRouteT11 ? QByteArray("serial route T11 rx ") : QByteArray("network variant F T11 rx "))
                            + ok.toHex(' '));
        }
    }
    if (received.isEmpty())
        return;
    if (countingHandshakeBytes)
        handshakeSawBytes = true;
    if (networkPhase == NetworkPhase::WaitEcho || networkPhase == NetworkPhase::WaitEchoLate)
        networkLateBytes.append(received);
    if (brightnessCapture)
        TrafficLog::instance().recordSession("WRITE-RX", received);
    if (fromSerial)
        TrafficLog::instance().recordSession("SER-RX", networkMode ? Rt21::redactForLog(received) : received);
    else if (networkMode)
        TrafficLog::instance().recordSession("NET-RX", Rt21::redactForLog(received));
    else
        TrafficLog::instance().record("RX", received);
    if (networkPhase == NetworkPhase::PreAt) {
        // Bytes that arrived during the quiet must not glue onto <uart2>;.
        const int before = framer.controlBytes();
        framer.append(received);
        logControlBytes(before);
        framer.take();
        return;
    }
    const int before = framer.controlBytes();
    framer.append(received);
    logControlBytes(before);
    const QList<QByteArray> frames = framer.take();
    for (const QByteArray &frame : frames)
        handleFrame(frame);
}

void Rt21Driver::onReadyRead()
{
    if (winRaw)
        return;
    if (!io || (serial && !serial->isOpen()))
        return;
    ingestReceived(io->readAll(), serial != nullptr);
}

void Rt21Driver::pollRawSerial()
{
#if defined(Q_OS_WIN)
    if (!winRaw || winRaw->handle == INVALID_HANDLE_VALUE)
        return;
    WinRawSerial *raw = winRaw;
    auto deliver = [this](const char *data, int capacity, DWORD got) {
        const QByteArray bytes = serialBytesTaken(data, capacity, int(got));
        if (!bytes.isEmpty())
            ingestReceived(bytes, true);
    };
    if (raw->readPending) {
        if (::WaitForSingleObject(raw->readEvent, 0) != WAIT_OBJECT_0)
            return;
        DWORD got = 0;
        const BOOL ok = ::GetOverlappedResult(raw->handle, &raw->readOv, &got, FALSE);
        raw->readPending = false;
        if (!ok) {
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("raw read error ") + QByteArray::number(int(::GetLastError())));
        } else {
            deliver(raw->buf, int(sizeof raw->buf), got);
        }
    }
    if (winRaw != raw)
        return;
    for (int n = 0; n < 8 && winRaw == raw && !raw->readPending; ++n) {
        ::ResetEvent(raw->readEvent);
        ::ZeroMemory(&raw->readOv, sizeof raw->readOv);
        raw->readOv.hEvent = raw->readEvent;
        ::ZeroMemory(raw->buf, sizeof raw->buf);
        DWORD got = 0;
        const BOOL ok = ::ReadFile(raw->handle, raw->buf, sizeof raw->buf, &got, &raw->readOv);
        if (ok) {
            deliver(raw->buf, int(sizeof raw->buf), got);
            if (got == 0 || winRaw != raw)
                break;
            continue;
        }
        const DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
            raw->readPending = true;
            break;
        }
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("raw read error ") + QByteArray::number(int(err)));
        break;
    }
#else
    Q_UNUSED(this);
#endif
}

void Rt21Driver::restoreQtSerial()
{
#if defined(Q_OS_WIN)
    if (networkRawTimer)
        networkRawTimer->stop();
    closeWinRaw(winRaw);
#endif
    if (serial && !serial->isOpen() && !endpoint.serialPort.isEmpty()) {
        serial->setBaudRate(endpoint.baud > 0 ? endpoint.baud : 4800);
        serial->setDataBits(QSerialPort::Data8);
        serial->setParity(QSerialPort::NoParity);
        serial->setStopBits(QSerialPort::OneStop);
        serial->setFlowControl(QSerialPort::NoFlowControl);
        if (serial->open(QIODevice::ReadWrite))
            raiseDtrAndRts(serial);
    }
}

void Rt21Driver::handleFrame(const QByteArray &frame)
{
    const QByteArray upper = frame.toUpper();
    if (upper == "BI1" || upper == "AI1" || upper == "R21" || upper == "R11" || upper == "R01" || upper == "R41")
        return;
    if (upper == "T11" && (serialRouteT11 || networkT11Watch)) {
        TrafficLog::instance().recordSession(
            "NOTE", (serialRouteT11 ? QByteArray("serial route T11 rx ") : QByteArray("network variant F T11 rx "))
                        + frame.toHex(' '));
        return;
    }

    if (inflight && inflight->note == QLatin1String("bridge")) {
        onBridgeReply(frame);
        return;
    }

    if (inflight && inflight->note == QLatin1String("warmup")) {
        failures = 0;
        const QByteArray command = inflight->bytes;
        replyTimer->stop();
        inflight.reset();
        onWarmupAnswer(command);
        return;
    }

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
        const bool postQuiet = inflight->note == QLatin1String("post-quiet");
        const bool connectHeading = inflight->note == QLatin1String("connect-heading");
        if (const auto status = Rt21::parseStatusFrame(frame)) {
            failures = 0;
            noteArrival(status->azimuth, status->haveStatus, status->status);
            finishInflight();
            if (connectHeading)
                enqueueVersionAfterHeading();
            else if (postNetwork)
                resumePollsAfterNetwork();
            else if (postQuiet)
                afterNetworkHeading(true);
            return;
        }
        if (const auto heading = Rt21::parseHeadingFrame(frame)) {
            failures = 0;
            noteArrival(*heading, false, -1);
            finishInflight();
            if (connectHeading)
                enqueueVersionAfterHeading();
            else if (postNetwork)
                resumePollsAfterNetwork();
            else if (postQuiet)
                afterNetworkHeading(true);
            return;
        }
        if (Rt21::isCommandEcho(frame))
            return;
        if (postNetwork || postQuiet) {
            failures = 0;
            finishInflight();
            if (postNetwork)
                resumePollsAfterNetwork();
            else
                afterNetworkHeading(true);
            return;
        }
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

    if (inflight && inflight->kind == Kind::Note && inflight->note == QLatin1String("route-version")) {
        failures = 0;
        serialRoute = false;
        const QString payload = Rt21::payloadAfterSoh(frame);
        firmwareReport = payload;
        TrafficLog::instance().recordSession("NOTE", QByteArray("serial route R11 ") + payload.toLatin1());
        finishInflight();
        startPoll();
        return;
    }

    if (inflight && inflight->kind == Kind::Note && inflight->note == QLatin1String("route-pot")) {
        failures = 0;
        serialRoute = false;
        const QString payload = Rt21::payloadAfterSoh(frame);
        TrafficLog::instance().recordSession("NOTE", QByteArray("serial route R01 ") + payload.toLatin1());
        finishInflight();
        // T11 may have opened the bridge even though R01 answered. ESC before the first poll.
        startPoll();
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
    if (link_ == Link::Failed || link_ == Link::Off) {
        nowMoving = false;
        if (target_) {
            target_.reset();
            emit targetChanged(0, false);
        }
    } else if (target_) {
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
    if (bridgeLatched || serialRouteT11 || networkT11Watch)
        releaseBridge();
    if (bridgeLatched)
        return;
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
    if (networkTxQuiet || inflight || queue.isEmpty() || !io)
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
    if (networkMode || networkAnotherCycle || (networkPhase != NetworkPhase::Idle && networkPhase != NetworkPhase::Quiet))
        cancelNetwork();
    else if (bridgeMayBeOpen())
        releaseBridge();
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
    if (bridgeLatched || networkEntered || (networkPhase != NetworkPhase::Idle && networkPhase != NetworkPhase::Quiet)) {
        if (networkMode || networkAnotherCycle)
            cancelNetwork();
        else
            releaseBridge();
        if (link_ == Link::Moving || link_ == Link::On)
            setLink(Link::On, detail_);
        return;
    }
    if (io) {
        const QByteArray stop = Rt21::stopCommand();
        TrafficLog::instance().record("TX", stop);
        io->write(stop);
        if (serial && serial->isOpen())
            serial->flush();
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
    networkTxQuiet = false;
    networkPhase = NetworkPhase::Idle;
    networkExitAfterCycle = false;
    if (networkTimer)
        networkTimer->stop();
    const bool good = networkWriting ? networkWriteOk : networkGotField;
    const bool stop = networkCancel || good || networkCycle >= kNetworkTries;
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
    else if (networkWriting) {
        if (success)
            rememberCycleVariant();
        finalText = success ? QStringLiteral("Saved %1").arg(name)
                            : QStringLiteral("Write not confirmed: %1").arg(name);
    }
    else if (success) {
        networkReadOkThisSession = true;
        rememberCycleVariant();
        finalText = QStringLiteral("Network module answered. Nothing was written.");
    } else {
        finalText = QStringLiteral("Network module didn't answer after 12 tries");
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
        return QStringLiteral("Writing %1 (try %2 of %3)…").arg(networkWriteName).arg(networkCycle).arg(kNetworkTries);
    return QStringLiteral("Reading network (try %1 of %2)…").arg(networkCycle).arg(kNetworkTries);
}

void Rt21Driver::beginColdBootWake()
{
    // Power-up leaves the controller in T01, where <uart2>; still arrives and
    // AT* is ignored. The utility's wake-up is once per entry, and it is not
    // glued onto AT;: T11;, 215 ms, ESC ESC, 1510 ms, ';', 1180 ms, then the
    // answered polls. A plain connect never comes through here.
    networkGotField = false;
    networkWriteOk = false;
    networkEntered = false;
    networkWakeSent = true;
    sentT11InSession = true;
    networkMode = true;
    configMode = true;
    networkPhase = NetworkPhase::LeadIn;
    serialRoute = false;
    serialRouteT11 = false;
    bridgeLatched = true;
    if (pollTimer)
        pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network cycle ") + QByteArray::number(networkCycle) + " of " + QByteArray::number(kNetworkTries));
    emit networkStatus(networkTryText());
    TrafficLog::instance().recordSession("NOTE", QByteArray("network cold-boot wake: T11; -> ESC ESC -> ;"));
    sendNetworkBytes(QByteArray("T11;"));
    QTimer::singleShot(kColdT11ToEscMs, this, [this]() {
        if (retired || networkCancel || networkPhase != NetworkPhase::LeadIn)
            return;
        const QByteArray esc(1, '\x1b');
        sendNetworkBytes(esc);
        sendNetworkBytes(esc);
        if ((serial && serial->isOpen()) || winRaw || (io && io->isOpen())) {
            bridgeLatched = false;
            serialRouteT11 = false;
            networkT11Watch = false;
            networkEntered = false;
        }
        TrafficLog::instance().recordSession("NOTE", QByteArray("network lead-in ESC ESC"));
        QTimer::singleShot(kLeadInMs, this, [this]() {
            if (retired || networkCancel || networkPhase != NetworkPhase::LeadIn)
                return;
            sendNetworkBytes(QByteArray(1, ';'));
            TrafficLog::instance().recordSession("NOTE", QByteArray("network lead-in ;"));
            QTimer::singleShot(kColdSemiToPollMs, this, [this]() {
                if (retired || networkCancel || networkPhase != NetworkPhase::LeadIn)
                    return;
                networkWarmupLimitMs = kColdWarmupCapMs;
                beginNetworkWarmup(kWarmupRetryMs);
            });
        });
    });
}

void Rt21Driver::beginNetworkCycle()
{
    networkGotField = false;
    networkWriteOk = false;
    networkEntered = false;
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network cycle ") + QByteArray::number(networkCycle) + " of " + QByteArray::number(kNetworkTries));
    emit networkStatus(networkTryText());
    if (pollTimer)
        pollTimer->stop();
    beginNetworkLeadIn();
}

void Rt21Driver::primeUtilityLines()
{
    if (!serial || !serial->isOpen() || winRaw)
        return;
    // The utility's CreateFile leaves DTR low for about 20 ms, with RTS on
    // and no flow control, before the double ESC. Qt's open raises DTR and
    // never drops it until after <uart2>;. Timeouts and the wait mask stay
    // with Qt's reader; those are host-side and already match on the reopen
    // that sends the echo.
#if defined(Q_OS_WIN)
    const HANDLE handle = portHandle(serial);
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return;
    ::EscapeCommFunction(handle, SETRTS);
    ::EscapeCommFunction(handle, CLRDTR);
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!::GetCommState(handle, &dcb))
        return;
    dcb.BaudRate = endpoint.baud > 0 ? DWORD(endpoint.baud) : 4800;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.XonChar = 0x11;
    dcb.XoffChar = 0x13;
    dcb.EofChar = 0x1A;
    dcb.EvtChar = 0x1A;
    dcb.XonLim = 0x200;
    dcb.XoffLim = 0x200;
    ::SetCommState(handle, &dcb);
    preciseSleepMs(kOpenDtrLowMs);
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    ::SetCommState(handle, &dcb);
    ::EscapeCommFunction(handle, SETRTS);
    ::EscapeCommFunction(handle, SETDTR);
    ::SetupComm(handle, 4096, 4096);
#else
    const int fd = int(serial->handle());
    if (fd >= 0) {
        int bits = 0;
        if (::ioctl(fd, TIOCMGET, &bits) == 0) {
            bits &= ~TIOCM_DTR;
            bits |= TIOCM_RTS;
            ::ioctl(fd, TIOCMSET, &bits);
        }
    }
    preciseSleepMs(kOpenDtrLowMs);
    raiseDtrAndRts(serial);
#endif
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network open DTR pulse: low ") + QByteArray::number(kOpenDtrLowMs)
                    + " ms (port open, not the post-uart2 pulse), RTS on, 4800 8N1, no flow control");
}

void Rt21Driver::beginNetworkLeadIn()
{
    networkPhase = NetworkPhase::LeadIn;
    networkMode = true;
    configMode = true;
    serialRoute = false;
    serialRouteT11 = false;
    if (pollTimer)
        pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    primeUtilityLines();
    const QByteArray esc(1, '\x1b');
    sendNetworkBytes(esc);
    sendNetworkBytes(esc);
    if ((serial && serial->isOpen()) || winRaw || (io && io->isOpen())) {
        bridgeLatched = false;
        serialRouteT11 = false;
        networkT11Watch = false;
        networkEntered = false;
    }
    TrafficLog::instance().recordSession("NOTE", QByteArray("network lead-in ESC ESC"));
    QTimer::singleShot(kLeadInMs, this, [this]() { finishNetworkLeadIn(); });
}

void Rt21Driver::finishNetworkLeadIn()
{
    if (retired || networkCancel || networkPhase != NetworkPhase::LeadIn)
        return;
    sendNetworkBytes(QByteArray(1, ';'));
    TrafficLog::instance().recordSession("NOTE", QByteArray("network lead-in ;"));
    beginNetworkWarmup(networkCycle <= 1 ? kWarmupFirstMs : kWarmupRetryMs);
}

void Rt21Driver::cancelNetwork()
{
    if (retired)
        return;
    if (bridgeLatched || serialRouteT11 || networkT11Watch)
        releaseBridge();
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
        Job agw;
        agw.kind = Kind::Module;
        agw.bytes = QByteArray("AT*AGW?\n");
        agw.moduleKey = QStringLiteral("agw");
        agw.expectToken = QStringLiteral("AGW=");
        queue.enqueue(agw);
        Job gw;
        gw.kind = Kind::Module;
        gw.bytes = QByteArray("AT*GW?\n");
        gw.moduleKey = QStringLiteral("gw");
        gw.expectToken = QStringLiteral("GW=");
        queue.enqueue(gw);
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
#if defined(Q_OS_WIN)
    if (winRaw) {
        if (bytes.contains("AT"))
            lastModuleTxMs = QDateTime::currentMSecsSinceEpoch();
        TrafficLog::instance().recordSession("NOTE", QByteArray("network tx ") + bytes.toHex(' '));
        TrafficLog::instance().recordSession("SER-TX", bytes);
        writeWinRaw(winRaw, bytes);
        return;
    }
#endif
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

void Rt21Driver::loadCycleOrigin()
{
    QSettings settings(UserFiles::settingsPath(), QSettings::IniFormat);
    const QString mode = settings.value(QStringLiteral("net_cycle_mode")).toString().trimmed().toUpper();
    if (mode == QLatin1String("A"))
        networkVariantOrigin = 1;
    else if (mode == QLatin1String("B") || mode == QLatin1String("DTR"))
        networkVariantOrigin = 2;
    else
        networkVariantOrigin = 0;
}

char Rt21Driver::variantForThisTry() const
{
    // The utility's only entry is the close and reopen. A and B were extra
    // AT; attempts inside the module's pass-through window, and both failed
    // the same way as D. Always use the captured close/open.
    Q_UNUSED(networkVariantOrigin);
    return 'D';
}

void Rt21Driver::rememberCycleVariant()
{
    QSettings settings(UserFiles::settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("net_cycle_mode"), QString(QLatin1Char(networkVariant)));
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network success variant ") + networkVariant);
}

void Rt21Driver::raiseLinesAndFinish(const std::function<void(bool)> &done)
{
    networkReopenedMs = QDateTime::currentMSecsSinceEpoch();
    networkDtrRoseProbeMs().store(networkReopenedMs);
    const int lowMs = int(networkReopenedMs - networkDtrLowMs);
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network DTR-low ") + QByteArray::number(lowMs)
                    + " ms post-uart2 variant " + networkVariant);
    portCycling = false;
    ignoreSerialErrorUntil = QDateTime::currentMSecsSinceEpoch() + 1500;
    done(true);
}

void Rt21Driver::cycleSerialPort(const std::function<void(bool)> &done)
{
    if (retired || !wantLink) {
        done(false);
        return;
    }
    if (networkPhase == NetworkPhase::AfterUart && networkUart2Ms > 0
        && QDateTime::currentMSecsSinceEpoch() < networkUart2Ms + kUartToDtrLowMs) {
        const qint64 deadline = networkUart2Ms + kUartToDtrLowMs;
        afterDeadline(deadline, [this, done]() {
            if (retired || networkPhase != NetworkPhase::AfterUart)
                return;
            cycleSerialPort(done);
        });
        return;
    }
    if (retired || !wantLink)
        return;
    const bool entry = networkPhase == NetworkPhase::AfterUart;
    if (!entry && networkPhase != NetworkPhase::Exiting)
        return;
    networkVariant = variantForThisTry();
    networkCycleReopen = networkVariant == 'D';
    portCycling = true;
    ignoreSerialErrorUntil = QDateTime::currentMSecsSinceEpoch() + 1500;
    networkDtrLowMs = QDateTime::currentMSecsSinceEpoch();
    networkDtrRoseProbeMs().store(0);
    TrafficLog::instance().recordSession("NOTE", QByteArray("network variant ") + networkVariant);
    if (entry && networkUart2Ms > 0) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network uart2->DTR low ")
                        + QByteArray::number(int(networkDtrLowMs - networkUart2Ms)) + " ms");
    }
    const int baud = endpoint.baud > 0 ? endpoint.baud : 4800;
#if defined(Q_OS_WIN)
    HANDLE handle = winRaw ? winRaw->handle : portHandle(serial);
    if (networkVariant == 'D' && !winRaw) {
        HANDLE live = portHandle(serial);
        const qint64 closeStarted = QDateTime::currentMSecsSinceEpoch();
        PortLeaveLines::apply(serial);
        if (live && live != INVALID_HANDLE_VALUE) {
            ::CancelIoEx(live, nullptr);
            ::SetCommMask(live, 0);
            ::EscapeCommFunction(live, CLRDTR);
            ::PurgeComm(live, PURGE_RXABORT | PURGE_RXCLEAR);
            ::PurgeComm(live, PURGE_TXABORT | PURGE_TXCLEAR);
        }
        if (serial)
            serial->close();
        const int closeMs = int(QDateTime::currentMSecsSinceEpoch() - closeStarted);
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network close ") + QByteArray::number(closeMs) + " ms");
        const QString device = endpoint.serialPort.startsWith(QStringLiteral("\\\\.\\"))
                                   ? endpoint.serialPort
                                   : QStringLiteral("\\\\.\\") + endpoint.serialPort;
        afterDeadline(networkDtrLowMs + kOpenAfterCloseMs, [this, done, baud, device, closeStarted]() {
            if (retired || !portCycling)
                return;
            const qint64 openStarted = QDateTime::currentMSecsSinceEpoch();
            HANDLE openedHandle = ::CreateFileW(reinterpret_cast<LPCWSTR>(device.utf16()), GENERIC_READ | GENERIC_WRITE,
                                                 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            bool opened = openedHandle != INVALID_HANDLE_VALUE;
            if (opened) {
                COMMPROP properties{};
                properties.wPacketLength = sizeof(COMMPROP);
                ::GetCommProperties(openedHandle, &properties);
                DWORD modem = 0;
                ::GetCommModemStatus(openedHandle, &modem);
                DCB dcb{};
                dcb.DCBlength = sizeof(DCB);
                dcb.BaudRate = baud;
                dcb.ByteSize = 8;
                dcb.Parity = NOPARITY;
                dcb.StopBits = ONESTOPBIT;
                dcb.fBinary = TRUE;
                dcb.fDtrControl = DTR_CONTROL_DISABLE;
                dcb.fRtsControl = RTS_CONTROL_ENABLE;
                dcb.XonChar = 0x11;
                dcb.XoffChar = 0x13;
                dcb.EofChar = 0x1A;
                dcb.EvtChar = 0x1A;
                dcb.XonLim = 0x200;
                dcb.XoffLim = 0x200;
                opened = ::SetCommState(openedHandle, &dcb);
                COMMTIMEOUTS timeouts{};
                timeouts.ReadIntervalTimeout = MAXDWORD;
                timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
                timeouts.ReadTotalTimeoutConstant = 2000;
                ::SetCommTimeouts(openedHandle, &timeouts);
                ::SetCommMask(openedHandle, 0x1FB);
                ::SetupComm(openedHandle, 4096, 4096);
            }
            const int openMs = int(QDateTime::currentMSecsSinceEpoch() - openStarted);
            const int closeOpenMs = int(QDateTime::currentMSecsSinceEpoch() - closeStarted);
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network open ") + QByteArray::number(openMs) + " ms");
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network close+open ") + QByteArray::number(closeOpenMs) + " ms");
            if (!opened) {
                if (openedHandle != INVALID_HANDLE_VALUE)
                    ::CloseHandle(openedHandle);
                restoreQtSerial();
                portCycling = false;
                done(false);
                return;
            }
            auto *raw = new WinRawSerial;
            raw->handle = openedHandle;
            raw->readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            raw->writeEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!raw->readEvent || !raw->writeEvent) {
                TrafficLog::instance().recordSession(
                    "NOTE", QByteArray("raw events failed err ") + QByteArray::number(int(::GetLastError())));
                closeWinRaw(raw);
                restoreQtSerial();
                portCycling = false;
                done(false);
                return;
            }
            winRaw = raw;
            TrafficLog::instance().recordSession("NOTE", QByteArray("network raw handle overlapped"));
            if (!networkRawTimer) {
                networkRawTimer = new QTimer(this);
                connect(networkRawTimer, &QTimer::timeout, this, [this]() { pollRawSerial(); });
            }
            networkRawTimer->start(5);
            framer.clear();
            pollRawSerial();
            afterDeadline(networkDtrLowMs + kDtrLowMs, [this, done, openedHandle]() {
                if (retired || !portCycling || !winRaw || winRaw->handle != openedHandle)
                    return;
                DCB dcb{};
                dcb.DCBlength = sizeof(DCB);
                if (::GetCommState(openedHandle, &dcb)) {
                    dcb.fDtrControl = DTR_CONTROL_ENABLE;
                    dcb.fRtsControl = RTS_CONTROL_ENABLE;
                    ::SetCommState(openedHandle, &dcb);
                }
                ::EscapeCommFunction(openedHandle, SETDTR);
                ::EscapeCommFunction(openedHandle, SETRTS);
                raiseLinesAndFinish(done);
            });
        });
        return;
    } else if (!handle || handle == INVALID_HANDLE_VALUE) {
        portCycling = false;
        done(false);
        return;
    } else if (networkVariant == 'B') {
        winLine(handle, SETRTS);
        winLine(handle, CLRDTR);
        winPurge(handle);
    } else if (networkVariant == 'A') {
        winLine(handle, CLRDTR);
        winLine(handle, CLRRTS);
        winPurge(handle);
    } else {
        winLine(handle, CLRDTR);
        winLine(handle, CLRRTS);
        winPurge(handle);
    }
    preciseSleepUntil(networkDtrLowMs + kDtrLowMs);
    winLine(handle, SETRTS);
    winLine(handle, SETDTR);
#else
    if (networkVariant == 'D') {
        if (!serial || !serial->isOpen()) {
            portCycling = false;
            done(false);
            return;
        }
        const qint64 closeStarted = QDateTime::currentMSecsSinceEpoch();
        const int fdBefore = int(serial->handle());
        if (fdBefore >= 0) {
            int low = 0;
            if (::ioctl(fdBefore, TIOCMGET, &low) == 0) {
                low &= ~TIOCM_DTR;
                ::ioctl(fdBefore, TIOCMSET, &low);
            }
            ::tcflush(fdBefore, TCIOFLUSH);
        }
        serial->close();
        const int closeMs = int(QDateTime::currentMSecsSinceEpoch() - closeStarted);
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network close ") + QByteArray::number(closeMs) + " ms");
        afterDeadline(networkDtrLowMs + kOpenAfterCloseMs, [this, done, baud, closeStarted]() {
            if (retired || !portCycling || !serial) {
                if (portCycling) {
                    portCycling = false;
                    done(false);
                }
                return;
            }
            serial->setBaudRate(baud);
            serial->setDataBits(QSerialPort::Data8);
            serial->setParity(QSerialPort::NoParity);
            serial->setStopBits(QSerialPort::OneStop);
            serial->setFlowControl(QSerialPort::NoFlowControl);
            const qint64 openStarted = QDateTime::currentMSecsSinceEpoch();
            const bool opened = serial->open(QIODevice::ReadWrite);
            const int openMs = int(QDateTime::currentMSecsSinceEpoch() - openStarted);
            const int closeOpenMs = int(QDateTime::currentMSecsSinceEpoch() - closeStarted);
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network open ") + QByteArray::number(openMs) + " ms");
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network close+open ") + QByteArray::number(closeOpenMs) + " ms");
            if (!opened) {
                portCycling = false;
                done(false);
                return;
            }
            const int fd = int(serial->handle());
            int bits = 0;
            if (fd >= 0 && ::ioctl(fd, TIOCMGET, &bits) == 0) {
                bits |= TIOCM_RTS;
                bits &= ~TIOCM_DTR;
                ::ioctl(fd, TIOCMSET, &bits);
            }
            framer.clear();
            afterDeadline(networkDtrLowMs + kDtrLowMs, [this, done]() {
                if (retired || !portCycling || !serial || !serial->isOpen())
                    return;
                const int raised = int(serial->handle());
                int lines = 0;
                if (raised >= 0 && ::ioctl(raised, TIOCMGET, &lines) == 0) {
                    lines |= TIOCM_DTR | TIOCM_RTS;
                    ::ioctl(raised, TIOCMSET, &lines);
                }
                raiseLinesAndFinish(done);
            });
        });
        return;
    } else if (!serial || !serial->isOpen()) {
        portCycling = false;
        done(false);
        return;
    }
    const int fd = int(serial->handle());
    int bits = 0;
    if (fd >= 0)
        ::ioctl(fd, TIOCMGET, &bits);
    if (networkVariant == 'B')
        bits |= TIOCM_RTS;
    else
        bits &= ~TIOCM_RTS;
    bits &= ~TIOCM_DTR;
    if (fd >= 0) {
        ::ioctl(fd, TIOCMSET, &bits);
        ::tcflush(fd, TCIOFLUSH);
    }
    preciseSleepUntil(networkDtrLowMs + kDtrLowMs);
    if (fd >= 0 && ::ioctl(fd, TIOCMGET, &bits) == 0) {
        bits |= TIOCM_DTR | TIOCM_RTS;
        ::ioctl(fd, TIOCMSET, &bits);
    }
#endif
    framer.clear();
    raiseLinesAndFinish(done);
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

bool Rt21Driver::bridgeMayBeOpen() const
{
    return bridgeLatched || serialRouteT11 || networkT11Watch || networkEntered
           || networkPhase != NetworkPhase::Idle;
}

void Rt21Driver::releaseBridge()
{
    if (!bridgeMayBeOpen() && !bridgeLatched)
        return;
    writeEsc();
}

void Rt21Driver::afterDeadline(qint64 deadlineMs, const std::function<void()> &fn)
{
    if (retired)
        return;
    const int remain = int(deadlineMs - QDateTime::currentMSecsSinceEpoch());
    if (remain <= 0) {
        fn();
        return;
    }
    QTimer::singleShot(qMin(remain, 10), this, [this, deadlineMs, fn]() { afterDeadline(deadlineMs, fn); });
}

void Rt21Driver::writeEsc()
{
    const QByteArray esc(1, '\x1b');
    const bool liveBefore =
#if defined(Q_OS_WIN)
        winRaw ||
#endif
        (serial && serial->isOpen()) || (io && io->isOpen());
#if defined(Q_OS_WIN)
    if (!winRaw && !(serial && serial->isOpen()))
        restoreQtSerial();
#else
    if (!(serial && serial->isOpen()) && !(io && io->isOpen()))
        restoreQtSerial();
#endif
    const bool live =
#if defined(Q_OS_WIN)
        winRaw ||
#endif
        (serial && serial->isOpen()) || (io && io->isOpen());
    if (!live) {
        if (liveBefore || serialRouteT11 || networkT11Watch || networkEntered)
            bridgeLatched = true;
        return;
    }
    sendNetworkBytes(esc);
    networkEntered = false;
    bridgeLatched = false;
    serialRouteT11 = false;
    networkT11Watch = false;
    TrafficLog::instance().recordSession("NOTE", QByteArray("network ESC"));
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
        if ((serial && serial->isOpen()) || winRaw)
            writeEsc();
        return;
    }
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    networkPhase = NetworkPhase::Exiting;
    networkTxQuiet = false;
    writeEsc();
    finishNetworkExit();
}

void Rt21Driver::recordHeadingMiss()
{
    headingMissAt.append(QDateTime::currentMSecsSinceEpoch());
}

bool Rt21Driver::powerCycleDue() const
{
    // Three BI1 misses, and the oldest of those three at least 10 s ago.
    if (headingMissAt.size() < 3)
        return false;
    const qint64 oldest = headingMissAt.at(headingMissAt.size() - 3);
    return QDateTime::currentMSecsSinceEpoch() - oldest >= 10000;
}

void Rt21Driver::logControlBytes(int before)
{
    const int now = framer.controlBytes();
    if (now <= before)
        return;
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("control bytes stripped ") + QByteArray::number(now - before) + " running "
                    + QByteArray::number(now));
}

void Rt21Driver::beginPassThroughQuiet()
{
    if (retired || networkPhase == NetworkPhase::Quiet)
        return;
    ++networkQuiets;
    networkQuietFromWarmup = networkPhase == NetworkPhase::Warmup;
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("no heading after exit, ESC then 35 s with no TX"));
    networkPhase = NetworkPhase::Quiet;
    networkMode = true;
    configMode = true;
    awaitHeadingAfterNetwork = false;
    if (networkTimer)
        networkTimer->stop();
    if (pollTimer)
        pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    networkEntered = true;
    writeEsc();
    restoreQtSerial();
    networkTxQuiet = true;
    QTimer::singleShot(kPassThroughQuietMs, this, [this]() {
        if (retired || networkPhase != NetworkPhase::Quiet)
            return;
        networkTxQuiet = false;
        TrafficLog::instance().recordSession("NOTE", QByteArray("network quiet done, one poll"));
        if (!io) {
            abortNetworkRead(QStringLiteral("Controller stopped answering. Power-cycle the RT-21."));
            return;
        }
        Job job;
        job.kind = Kind::Heading;
        job.bytes = Rt21::queryTenths();
        job.note = QStringLiteral("post-quiet");
        queue.enqueue(job);
        pump();
    });
}

void Rt21Driver::abortNetworkRead(const QString &text)
{
    if (bridgeMayBeOpen())
        releaseBridge();
    networkTxQuiet = false;
    networkPhase = NetworkPhase::Idle;
    networkAnotherCycle = false;
    networkMode = false;
    configMode = false;
    networkEntered = false;
    awaitHeadingAfterNetwork = false;
    networkCancel = false;
    if (networkTimer)
        networkTimer->stop();
    if (pollTimer)
        pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    const bool writing = networkWriting;
    networkWriting = false;
    networkWriteName.clear();
    TrafficLog::instance().recordSession("NOTE", text.toLatin1());
    restoreQtSerial();
    startPoll();
    if (writing)
        emit writeFinished(false, text);
    else
        emit networkFinished(false, text);
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
    networkLateBytes.clear();
    networkEchoSends = 0;
    networkEchoSawReply = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (networkReopenedMs > 0) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network DTR high->ECHO ") + QByteArray::number(int(now - networkReopenedMs)) + " ms");
    }
    if (networkUart2Ms > 0) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network uart2->ECHO ") + QByteArray::number(int(now - networkUart2Ms)) + " ms");
    }
    resendEcho();
}

void Rt21Driver::resendEcho()
{
    if (networkPhase == NetworkPhase::Exiting || networkCancel || retired)
        return;
    if (networkEchoSends >= 2) {
        TrafficLog::instance().recordSession("NOTE", QByteArray("network echo gave no OK, listening 400 ms for late bytes"));
        networkPhase = NetworkPhase::WaitEchoLate;
        armNetworkTimer(400);
        return;
    }
    ++networkEchoSends;
    networkEchoTxMs = QDateTime::currentMSecsSinceEpoch();
    networkPhase = NetworkPhase::WaitEcho;
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network echo send ") + QByteArray::number(networkEchoSends));
    sendNetworkBytes(QByteArray("AT*SERIAL_ECHO=0\n"));
    armNetworkTimer(kEchoReplyMs);
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
    networkWriteReadStep = 0;
    networkPhase = NetworkPhase::WriteAck;
    framer.setLineMode(true);
    sendNetworkBytes(networkWriteCommand.toLatin1());
    armNetworkTimer(700);
}

void Rt21Driver::onNetworkTimeout()
{
    if (networkPhase == NetworkPhase::WaitUart) {
        TrafficLog::instance().recordSession("NOTE", QByteArray("network uart2 timeout"));
        beginNetworkExit();
        return;
    }
    if (networkPhase == NetworkPhase::WaitEcho) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network echo no reply in ") + QByteArray::number(kEchoReplyMs)
                        + " ms, not resending into silence");
        networkPhase = NetworkPhase::WaitEchoLate;
        armNetworkTimer(400);
        return;
    }
    if (networkPhase == NetworkPhase::WaitEchoLate) {
        TrafficLog::instance().recordSession(
            "NOTE", networkLateBytes.isEmpty()
                        ? QByteArray("network echo late bytes none")
                        : QByteArray("network echo late bytes ") + networkLateBytes.toHex(' '));
        // <uart2>; with no echo bytes is TCP mode after a power cycle.
        // ERROR 1 means the module already answered, so this is not that case.
        if (networkUart2Ms > 0 && !networkEchoSawReply && networkLateBytes.isEmpty() && !networkWakeSent)
            networkColdBootPending = true;
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
    if (networkPhase == NetworkPhase::Idle || networkPhase == NetworkPhase::Warmup
        || networkPhase == NetworkPhase::Quiet || networkPhase == NetworkPhase::PreAt
        || networkPhase == NetworkPhase::LeadIn || networkPhase == NetworkPhase::BridgeW)
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
        if (!line.contains(QStringLiteral("<uart2>"), Qt::CaseInsensitive))
            return true;
        if (networkTimer)
            networkTimer->stop();
        networkPhase = NetworkPhase::AfterUart;
        networkUart2Ms = QDateTime::currentMSecsSinceEpoch();
        networkT11Watch = false;
        TrafficLog::instance().recordSession("NOTE", QByteArray("network uart2"));
        QTimer::singleShot(0, this, [this]() {
            if (retired || networkPhase != NetworkPhase::AfterUart)
                return;
            cycleSerialPort([this](bool ok) {
                if (!ok) {
                    beginNetworkExit();
                    return;
                }
                if (networkExitAfterCycle || networkCancel || networkPhase == NetworkPhase::Exiting) {
                    networkExitAfterCycle = false;
                    networkPhase = NetworkPhase::Idle;
                    beginNetworkExit();
                    return;
                }
                sendEcho();
            });
        });
        return true;
    }

    const bool errorLine = line.contains(QStringLiteral("ERROR"), Qt::CaseInsensitive);
    const bool okLine = !errorLine && line.contains(QStringLiteral("OK"), Qt::CaseInsensitive);
    if (networkPhase == NetworkPhase::WaitEcho || networkPhase == NetworkPhase::WaitEchoLate) {
        networkEchoSawReply = true;
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network echo rx ") + line.toLatin1() + " " + frame.toHex(' '));
        if (errorLine && networkPhase == NetworkPhase::WaitEcho) {
            if (networkTimer)
                networkTimer->stop();
            resendEcho();
            return true;
        }
        if (!okLine)
            return true;
        if (networkTimer)
            networkTimer->stop();
        TrafficLog::instance().recordSession(
            "NOTE", networkPhase == NetworkPhase::WaitEchoLate ? QByteArray("network echo arrived late")
                                                              : QByteArray("network echo ok"));
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

    if (networkPhase == NetworkPhase::Query) {
        const QList<Rt21::ModuleQuery> rows = Rt21::networkReadSequence();
        if (networkQueryIndex >= rows.size())
            return true;
        const Rt21::ModuleQuery query = rows.at(networkQueryIndex);
        const auto parsed = Rt21::parseModuleLine(line.toLatin1());
        QString expectKey = query.token;
        if (expectKey.endsWith(QLatin1Char('=')))
            expectKey.chop(1);
        if (!parsed || parsed->key.compare(expectKey, Qt::CaseInsensitive) != 0)
            return true;
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network query rx ") + Rt21::redactForLog(line.toLatin1()));
        if (networkTimer)
            networkTimer->stop();
        const QString value = parsed->value;
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
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network write rx ") + line.toLatin1() + " " + frame.toHex(' '));
        if (networkTimer)
            networkTimer->stop();
        networkPhase = NetworkPhase::WriteRead;
        networkWriteReadStep = 0;
        sendNetworkBytes(QByteArray("AT*AGW?\n"));
        armNetworkTimer(300);
        return true;
    }

    if (networkPhase == NetworkPhase::WriteRead) {
        const auto parsedLine = Rt21::parseModuleLine(line.toLatin1());
        const bool agwLine = parsedLine && parsedLine->key.compare(QLatin1String("AGW"), Qt::CaseInsensitive) == 0;
        const bool gwLine = parsedLine && parsedLine->key.compare(QLatin1String("GW"), Qt::CaseInsensitive) == 0;
        if (networkWriteReadStep == 0) {
            if (!agwLine)
                return true;
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network query rx ") + Rt21::redactForLog(line.toLatin1()));
            if (networkTimer)
                networkTimer->stop();
            const QString value = parsedLine->value;
            networkWriteOk = value == networkWriteExpect;
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network read-back ") + value.toLatin1()
                            + (networkWriteOk ? " match" : " mismatch"));
            emit networkField(QStringLiteral("agw"), value);
            networkWriteReadStep = 1;
            sendNetworkBytes(QByteArray("AT*GW?\n"));
            armNetworkTimer(300);
            return true;
        }
        if (!gwLine)
            return true;
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network query rx ") + Rt21::redactForLog(line.toLatin1()));
        if (networkTimer)
            networkTimer->stop();
        emit networkField(QStringLiteral("gw"), parsedLine->value);
        beginNetworkExit();
        return true;
    }
    return true;
}

void Rt21Driver::beginNetworkWarmup(int ms)
{
    networkPhase = NetworkPhase::Warmup;
    networkMode = true;
    configMode = true;
    networkWarmupMs = ms;
    if (networkWarmupLimitMs <= 0)
        networkWarmupLimitMs = kWarmupCapMs;
    const int cap = networkWarmupLimitMs;
    networkWarmupLimitMs = kWarmupCapMs;
    networkWarmupCapActive = cap;
    networkWarmupHardStartMs = QDateTime::currentMSecsSinceEpoch();
    networkWarmupStartMs = networkWarmupHardStartMs;
    networkWarmupStep = 0;
    networkWarmupSawHeading = false;
    networkWarmupSawR01 = false;
    networkWarmupSawR21 = false;
    QTimer::singleShot(cap, this, [this]() {
        if (networkPhase == NetworkPhase::Warmup)
            onWarmupMiss();
    });
    if (pollTimer)
        pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network warmup ") + QByteArray::number(ms) + " ms of answered polls before AT;");
    sendWarmupPoll();
}

void Rt21Driver::sendWarmupPoll()
{
    if (networkPhase != NetworkPhase::Warmup || retired)
        return;
    if (!io) {
        abortNetworkRead(QStringLiteral("Connect to a rotor before reading the network module."));
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (networkWarmupHardStartMs > 0 && now - networkWarmupHardStartMs >= networkWarmupCapActive) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network warmup capped at ") + QByteArray::number(networkWarmupCapActive / 1000) + " s");
        beginPassThroughQuiet();
        return;
    }
    const QByteArray commands[] = {Rt21::queryStatus(), Rt21::queryPot()};
    Job job;
    job.kind = Kind::Heading;
    job.bytes = commands[networkWarmupStep % 2];
    job.note = QStringLiteral("warmup");
    ++networkWarmupStep;
    queue.enqueue(job);
    pump();
}

void Rt21Driver::onWarmupAnswer(const QByteArray &command)
{
    if (networkPhase != NetworkPhase::Warmup || networkCancel || retired)
        return;
    const QByteArray upper = command.toUpper();
    networkQuiets = 0;
    if (upper.contains("R01"))
        networkWarmupSawR01 = true;
    else if (upper.contains("R21"))
        networkWarmupSawR21 = true;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - networkWarmupStartMs;
    if (elapsed >= networkWarmupMs && networkWarmupSawR01 && networkWarmupSawR21) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network warmup done ") + QByteArray::number(int(elapsed)) + " ms");
        beginBridgeRearm();
        return;
    }
    const int gap = upper.contains("R21") ? kPairAfterR21Ms : kPairAfterR01Ms;
    QTimer::singleShot(gap, this, [this]() {
        if (networkPhase == NetworkPhase::Warmup && !networkCancel)
            sendWarmupPoll();
    });
}

void Rt21Driver::onWarmupMiss()
{
    if (networkPhase != NetworkPhase::Warmup || retired)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (networkWarmupHardStartMs > 0 && now - networkWarmupHardStartMs >= networkWarmupCapActive) {
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network warmup capped at ") + QByteArray::number(networkWarmupCapActive / 1000) + " s");
        beginPassThroughQuiet();
        return;
    }
    networkWarmupStartMs = now;
    networkWarmupSawR01 = false;
    networkWarmupSawR21 = false;
    TrafficLog::instance().recordSession("NOTE", QByteArray("network warmup poll missed"));
    QTimer::singleShot(kWarmupGapMs, this, [this]() {
        if (networkPhase == NetworkPhase::Warmup && !networkCancel)
            sendWarmupPoll();
    });
}

void Rt21Driver::enqueueVersionAfterHeading()
{
    Job version;
    version.kind = Kind::Note;
    version.bytes = Rt21::queryVersion();
    version.note = QStringLiteral("route-version");
    queue.enqueue(version);
    pump();
}

void Rt21Driver::beginSerialRoute()
{
    // A plain serial connect does not enter the bridge. BI1 goes out first
    // so the heading is on screen before R11 asks for the version.
    serialRoute = true;
    serialRouteT11 = false;
    if (bridgeLatched || networkT11Watch)
        releaseBridge();
    if (!io || endpoint.transport == RotorEndpoint::Transport::Tcp) {
        startPoll();
        return;
    }
    queue.clear();
    inflight.reset();
    Job heading;
    heading.kind = Kind::Heading;
    heading.bytes = Rt21::queryTenths();
    heading.note = QStringLiteral("connect-heading");
    queue.enqueue(heading);
    pump();
}

void Rt21Driver::beginBridgeRearm()
{
    if (!kRearmRegisterW) {
        holdQuietThenEnter();
        return;
    }
    if (retired || networkCancel || !io) {
        abortNetworkRead(QStringLiteral("Register W did not answer, so the network read was not started."));
        return;
    }
    networkPhase = NetworkPhase::BridgeW;
    networkBridgeStep = 1;
    networkBridgeValue = -1;
    sendBridgeCommand(QByteArray("RW1;"), QByteArray("network bridge read RW1;"));
}

void Rt21Driver::sendBridgeCommand(const QByteArray &bytes, const QByteArray &note)
{
    Job job;
    job.kind = Kind::Read;
    job.id = QLatin1Char('W');
    job.bytes = bytes;
    job.note = QStringLiteral("bridge");
    TrafficLog::instance().recordSession("NOTE", note);
    queue.clear();
    inflight.reset();
    replyTimer->stop();
    queue.enqueue(job);
    pump();
}

void Rt21Driver::onBridgeReply(const QByteArray &frame)
{
    if (networkPhase != NetworkPhase::BridgeW || retired)
        return;
    const QByteArray upper = frame.toUpper();
    if (upper == "RW1" || upper.startsWith("WW0"))
        return;
    if (networkBridgeStep == 1 || networkBridgeStep == 3) {
        const bool marked = !frame.isEmpty() && (frame.at(0) == 'W' || frame.at(0) == 'w');
        const auto value = marked ? Rt21::parseIntegerPayload(frame) : std::nullopt;
        if (!value) {
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network bridge RW1 unparsed ") + frame.toHex(' '));
            abortNetworkRead(QStringLiteral("Register W did not parse, so the network read was not started."));
            return;
        }
        replyTimer->stop();
        inflight.reset();
        if (networkBridgeStep == 1) {
            networkBridgeValue = *value;
            const QByteArray write = Rt21::registerWriteCommand(QLatin1Char('W'), *value);
            if (!write.startsWith("WW0")) {
                abortNetworkRead(QStringLiteral(
                    "Register W write was not a write-back, so the network read was not started."));
                return;
            }
            networkBridgeStep = 2;
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network bridge value ") + QByteArray::number(*value));
            sendBridgeCommand(write, QByteArray("network bridge write ") + write);
            return;
        }
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network bridge reread ") + QByteArray::number(*value));
        if (*value != networkBridgeValue) {
            abortNetworkRead(QStringLiteral("Register W read back %1, not %2, so the network read was not started.")
                                 .arg(*value)
                                 .arg(networkBridgeValue));
            return;
        }
        holdQuietThenEnter();
        return;
    }
    if (networkBridgeStep == 2) {
        const bool ack = frame.size() >= 2 && static_cast<unsigned char>(frame.at(0)) == 0x06
                         && (frame.at(1) == 'W' || frame.at(1) == 'w');
        if (!ack) {
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network bridge ack unparsed ") + frame.toHex(' '));
            abortNetworkRead(QStringLiteral(
                "Register W write was not acknowledged, so the network read was not started."));
            return;
        }
        replyTimer->stop();
        inflight.reset();
        networkBridgeStep = 3;
        TrafficLog::instance().recordSession("NOTE", QByteArray("network bridge ack <06>W;"));
        sendBridgeCommand(QByteArray("RW1;"), QByteArray("network bridge reread RW1;"));
    }
}

void Rt21Driver::holdQuietThenEnter()
{
    if (retired || (networkPhase != NetworkPhase::Warmup && networkPhase != NetworkPhase::BridgeW))
        return;
    const qint64 replyMs = QDateTime::currentMSecsSinceEpoch();
    networkPhase = NetworkPhase::PreAt;
    networkTxQuiet = true;
    if (pollTimer)
        pollTimer->stop();
    replyTimer->stop();
    inflight.reset();
    queue.clear();
    framer.clear();
    afterDeadline(replyMs + kPreAtQuietMs, [this, replyMs]() {
        if (retired || networkPhase != NetworkPhase::PreAt)
            return;
        networkTxQuiet = false;
        framer.clear();
        const int quiet = int(QDateTime::currentMSecsSinceEpoch() - replyMs);
        TrafficLog::instance().recordSession(
            "NOTE", QByteArray("network pre-AT quiet ") + QByteArray::number(quiet) + " ms");
        if (networkCancel || !wantLink) {
            beginNetworkExit();
            return;
        }
        startNetworkPass();
    });
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
    networkUart2Ms = 0;
    networkReopenedMs = 0;
    networkVariant = variantForThisTry();
    networkCycleReopen = networkVariant == 'D';
    networkPhase = NetworkPhase::WaitUart;
    networkMode = true;
    configMode = true;
    // T11 mutes the controller for tens of seconds. The utility never sends
    // it before AT;. Entry is the answered R21/R01 polls, then a bare AT;.
    if (bridgeLatched || serialRouteT11 || networkT11Watch)
        releaseBridge();
    const QByteArray enter("AT;");
    networkEntered = true;
    networkAtMs = QDateTime::currentMSecsSinceEpoch();
    TrafficLog::instance().recordSession(
        "NOTE", QByteArray("network enter AT; expecting <uart2>; variant ") + networkVariant);
    sendNetworkBytes(enter);
    armNetworkTimer(1000);
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
    if (!serial || !serial->isOpen()) {
        emit networkFinished(false, QStringLiteral("Connect to a rotor before reading the network module."));
        return;
    }
    if (brightnessQueued >= 0 || brightnessCapture || brightnessWatching || brightnessHold) {
        TrafficLog::instance().recordSession("NOTE", QByteArray("network read held for brightness write"));
        emit networkFinished(false, QStringLiteral("Network read waits until the brightness write finishes."));
        return;
    }
    networkWriting = false;
    networkCancel = false;
    networkQuiets = 0;
    networkQuietFromWarmup = false;
    networkCycle = 1;
    networkGotField = false;
    networkEntered = false;
    networkWakeSent = false;
    networkColdBootPending = false;
    networkReadOkThisSession = false;
    loadCycleOrigin();
    beginNetworkCycle();
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
        networkQuiets = 0;
        networkQuietFromWarmup = false;
        networkCycle = 1;
        networkGotField = false;
        networkWriteOk = false;
        networkEntered = false;
        networkWakeSent = false;
        networkColdBootPending = false;
        loadCycleOrigin();
        beginNetworkCycle();
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
    if (key != QLatin1String("agw")) {
        emit writeFinished(false, QStringLiteral("Only the gateway can be written right now."));
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
    const QString trimmed = value.trimmed();
    const QStringList octets = trimmed.split(QLatin1Char('.'));
    QStringList normalized;
    bool dotted = octets.size() == 4;
    for (const QString &octet : octets) {
        if (!dotted)
            break;
        if (octet.isEmpty() || octet.size() > 3) {
            dotted = false;
            break;
        }
        for (const QChar c : octet) {
            if (!c.isDigit())
                dotted = false;
        }
        bool ok = false;
        const int n = octet.toInt(&ok);
        if (!ok || n < 0 || n > 255)
            dotted = false;
        if (dotted)
            normalized.append(QString::number(n));
    }
    if (!dotted) {
        emit writeFinished(false, QStringLiteral("Gateway needs an IPv4 address."));
        return;
    }
    const QString address = normalized.join(QLatin1Char('.'));
    const QString command = QStringLiteral("AT*AGW=%1\n").arg(address);
    networkWriteExpect = address;
    networkReadBackCommand = QByteArray("AT*AGW?\n");
    networkReadBackToken = QStringLiteral("AGW=");
    networkWriteReadStep = 0;
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
    restoreQtSerial();
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
    networkTxQuiet = false;
    if (networkPhase == NetworkPhase::Quiet)
        networkPhase = NetworkPhase::Idle;
    TrafficLog::instance().recordSession(
        "NOTE", headingSeen ? QByteArray("heading resumed after network")
                            : QByteArray("no heading after network exit"));
    if (!headingSeen && !networkCancel) {
        if (powerCycleDue()) {
            abortNetworkRead(QStringLiteral("Controller stopped answering. Power-cycle the RT-21."));
            return;
        }
        // One missed BI1 is not the end of the 12 tries.
    }
    if (networkQuietFromWarmup && !networkCancel && io && networkCycle <= kNetworkTries) {
        networkQuietFromWarmup = false;
        beginNetworkCycle();
        return;
    }
    if (networkAnotherCycle && !networkCancel && io && networkCycle < kNetworkTries) {
        ++networkCycle;
        if (networkColdBootPending) {
            // The controller is in TCP mode. Waiting 35 s does not leave it.
            // Wake once, then the normal answered polls. Do not put T11 next to AT;.
            networkColdBootPending = false;
            networkAnotherCycle = false;
            beginColdBootWake();
            return;
        }
        // The ~31 s pass-through drop only starts once <uart2>; has been
        // seen. A try that never entered (no <uart2>;) can go again at once.
        int delay = 0;
        if (networkUart2Ms > 0 && networkAtMs > 0) {
            const int prelude = kLeadInMs + kWarmupRetryMs + kPreAtQuietMs + 500;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qint64 earliest = networkAtMs + kPassThroughQuietMs - prelude;
            delay = int(std::max(qint64(0), earliest - now));
        }
        if (delay > 0) {
            TrafficLog::instance().recordSession(
                "NOTE", QByteArray("network next AT; waits ") + QByteArray::number(delay)
                            + " ms so attempts stay 35 s apart");
            emit networkStatus(QStringLiteral("Waiting for the module to leave pass-through (try %1 of %2)…")
                                   .arg(networkCycle)
                                   .arg(kNetworkTries));
            QTimer::singleShot(delay, this, [this]() {
                if (retired || networkCancel || !networkAnotherCycle)
                    return;
                networkAnotherCycle = false;
                beginNetworkCycle();
            });
            return;
        }
        networkAnotherCycle = false;
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
