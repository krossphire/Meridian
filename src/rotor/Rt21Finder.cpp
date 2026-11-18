/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Finder.h"

#include "Rt21Protocol.h"
#include "SerialLines.h"
#include "TrafficLog.h"

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
    QList<int> ports = {23, 4001, 5000, 6555, 8080, 10001};
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
    return {4800, 9600, 19200};
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
                                               const std::function<bool()> &cancelled)
{
    QList<FoundRotor> found;
    // Edgeport/8 shows up as ordinary COM1–COM8. Every present port is probed.
    // FTDI and CP210x are not treated differently once a COM port exists.
    auto ports = QSerialPortInfo::availablePorts();
    std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
        return portLess(a.portName(), b.portName());
    });
    for (const QSerialPortInfo &info : ports) {
        if (cancelled && cancelled())
            return found;
        const QString chip = usbChipName(info.manufacturer(), info.description());
        const QString blob = (info.manufacturer() + info.description() + info.portName()).toLower();
        const bool usb = info.hasVendorIdentifier() || blob.contains(QStringLiteral("usb"))
                         || blob.contains(QStringLiteral("ttyusb")) || blob.contains(QStringLiteral("ttyacm"));
        QString kind = chip;
        if (kind.isEmpty() && usb)
            kind = QStringLiteral("USB");
        else if (kind.isEmpty())
            kind = QStringLiteral("serial");
        for (int baud : serialProbeBauds()) {
            if (const auto hit = probeSerial(info.portName(), kind, baud, timeoutMs)) {
                found.append(*hit);
                if (onFound)
                    onFound(*hit);
                break;
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
    for (int start = 0; start < targets.size() && budget.elapsed() < 14000; start += kParallel) {
        if (cancelled && cancelled())
            break;
        const int end = std::min(start + kParallel, int(targets.size()));
        QEventLoop loop;
        int pending = 0;
        QList<QTcpSocket *> sockets;
        QTimer cap;
        cap.setSingleShot(true);
        QObject::connect(&cap, &QTimer::timeout, &loop, &QEventLoop::quit);
        cap.start(connectMs + 900);
        QTimer watch;
        watch.setInterval(50);
        QObject::connect(&watch, &QTimer::timeout, &loop, [&]() {
            if (cancelled && cancelled())
                loop.quit();
        });
        watch.start();
        for (int i = start; i < end; ++i) {
            auto *socket = new QTcpSocket;
            sockets.append(socket);
            ++pending;
            auto *framer = new Rt21::Framer;
            QObject::connect(socket, &QObject::destroyed, [framer]() { delete framer; });
            auto *readTimer = new QTimer(socket);
            readTimer->setSingleShot(true);
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
            // Copy `note` into settle. A reference would dangle: the slot runs
            // from loop.exec() after this iteration's locals are gone.
            auto settle = [&pending, &loop, socket, note](const QByteArray &result) {
                if (socket->property("done").toBool())
                    return;
                socket->setProperty("done", true);
                note(result);
                socket->abort();
                if (--pending <= 0)
                    loop.quit();
            };
            QObject::connect(socket, &QTcpSocket::connected, socket, [=]() {
                socket->setProperty("opened", true);
                socket->write(Rt21::queryTenths());
                socket->flush();
                readTimer->start(400);
            });
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [=, &found]() {
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
            QTimer::singleShot(connectMs, socket, [=]() {
                if (socket->state() != QAbstractSocket::ConnectedState)
                    settle(QByteArray("connect failed"));
            });
            socket->connectToHost(host, quint16(port));
        }
        if (pending > 0)
            loop.exec();
        for (QTcpSocket *socket : sockets) {
            socket->abort();
            socket->deleteLater();
        }
        if (cancelled && cancelled())
            break;
    }
    return found;
}
