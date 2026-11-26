/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * QRZ XML session. The session key stays in memory. The password is read
 * from the keychain for the login request and is not logged.
 */

#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class QrzClient : public QObject
{
    Q_OBJECT
public:
    explicit QrzClient(QObject *parent = nullptr);

    void setUsername(const QString &username);
    bool configured() const;
    void lookup(const QString &call);

    // The next lookup fails immediately and does not open a socket.
    static void failNextLookupForTests(bool on);
    // The next lookup reports this fix and does not open a socket.
    // While held, the result waits for releaseScriptedLookupForTests().
    static void scriptNextLookupForTests(double latitude, double longitude, const QString &grid, bool havePoint);
    static void holdScriptedLookupForTests(bool on);
    static void releaseScriptedLookupForTests();

signals:
    void located(const QString &call, const QString &grid, double latitude, double longitude, bool havePoint);
    void note(const QString &text);
    void lookupFinished(bool ok);

private:
    void login();
    void fetch(const QString &call);
    void begin(const QUrl &url);
    void dropInflight();
    void fail(const QString &text);
    void finish(QNetworkReply *reply);

    QNetworkAccessManager *net = nullptr;
    class QTimer *limit = nullptr;
    QNetworkReply *inflight = nullptr;
    QString username;
    QString sessionKey;
    QString pendingCall;
    bool loggingIn = false;
    bool sessionRetried = false;
    bool scripted = false;
    bool scriptPoint = false;
    double scriptLat = 0;
    double scriptLon = 0;
    QString scriptGrid;
    void deliverScripted();
};
