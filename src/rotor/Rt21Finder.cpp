/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Finder.h"

#include "Rt21Protocol.h"

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

std::optional<double> readHeading(QIODevice *device, int timeoutMs)
{
    Rt21::Framer framer;
    std::optional<double> heading;
    QEventLoop loop;
    QTimer cap;
    cap.setSingleShot(true);
    QObject::connect(&cap, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(device, &QIODevice::readyRead, &loop, [&]() {
        framer.append(device->readAll());
        for (const QByteArray &frame : framer.take()) {
            if (const auto parsed = Rt21::parseHeadingFrame(frame)) {
                heading = parsed;
                loop.quit();
                return;
            }
        }
    });
    if (device->bytesAvailable() > 0)
        framer.append(device->readAll());
    for (const QByteArray &frame : framer.take()) {
        if (const auto parsed = Rt21::parseHeadingFrame(frame))
            return parsed;
    }
    cap.start(timeoutMs);
    loop.exec();
    return heading;
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
    // In use, missing, or a virtual port another program owns: skip it quietly.
    if (!port.open(QIODevice::ReadWrite))
        return std::nullopt;
    port.write(Rt21::queryTenths());
    port.flush();
    const auto heading = readHeading(&port, timeoutMs);
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
    if (!socket.waitForConnected(timeoutMs)) {
        socket.abort();
        return std::nullopt;
    }
    socket.write(Rt21::queryTenths());
    socket.flush();
    const auto heading = readHeading(&socket, timeoutMs);
    socket.disconnectFromHost();
    socket.close();
    if (!heading)
        return std::nullopt;
    FoundRotor found;
    found.endpoint.transport = RotorEndpoint::Transport::Tcp;
    found.endpoint.host = host;
    found.endpoint.tcpPort = port;
    found.heading = *heading;
    found.label = foundRotorLabel(QStringLiteral("%1:%2").arg(host).arg(port), QString(), 0, *heading);
    return found;
}

QList<int> serialProbeBauds()
{
    return {4800, 9600, 19200};
}

QList<FoundRotor> Rt21Finder::scanSerialPorts(int timeoutMs, const std::function<void(const FoundRotor &)> &onFound)
{
    QList<FoundRotor> found;
    // Edgeport/8 shows up as ordinary COM1–COM8. Every present port is probed.
    // FTDI and CP210x are not treated differently once a COM port exists.
    auto ports = QSerialPortInfo::availablePorts();
    std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
        auto number = [](QString name) {
            name.remove(QRegularExpression(QStringLiteral("\\D")));
            bool ok = false;
            const int value = name.toInt(&ok);
            return ok ? value : 100000;
        };
        const int left = number(a.portName());
        const int right = number(b.portName());
        if (left != right)
            return left < right;
        return a.portName() < b.portName();
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
            || (iface.flags() & QNetworkInterface::IsLoopBack) || isVirtualAdapterName(adapter))
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const quint32 address = entry.ip().toIPv4Address();
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
                                       const std::function<void(const FoundRotor &)> &onFound)
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
        const int end = std::min(start + kParallel, int(targets.size()));
        QEventLoop loop;
        int pending = 0;
        QList<QTcpSocket *> sockets;
        QTimer cap;
        cap.setSingleShot(true);
        QObject::connect(&cap, &QTimer::timeout, &loop, &QEventLoop::quit);
        cap.start(connectMs + 350);
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
            auto settle = [&, socket]() {
                if (socket->property("done").toBool())
                    return;
                socket->setProperty("done", true);
                socket->abort();
                if (--pending <= 0)
                    loop.quit();
            };
            QObject::connect(socket, &QTcpSocket::connected, socket, [=]() {
                socket->write(Rt21::queryTenths());
                socket->flush();
                readTimer->start(250);
            });
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [=, &found]() {
                framer->append(socket->readAll());
                for (const QByteArray &frame : framer->take()) {
                    if (const auto heading = Rt21::parseHeadingFrame(frame)) {
                        FoundRotor hit;
                        hit.endpoint.transport = RotorEndpoint::Transport::Tcp;
                        hit.endpoint.host = host;
                        hit.endpoint.tcpPort = port;
                        hit.heading = *heading;
                        hit.label = foundRotorLabel(QStringLiteral("%1:%2").arg(host).arg(port), QString(), 0, *heading);
                        found.append(hit);
                        if (onFound)
                            onFound(hit);
                        settle();
                        return;
                    }
                }
            });
            QObject::connect(socket, &QTcpSocket::errorOccurred, socket, [=](QAbstractSocket::SocketError) { settle(); });
            QObject::connect(readTimer, &QTimer::timeout, socket, [=]() { settle(); });
            QTimer::singleShot(connectMs, socket, [=]() {
                if (socket->state() != QAbstractSocket::ConnectedState)
                    settle();
            });
            socket->connectToHost(host, quint16(port));
        }
        if (pending > 0)
            loop.exec();
        for (QTcpSocket *socket : sockets) {
            socket->abort();
            socket->deleteLater();
        }
    }
    return found;
}
