/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DxKeeperSource.h"

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
    , timer(new QTimer(this))
{
    timer->setInterval(700);
    connect(timer, &QTimer::timeout, this, &DxKeeperSource::poll);
}

DxKeeperSource::~DxKeeperSource()
{
    disconnectServer();
}

void DxKeeperSource::setEnabled(bool on)
{
    enabled = on;
    if (!on) {
        timer->stop();
        disconnectServer();
        if (!lastCall.isEmpty()) {
            lastCall.clear();
            lastGrid.clear();
            emit cleared();
        }
        return;
    }
    timer->start();
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
    if (!data)
        return {};
    DWORD size = 0;
    const char *text = static_cast<const char *>(DdeAccessData(data, &size));
    QString value;
    if (text)
        value = QString::fromLocal8Bit(text, int(size ? size - 1 : 0)).trimmed();
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
    const QString call = requestItem(L"LookupCallsign");
    const QString grid = requestItem(L"CallbookGrid");
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
