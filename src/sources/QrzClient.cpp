/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QrzClient.h"

#include "CredentialStore.h"
#include "QrzXml.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

namespace {

const QString kEndpoint = QStringLiteral("https://xmldata.qrz.com/xml/current/");

QString safeError(const QString &error, const QString &password)
{
    if (!password.isEmpty() && error.contains(password))
        return QStringLiteral("QRZ login failed.");
    return error;
}

} // namespace

QrzClient::QrzClient(QObject *parent)
    : QObject(parent)
    , net(new QNetworkAccessManager(this))
{
}

void QrzClient::setUsername(const QString &name)
{
    const QString next = name.trimmed();
    if (next.compare(username, Qt::CaseInsensitive) != 0)
        sessionKey.clear();
    username = next;
}

bool QrzClient::configured() const
{
    return !username.isEmpty() && !CredentialStore::load(username).isEmpty();
}

void QrzClient::lookup(const QString &call)
{
    const QString wanted = call.trimmed().toUpper();
    if (wanted.isEmpty())
        return;
    pendingCall = wanted;
    if (username.isEmpty() || CredentialStore::load(username).isEmpty())
        return;
    if (sessionKey.isEmpty())
        login();
    else
        fetch(pendingCall);
}

void QrzClient::login()
{
    if (loggingIn)
        return;
    QString password = CredentialStore::load(username);
    if (username.isEmpty() || password.isEmpty())
        return;
    loggingIn = true;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("username"), username);
    query.addQueryItem(QStringLiteral("password"), password);
    password.fill(QLatin1Char('x'));
    password.clear();
    QUrl url(kEndpoint);
    url.setQuery(query);
    QNetworkReply *reply = net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finish(reply); });
}

void QrzClient::fetch(const QString &call)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("s"), sessionKey);
    query.addQueryItem(QStringLiteral("callsign"), call);
    QUrl url(kEndpoint);
    url.setQuery(query);
    QNetworkReply *reply = net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finish(reply); });
}

void QrzClient::finish(QNetworkReply *reply)
{
    const bool wasLogin = loggingIn;
    loggingIn = false;
    const QByteArray body = reply->readAll();
    const bool transportFailed = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();
    const QrzXml parsed = parseQrzXml(body);
    if (transportFailed || !parsed.error.isEmpty() || (wasLogin && parsed.sessionKey.isEmpty())) {
        const QString reason = parsed.error.isEmpty() ? QStringLiteral("QRZ didn't answer.") : parsed.error;
        if (reason.contains(QStringLiteral("Session"), Qt::CaseInsensitive) && !wasLogin) {
            sessionKey.clear();
            login();
            return;
        }
        sessionKey.clear();
        emit note(safeError(reason, CredentialStore::load(username)));
        return;
    }
    if (wasLogin) {
        sessionKey = parsed.sessionKey;
        if (!pendingCall.isEmpty())
            fetch(pendingCall);
        return;
    }
    const bool havePoint = parsed.haveLatitude && parsed.haveLongitude;
    emit located(parsed.call.isEmpty() ? pendingCall : parsed.call, parsed.grid, parsed.latitude, parsed.longitude,
                 havePoint);
}
