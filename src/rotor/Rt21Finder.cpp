/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Finder.h"

#include "Rt21Protocol.h"
#include "SerialLines.h"
#include "TrafficLog.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

QString headingText(double heading)
{
    if (std::abs(heading - std::round(heading)) < 0.05)
        return QString::number(qRound(heading));
    return QString::number(heading, 'f', 1);
}

std::optional<double> readHeading(QIODevice *device, int timeoutMs, QByteArray *raw)
{
    Rt21::Framer framer;
    std::optional<double> heading;
    auto take = [&]() {
        const QByteArray chunk = device->readAll();
        if (raw)
            raw->append(chunk);
        framer.append(chunk);
        for (const QByteArray &frame : framer.take()) {
            if (const auto parsed = Rt21::parseHeadingFrame(frame)) {
                heading = parsed;
                return true;
            }
        }
        return false;
    };
    if (device->bytesAvailable() > 0 && take())
        return heading;
    QEventLoop loop;
    QTimer cap;
    cap.setSingleShot(true);
    QObject::connect(&cap, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(device, &QIODevice::readyRead, &loop, [&]() {
        if (take())
            loop.quit();
    });
    cap.start(timeoutMs);
    loop.exec();
    if (device->bytesAvailable() > 0)
        take();
    return heading;
}

std::optional<QString> readVersion(QIODevice *device, int timeoutMs, QByteArray *raw)
{
    std::optional<QString> version;
    Rt21::Framer framer;
    auto take = [&]() {
        const QByteArray chunk = device->readAll();
        if (raw)
            raw->append(chunk);
        framer.append(chunk);
        for (const QByteArray &frame : framer.take()) {
            const QString text = Rt21::payloadAfterSoh(frame);
            if (text.contains(QStringLiteral("RT-21"))) {
                version = text;
                return true;
            }
        }
        return false;
    };
    if (device->bytesAvailable() > 0 && take())
        return version;
    QEventLoop loop;
    QTimer cap;
    cap.setSingleShot(true);
    QObject::connect(&cap, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(device, &QIODevice::readyRead, &loop, [&]() {
        if (take())
            loop.quit();
    });
    cap.start(timeoutMs);
    loop.exec();
    if (device->bytesAvailable() > 0)
        take();
    return version;
}

} // namespace

QString usbChipName(const QString &manufacturer, const QString &description)
{
    const QString blob = (manufacturer + QLatin1Char(' ') + description).toLower();
    if (blob.contains(QStringLiteral("ftdi")))
        return QStringLiteral("FTDI");
    if (blob.contains(QStringLiteral("prolific")))
        return QStringLiteral("Prolific");
    if (blob.contains(QStringLiteral("ch340")) || blob.contains(QStringLiteral("ch341"))
        || blob.contains(QStringLiteral("wch")))
        return QStringLiteral("CH340");
    if (blob.contains(QStringLiteral("cp210")) || blob.contains(QStringLiteral("silicon labs")))
        return QStringLiteral("CP210");
    return {};
}

QString foundRotorLabel(const QString &place, const QString &kind, int baud, double heading)
{
    const QString text = headingText(heading);
    if (baud > 0) {
        return QStringLiteral("RT-21 on %1 (%2, %3), heading %4°").arg(place, kind).arg(baud).arg(text);
    }
    return QStringLiteral("RT-21 at %1 (network), heading %2°").arg(place, text);
}

bool isVirtualAdapterName(const QString &name)
{
    const QString folded = name.toLower();
    const QStringList markers = {QStringLiteral("virtual"), QStringLiteral("vpn"),  QStringLiteral("tun"),
                                 QStringLiteral("tap"),     QStringLiteral("wsl"),  QStringLiteral("hyper-v"),
                                 QStringLiteral("vbox"),    QStringLiteral("vmware"), QStringLiteral("bluetooth"),
                                 QStringLiteral("loopback"), QStringLiteral("npcap"), QStringLiteral("pseudo"),
                                 QStringLiteral("teredo"),  QStringLiteral("isatap"), QStringLiteral("docker")};
    for (const QString &marker : markers) {
        if (folded.contains(marker))
            return true;
    }
    return false;
}

QList<int> networkCandidatePorts(const QList<int> &extra)
{
    QList<int> ports = {23, 4001, 5000, 6555, 6665, 8080, 10001};
    for (int port : extra) {
        if (port > 0 && port <= 65535 && !ports.contains(port))
            ports.append(port);
    }
    return ports;
}

std::optional<FoundRotor> Rt21Finder::probeSerial(const QString &portName, const QString &kind, int baud, int timeoutMs)
{
    if (portName.trimmed().isEmpty())
        return std::nullopt;
    QSerialPort port;
    port.setPortName(portName);
    port.setBaudRate(baud);
    port.setDataBits(QSerialPort::Data8);
    port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);
    port.setFlowControl(QSerialPort::NoFlowControl);
    const QByteArray where = portName.toLatin1() + " " + QByteArray::number(baud);
    TrafficLog::instance().recordSession("FIND", QByteArray("probe ") + where + " " + kind.toLatin1());
    // In use, missing, or a virtual port another program owns: skip it quietly.
    if (!port.open(QIODevice::ReadWrite)) {
        TrafficLog::instance().recordSession("FIND", QByteArray("open failed ") + where);
        return std::nullopt;
    }
    raiseDtrAndRts(&port);
    const QByteArray query = Rt21::queryTenths();
    TrafficLog::instance().recordSession("FIND-TX", query);
    port.write(query);
    port.flush();
    QByteArray rx;
    const auto heading = readHeading(&port, timeoutMs, &rx);
    TrafficLog::instance().recordSession("FIND-RX", rx);
    TrafficLog::instance().recordSession(
        "FIND", heading ? QByteArray("hit ") + where : QByteArray("no reply ") + where);
    // Deliver the port's last completion before its notifier is destroyed.
    // Closing and then destroying it as the worker thread exits crashes in
    // ntdll on Windows (overlapped I/O into a freed port).
    // Destroy the port on this thread, with no event-loop turn in between.
    // processEvents after close delivered a freed overlapped read into
    // ntdll (c0000005 at ntdll+0x69a21) on the Find worker.
    port.blockSignals(true);
    port.clear(QSerialPort::AllDirections);
    if (port.isOpen())
        port.close();
    if (!heading)
        return std::nullopt;
    FoundRotor found;
    found.endpoint.transport = RotorEndpoint::Transport::Serial;
    found.endpoint.serialPort = portName;
    found.endpoint.baud = baud;
    found.heading = *heading;
    const QString shown = kind.isEmpty() ? QStringLiteral("serial") : kind;
    found.label = foundRotorLabel(portName, shown, baud, *heading);
    return found;
}

