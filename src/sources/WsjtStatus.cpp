/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WsjtStatus.h"

#include <QDataStream>

WsjtStatus parseWsjtStatus(const QByteArray &datagram)
{
    WsjtStatus out;
    QDataStream stream(datagram);
    stream.setByteOrder(QDataStream::BigEndian);
    quint32 magic = 0;
    quint32 schema = 0;
    quint32 type = 0;
    stream >> magic >> schema >> type;
    if (stream.status() != QDataStream::Ok || magic != 0xadbccbda || type != 1)
        return out;
    QByteArray id;
    QByteArray mode;
    QByteArray dxCall;
    QByteArray report;
    QByteArray txMode;
    QByteArray deCall;
    QByteArray deGrid;
    QByteArray dxGrid;
    quint64 dial = 0;
    bool txEnabled = false;
    bool transmitting = false;
    bool decoding = false;
    quint32 rxDf = 0;
    quint32 txDf = 0;
    stream >> id >> dial >> mode >> dxCall >> report >> txMode;
    stream >> txEnabled >> transmitting >> decoding;
    stream >> rxDf >> txDf >> deCall >> deGrid >> dxGrid;
    if (stream.status() != QDataStream::Ok)
        return out;
    Q_UNUSED(schema);
    out.isStatus = true;
    out.dxCall = QString::fromUtf8(dxCall).trimmed().toUpper();
    out.dxGrid = QString::fromUtf8(dxGrid).trimmed().toUpper();
    return out;
}
