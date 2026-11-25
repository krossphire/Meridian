/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QlogSource.h"

#include "QlogNotice.h"

#include <QNetworkDatagram>
#include <QUdpSocket>

QlogSource::QlogSource(QObject *parent)
    : QObject(parent)
    , socket(new QUdpSocket(this))
{
    connect(socket, &QUdpSocket::readyRead, this, &QlogSource::readDatagrams);
}

void QlogSource::listen(int port)
{
    if (socket->state() != QAbstractSocket::UnconnectedState)
        socket->close();
    if (port <= 0 || port > 65535)
        return;
    socket->bind(QHostAddress::AnyIPv4, quint16(port), QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
}

void QlogSource::readDatagrams()
{
    while (socket->hasPendingDatagrams()) {
        const QlogContact contact = parseQlogNotice(socket->receiveDatagram().data());
        if (!contact.isCurrentContact)
            continue;
        emit currentContact(contact.call, contact.grid, contact.azimuth, contact.haveAzimuth);
    }
}
