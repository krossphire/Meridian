/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The socket is bound with SO_REUSEADDR, and SO_REUSEPORT where the
 * platform has it, so N1MM and another listener can share the port.
 */

#include "N1mmSource.h"

#include "N1mmXml.h"

#include <QNetworkDatagram>
#include <QUdpSocket>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

bool shareBound(QUdpSocket *socket, int port)
{
#if defined(Q_OS_WIN)
    const auto mode = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    return socket->bind(QHostAddress::AnyIPv4, quint16(port), mode);
#else
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(uint16_t(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return false;
    }
    if (!socket->setSocketDescriptor(fd)) {
        ::close(fd);
        return false;
    }
    return true;
#endif
}

} // namespace

N1mmSource::N1mmSource(QObject *parent)
    : QObject(parent)
    , socket(new QUdpSocket(this))
{
    connect(socket, &QUdpSocket::readyRead, this, &N1mmSource::readDatagrams);
}

void N1mmSource::listen(int port)
{
    if (socket->state() != QAbstractSocket::UnconnectedState)
        socket->close();
    if (port <= 0 || port > 65535)
        return;
    if (!shareBound(socket, port))
        emit note(QStringLiteral("Couldn't listen for N1MM."));
}

void N1mmSource::readDatagrams()
{
    while (socket->hasPendingDatagrams()) {
        const N1mmPacket packet = parseN1mmPacket(socket->receiveDatagram().data());
        if (packet.kind == N1mmPacket::Kind::Contact)
            emit contactLogged();
        else if (packet.kind == N1mmPacket::Kind::Lookup)
            emit lookup(packet.call, packet.grid);
    }
}
