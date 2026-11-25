/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

class QUdpSocket;

class N1mmSource : public QObject
{
    Q_OBJECT
public:
    explicit N1mmSource(QObject *parent = nullptr);
    void listen(int port);

signals:
    void lookup(const QString &call, const QString &grid);
    void contactLogged();
    void note(const QString &text);

private:
    void readDatagrams();

    QUdpSocket *socket = nullptr;
};
