/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

class QUdpSocket;

class QlogSource : public QObject
{
    Q_OBJECT
public:
    explicit QlogSource(QObject *parent = nullptr);
    void listen(int port);

signals:
    void currentContact(const QString &call, const QString &grid, double azimuth, bool haveAzimuth);

private:
    void readDatagrams();

    QUdpSocket *socket = nullptr;
};