std::optional<FoundRotor> Rt21Finder::probeTcp(const QString &host, int port, int timeoutMs)
{
    if (host.trimmed().isEmpty() || port <= 0)
        return std::nullopt;
    QTcpSocket socket;
    socket.connectToHost(host, quint16(port));
    const QByteArray place = host.toLatin1() + ":" + QByteArray::number(port);
    if (!socket.waitForConnected(timeoutMs)) {
        TrafficLog::instance().recordSession("FIND-NET", QByteArray("connect failed ") + place);
        socket.abort();
        return std::nullopt;
    }
    const QByteArray where = host.toLatin1() + ":" + QByteArray::number(port);
    TrafficLog::instance().recordSession("FIND-NET", QByteArray("connect ") + where);
    socket.write(Rt21::queryTenths());
    socket.flush();
    QByteArray rx;
    const auto heading = readHeading(&socket, timeoutMs, &rx);
    if (!heading) {
        TrafficLog::instance().recordSession("FIND-NET", QByteArray("no heading ") + where);
        TrafficLog::instance().recordSession("FIND-NET", rx);
        socket.abort();
        return std::nullopt;
    }
    socket.write(Rt21::queryVersion());
    socket.flush();
    const auto version = readVersion(&socket, timeoutMs, &rx);
    socket.disconnectFromHost();
    socket.close();
    if (!version) {
        TrafficLog::instance().recordSession("FIND-NET", QByteArray("no version ") + where);
        TrafficLog::instance().recordSession("FIND-NET", rx);
        return std::nullopt;
    }
    TrafficLog::instance().recordSession(
        "FIND-NET", QByteArray("hit ") + where + " " + version->toLatin1());
    TrafficLog::instance().recordSession("FIND-NET", rx);
    FoundRotor found;
    found.endpoint.transport = RotorEndpoint::Transport::Tcp;
    found.endpoint.host = host;
    found.endpoint.tcpPort = port;
    found.heading = *heading;
    found.label = foundRotorLabel(QStringLiteral("%1:%2").arg(host).arg(port), QString(), 0, *heading)
                  + QStringLiteral(", ") + *version;
    return found;
}

