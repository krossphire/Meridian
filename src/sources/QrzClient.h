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

signals:
    void located(const QString &call, const QString &grid, double latitude, double longitude, bool havePoint);
    void note(const QString &text);

private:
    void login();
    void fetch(const QString &call);
    void finish(QNetworkReply *reply);

    QNetworkAccessManager *net = nullptr;
    QString username;
    QString sessionKey;
    QString pendingCall;
    bool loggingIn = false;
};
