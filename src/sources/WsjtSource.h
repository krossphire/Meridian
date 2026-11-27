/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

class QUdpSocket;

// 2237 unicast belongs to WSJT-X. Empty port is off. Multicast may share 2237.
bool wsjtListenAllowed(int port, const QString &multicast);

class WsjtSource : public QObject
{
    Q_OBJECT
public:
    explicit WsjtSource(QObject *parent = nullptr);
    void listen(int port, const QString &multicast);

signals:
    void dxStation(const QString &call, const QString &grid);
    void note(const QString &text);

private:
    void readDatagrams();

    QUdpSocket *socket = nullptr;
    int port = 0;
};