QList<int> serialProbeBauds()
{
    return {4800, 9600};
}

bool findReplacesSavedEndpoint(const RotorEndpoint &saved, bool linkUp, const RotorEndpoint &found)
{
    if (linkUp || saved.transport != RotorEndpoint::Transport::Tcp || !saved.configured() || !found.configured())
        return false;
    if (found.transport == RotorEndpoint::Transport::Tcp && found.host == saved.host && found.tcpPort == saved.tcpPort)
        return false;
    return true;
}

int tcpPortAfterFind(int savedPort, const RotorEndpoint &found)
{
    if (found.transport == RotorEndpoint::Transport::Tcp && found.tcpPort > 0 && found.tcpPort <= 65535)
        return found.tcpPort;
    return savedPort;
}

int baudKeptAfterFind(int savedBaud, int foundBaud)
{
    if (foundBaud == 4800 || foundBaud == 9600)
        return foundBaud;
    return savedBaud == 9600 ? 9600 : 4800;
}

QString plainLinkFailure(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("Unknown error"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("The rotor closed the connection.");
    return trimmed;
}

namespace {

bool portLess(const QString &a, const QString &b)
{
    // COM1, COM2, COM10, and cu.usbserial-2 before cu.usbserial-10.
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            const int iStart = i;
            const int jStart = j;
            while (i < a.size() && a.at(i) == QLatin1Char('0'))
                ++i;
            while (j < b.size() && b.at(j) == QLatin1Char('0'))
                ++j;
            const int iDigits = i;
            const int jDigits = j;
            while (i < a.size() && a.at(i).isDigit())
                ++i;
            while (j < b.size() && b.at(j).isDigit())
                ++j;
            const int lengthA = i - iDigits;
            const int lengthB = j - jDigits;
            if (lengthA != lengthB)
                return lengthA < lengthB;
            const int compared = a.mid(iDigits, lengthA).compare(b.mid(jDigits, lengthB));
            if (compared != 0)
                return compared < 0;
            const int zerosA = iDigits - iStart;
            const int zerosB = jDigits - jStart;
            if (zerosA != zerosB)
                return zerosA < zerosB;
            continue;
        }
        const QChar left = a.at(i).toLower();
        const QChar right = b.at(j).toLower();
        if (left != right)
            return left < right;
        ++i;
        ++j;
    }
    return a.size() < b.size();
}

} // namespace

