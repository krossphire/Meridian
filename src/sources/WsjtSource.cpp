/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WsjtSource.h"

#include "WsjtStatus.h"

#include <QNetworkDatagram>
#include <QUdpSocket>

bool wsjtListenAllowed(int port, const QString &multicast)
{
    if (port <= 0 || port > 65535)
        return false;
    if (port == 2237 && !QHostAddress(multicast.trimmed()).isMulticast())
        return false;
    return true;
}

WsjtSource::WsjtSource(QObject *parent)
    : QObject(parent)
    , socket(new QUdpSocket(this))
{
    connect(socket, &QUdpSocket::readyRead, this, &WsjtSource::readDatagrams);
}

void WsjtSource::listen(int nextPort, const QString &multicast)
{
    if (socket->state() != QAbstractSocket::UnconnectedState)
        socket->close();
    port = 0;
    if (!wsjtListenAllowed(nextPort, multicast)) {
        if (nextPort == 2237)
            emit note(QStringLiteral("Port 2237 belongs to WSJT-X. Use QLog's forward port, or a multicast group."));
        return;
    }
    const QHostAddress group(multicast.trimmed());
    const bool shared = group.isMulticast();
    const QHostAddress bindAddress = shared ? QHostAddress::AnyIPv4 : QHostAddress::AnyIPv4;
    const auto mode = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    if (!socket->bind(bindAddress, quint16(nextPort), mode)) {
        emit note(QStringLiteral("Couldn't listen for WSJT-X."));
        return;
    }
    if (shared && !socket->joinMulticastGroup(group)) {
        socket->close();
        emit note(QStringLiteral("Couldn't join the WSJT-X multicast group."));
        return;
    }
    port = nextPort;
}

void WsjtSource::readDatagrams()
{
    while (socket->hasPendingDatagrams()) {
        const QByteArray datagram = socket->receiveDatagram().data();
        const WsjtStatus status = parseWsjtStatus(datagram);
        if (!status.isStatus)
            continue;
        emit dxStation(status.dxCall, status.dxGrid);
    }
}
