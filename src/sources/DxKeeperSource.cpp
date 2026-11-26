/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DxKeeperSource.h"

#include <QThread>
#include <QTimer>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ddeml.h>
#endif

DxKeeperReading interpretDxKeeper(const QString &previousCall, const QString &previousGrid, const QString &callIn,
                             const QString &gridIn)
{
    DxKeeperReading out;
    out.call = callIn.trimmed().toUpper();
    out.grid = gridIn.trimmed().toUpper();
    if (out.call.isEmpty()) {
        if (!previousCall.trimmed().isEmpty())
            out.action = DxKeeperAction::Clear;
        return out;
    }
    if (out.call != previousCall.trimmed().toUpper() || out.grid != previousGrid.trimmed().toUpper())
        out.action = DxKeeperAction::Offer;
    return out;
}

DxKeeperSource::DxKeeperSource(QObject *parent)
    : QObject(parent)
    , timer(new QTimer)
{
    timer->setInterval(700);
    // DDE calls block. The timer lives on a worker so a slow server cannot
    // freeze the window. Linux has no DDE; the same timer just returns.
    worker = new QThread(this);
    timer->moveToThread(worker);
    connect(timer, &QTimer::timeout, timer, [this]() { poll(); });
    connect(worker, &QThread::started, timer, [this]() { timer->start(); });
    connect(worker, &QThread::finished, timer, &QObject::deleteLater);
    worker->start();
}

DxKeeperSource::~DxKeeperSource()
{
    enabled = false;
    if (worker && worker->isRunning() && timer) {
        QMetaObject::invokeMethod(timer, [this]() { disconnectServer(); timer->stop(); },
                                  Qt::BlockingQueuedConnection);
    }
    if (worker) {
        worker->quit();
        worker->wait(2000);
    }
}

void DxKeeperSource::setEnabled(bool on)
{
    enabled = on;
    if (!on) {
        if (worker && worker->isRunning())
            QMetaObject::invokeMethod(timer, [this]() { disconnectServer(); }, Qt::BlockingQueuedConnection);
        else
            disconnectServer();
        if (!lastCall.isEmpty()) {
            lastCall.clear();
            lastGrid.clear();
            emit cleared();
        }
    }
}

void DxKeeperSource::disconnectServer()
{
#if defined(Q_OS_WIN)
    if (conversation && instance) {
        DdeDisconnect(static_cast<HCONV>(conversation));
        conversation = nullptr;
    }
    if (instance) {
        DdeUninitialize(instance);
        instance = 0;
    }
#endif
    connected = false;
}

QString DxKeeperSource::requestItem(const wchar_t *name)
{
#if defined(Q_OS_WIN)
    if (!conversation || !instance || !name)
        return {};
    HSZ item = DdeCreateStringHandleW(instance, name, CP_WINUNICODE);
    if (!item)
        return {};
    HDDEDATA data = DdeClientTransaction(nullptr, 0, static_cast<HCONV>(conversation), item, CF_TEXT, XTYP_REQUEST, 800,
                                         nullptr);
    DdeFreeStringHandle(instance, item);
    if (!data) {
        const UINT err = DdeGetLastError(instance);
        if (err == DMLERR_NO_CONV || err == DMLERR_SERVER_DIED || err == DMLERR_NOTPROCESSED
            || err == DMLERR_ADVACKTIMEOUT)
            itemFailed = true;
        return {};
    }
    DWORD size = 0;
    // MinGW declares DdeAccessData as returning LPBYTE. That is unsigned char*,
    // which does not static_cast to char*. Stop at the first NUL and never
    // read past the handle. Qt 6.4, which the Linux CI uses, has no qstrnlen.
    const auto *bytes = DdeAccessData(data, &size);
    QString value;
    if (bytes && size > 0) {
        int length = 0;
        const int cap = int(size);
        while (length < cap && bytes[length] != 0)
            ++length;
        value = QString::fromLocal8Bit(reinterpret_cast<const char *>(bytes), length).trimmed();
    }
    DdeUnaccessData(data);
    DdeFreeDataHandle(data);
    return value;
#else
    Q_UNUSED(name);
    return {};
#endif
}

void DxKeeperSource::poll()
{
    if (!enabled)
        return;
#if !defined(Q_OS_WIN)
    return;
#else
    if (!connected) {
        if (DdeInitializeW(&instance, nullptr, APPCMD_CLIENTONLY, 0) != DMLERR_NO_ERROR) {
            instance = 0;
            return;
        }
        HSZ service = DdeCreateStringHandleW(instance, L"DXKeeper", CP_WINUNICODE);
        HSZ topic = DdeCreateStringHandleW(instance, L"DDEServer", CP_WINUNICODE);
        conversation = DdeConnect(instance, service, topic, nullptr);
        DdeFreeStringHandle(instance, service);
        DdeFreeStringHandle(instance, topic);
        if (!conversation) {
            DdeUninitialize(instance);
            instance = 0;
            return;
        }
        connected = true;
    }
    itemFailed = false;
    const QString call = requestItem(L"LookupCallsign");
    if (itemFailed) {
        disconnectServer();
        return;
    }
    const QString grid = requestItem(L"CallbookGrid");
    if (itemFailed) {
        disconnectServer();
        return;
    }
    const DxKeeperReading reading = interpretDxKeeper(lastCall, lastGrid, call, grid);
    if (reading.action == DxKeeperAction::Clear) {
        lastCall.clear();
        lastGrid.clear();
        emit cleared();
    } else if (reading.action == DxKeeperAction::Offer) {
        lastCall = reading.call;
        lastGrid = reading.grid;
        emit station(reading.call, reading.grid);
    }
#endif
}