QList<SerialPortChoice> arrangeSerialPorts(QList<SerialPortChoice> present, const QString &saved)
{
    std::sort(present.begin(), present.end(), [](const SerialPortChoice &a, const SerialPortChoice &b) {
        return portLess(a.port, b.port);
    });
    QList<SerialPortChoice> unique;
    for (const SerialPortChoice &choice : present) {
        bool duplicate = false;
        for (const SerialPortChoice &kept : unique) {
            if (kept.port.compare(choice.port, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            unique.append(choice);
    }
    const QString wanted = saved.trimmed();
    if (!wanted.isEmpty()) {
        bool found = false;
        for (const SerialPortChoice &choice : unique) {
            if (choice.port.compare(wanted, Qt::CaseInsensitive) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            unique.append(SerialPortChoice{wanted, wanted + QStringLiteral(" (missing)")});
    }
    return unique;
}

QList<FoundRotor> Rt21Finder::scanSerialPorts(int timeoutMs, const std::function<void(const FoundRotor &)> &onFound,
                                               const std::function<bool()> &cancelled, const QStringList &onlyPorts)
{
    struct Candidate {
        QString name;
        QString kind;
    };
    QList<Candidate> candidates;
    if (!onlyPorts.isEmpty()) {
        for (const QString &name : onlyPorts) {
            if (!name.trimmed().isEmpty())
                candidates.append({name, QStringLiteral("serial")});
        }
    } else {
        // Copy the names out and drop QSerialPortInfo before any probe. Holding
        // that list until the thread ends, while the live connection has the
        // winning COM port open, crashes in ntdll on Windows.
        auto ports = QSerialPortInfo::availablePorts();
        std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
            return portLess(a.portName(), b.portName());
        });
        for (const QSerialPortInfo &info : ports) {
            const QString chip = usbChipName(info.manufacturer(), info.description());
            const QString blob = (info.manufacturer() + info.description() + info.portName()).toLower();
            const bool usb = info.hasVendorIdentifier() || blob.contains(QStringLiteral("usb"))
                             || blob.contains(QStringLiteral("ttyusb")) || blob.contains(QStringLiteral("ttyacm"));
            QString kind = chip;
            if (kind.isEmpty() && usb)
                kind = QStringLiteral("USB");
            else if (kind.isEmpty())
                kind = QStringLiteral("serial");
            candidates.append({info.portName(), kind});
        }
    }
    QList<FoundRotor> found;
    for (const Candidate &candidate : candidates) {
        if (cancelled && cancelled())
            return found;
        for (int baud : serialProbeBauds()) {
            if (const auto hit = probeSerial(candidate.name, candidate.kind, baud, timeoutMs)) {
                found.append(*hit);
                if (onFound)
                    onFound(*hit);
                return found;
            }
        }
    }
    return found;
}

QStringList Rt21Finder::localSubnetHosts()
{
    QStringList hosts;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const QString adapter = iface.humanReadableName() + QLatin1Char(' ') + iface.name();
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning)
            || (iface.flags() & QNetworkInterface::IsLoopBack) || (iface.flags() & QNetworkInterface::IsPointToPoint)
            || isVirtualAdapterName(adapter))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const quint32 address = entry.ip().toIPv4Address();
            if ((address & 0xFFFF0000u) == 0xA9FE0000u)
                continue;
            const quint32 base = address & 0xFFFFFF00u;
            const quint32 self = address & 0xFFu;
            for (quint32 host = 1; host < 255 && hosts.size() < 512; ++host) {
                if (host == self)
                    continue;
                hosts.append(QHostAddress(base | host).toString());
            }
        }
    }
    return hosts;
}

