/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DXKeeper, not DXView. DXLabTest (DDEClientModule.bas, 2019-01-22) links
 * these items on topic DXKeeper|DDEServer:
 *   LookupCallsign, CallbookGrid
 * It does not link a latitude or longitude item for DXKeeper. A live
 * DXKeeper was not available here, so the values those items return while
 * the Capture window changes are unverified.
 */

#pragma once

#include <QObject>
#include <QString>

class QThread;
class QTimer;

enum class DxKeeperAction { None, Offer, Clear };

struct DxKeeperReading {
    DxKeeperAction action = DxKeeperAction::None;
    QString call;
    QString grid;
};

DxKeeperReading interpretDxKeeper(const QString &previousCall, const QString &previousGrid, const QString &call,
                             const QString &grid);

class DxKeeperSource : public QObject
{
    Q_OBJECT
public:
    explicit DxKeeperSource(QObject *parent = nullptr);
    ~DxKeeperSource() override;
    void setEnabled(bool on);

signals:
    void station(const QString &call, const QString &grid);
    void cleared();

private:
    void poll();
    QString requestItem(const wchar_t *name);
    void disconnectServer();

    QTimer *timer = nullptr;
    QThread *worker = nullptr;
    QString lastCall;
    QString lastGrid;
    bool enabled = false;
    bool connected = false;
    bool itemFailed = false;
#if defined(Q_OS_WIN)
    unsigned long instance = 0;
    void *conversation = nullptr;
#endif
};