QList<FoundRotor> Rt21Finder::scanHosts(const QStringList &hosts, const QList<int> &ports, int connectMs,
                                       const std::function<void(const FoundRotor &)> &onFound,
                                       const std::function<bool()> &cancelled)
{
    struct Target {
        QString host;
        int port = 0;
    };
    QList<Target> targets;
    for (const QString &host : hosts) {
        for (int port : ports) {
            if (port > 0 && port <= 65535)
                targets.append({host, port});
        }
    }
    QList<FoundRotor> found;
    constexpr int kParallel = 32;
    QElapsedTimer budget;
    budget.start();
    for (int start = 0; start < targets.size() && found.isEmpty() && budget.elapsed() < 14000; start += kParallel) {
        if (cancelled && cancelled())
            break;
        const int end = std::min(start + kParallel, int(targets.size()));
        QEventLoop loop;
        int pending = 0;
        bool stop = false;
        struct Probe {
            QTcpSocket *socket = nullptr;
            Rt21::Framer *framer = nullptr;
            QTimer *readTimer = nullptr;
            QTimer *connectTimer = nullptr;
        };
        QList<Probe> probes;
        QTimer cap;
        cap.setSingleShot(true);
        QObject::connect(&cap, &QTimer::timeout, &loop, &QEventLoop::quit);
        cap.start(connectMs + 900);
        QTimer watch;
        watch.setInterval(50);
        QObject::connect(&watch, &QTimer::timeout, &loop, [&]() {
            if (stop || (cancelled && cancelled()))
                loop.quit();
        });
        watch.start();
        for (int i = start; i < end; ++i) {
            Probe probe;
            probe.socket = new QTcpSocket;
            probe.framer = new Rt21::Framer;
            probe.readTimer = new QTimer(probe.socket);
            probe.readTimer->setSingleShot(true);
            probe.connectTimer = new QTimer(probe.socket);
            probe.connectTimer->setSingleShot(true);
            probes.append(probe);
            ++pending;
            QTcpSocket *socket = probe.socket;
            Rt21::Framer *framer = probe.framer;
            QTimer *readTimer = probe.readTimer;
            QTimer *connectTimer = probe.connectTimer;
            const QString host = targets.at(i).host;
            const int port = targets.at(i).port;
            auto note = [socket, host, port](const QByteArray &result) {
                if (socket->property("logged").toBool())
                    return;
                socket->setProperty("logged", true);
                const QByteArray line = host.toLatin1() + ":" + QByteArray::number(port) + " " + result;
                TrafficLog::instance().recordSession("FIND-NET", line);
                const QByteArray rx = socket->property("rx").toByteArray();
                if (!rx.isEmpty())
                    TrafficLog::instance().recordSession("FIND-NET", rx);
            };
            // Slots capture the loop by reference and are disconnected before
            // the sockets are deleted. deleteLater across thread shutdown was
            // invoking them after this stack frame was gone.
            auto settle = [&loop, &pending, &stop, socket, note](const QByteArray &result) {
                if (socket->property("done").toBool())
                    return;
                socket->setProperty("done", true);
                note(result);
                socket->abort();
                if (--pending <= 0 || stop)
                    loop.quit();
            };
            QObject::connect(socket, &QTcpSocket::connected, socket, [=]() {
                socket->setProperty("opened", true);
                socket->write(Rt21::queryTenths());
                socket->flush();
                readTimer->start(400);
            });
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [=, &found, &stop]() {
                if (stop || socket->property("done").toBool())
                    return;
                const QByteArray chunk = socket->readAll();
                socket->setProperty("rx", socket->property("rx").toByteArray() + chunk);
                framer->append(chunk);
                const bool wantVersion = socket->property("phase").toString() == QLatin1String("version");
                for (const QByteArray &frame : framer->take()) {
                    if (!wantVersion) {
                        if (const auto heading = Rt21::parseHeadingFrame(frame)) {
                            socket->setProperty("heading", *heading);
                            socket->setProperty("phase", QStringLiteral("version"));
                            socket->write(Rt21::queryVersion());
                            socket->flush();
                            readTimer->start(400);
                            return;
                        }
                        continue;
                    }
                    const QString text = Rt21::payloadAfterSoh(frame);
                    if (!text.contains(QStringLiteral("RT-21")))
                        continue;
                    FoundRotor hit;
                    hit.endpoint.transport = RotorEndpoint::Transport::Tcp;
                    hit.endpoint.host = host;
                    hit.endpoint.tcpPort = port;
                    hit.heading = socket->property("heading").toDouble();
                    hit.label = foundRotorLabel(QStringLiteral("%1:%2").arg(host).arg(port), QString(), 0, hit.heading)
                                + QStringLiteral(", ") + text;
                    found.append(hit);
                    stop = true;
                    if (onFound)
                        onFound(hit);
                    settle(QByteArray("hit ") + text.toLatin1());
                    return;
                }
            });
            QObject::connect(socket, &QTcpSocket::errorOccurred, socket, [=](QAbstractSocket::SocketError) {
                settle(socket->property("opened").toBool() ? QByteArray("closed") : QByteArray("connect failed"));
            });
            QObject::connect(readTimer, &QTimer::timeout, socket, [=]() {
                const QByteArray why = socket->property("phase").toString() == QLatin1String("version")
                                           ? QByteArray("no version")
                                           : QByteArray("no heading");
                settle(why);
            });
            QObject::connect(connectTimer, &QTimer::timeout, socket, [=]() {
                if (socket->state() != QAbstractSocket::ConnectedState)
                    settle(QByteArray("connect failed"));
            });
            connectTimer->start(connectMs);
            socket->connectToHost(host, quint16(port));
        }
        if (pending > 0)
            loop.exec();
        watch.stop();
        cap.stop();
        for (const Probe &probe : probes) {
            probe.socket->disconnect();
            probe.readTimer->stop();
            probe.connectTimer->stop();
            probe.socket->abort();
            delete probe.framer;
            delete probe.socket;
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (stop || (cancelled && cancelled()))
            break;
    }
    return found;
}
