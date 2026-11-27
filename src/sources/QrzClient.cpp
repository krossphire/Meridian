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
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace {

const QString kEndpoint = QStringLiteral("https://xmldata.qrz.com/xml/current/");
constexpr int kLookupTimeoutMs = 8000;
bool failNextLookup = false;
bool holdScripted = false;
QrzClient *scriptedOwner = nullptr;

struct ScriptedFix {
    bool armed = false;
    double lat = 0;
    double lon = 0;
    QString grid;
    bool havePoint = false;
};
ScriptedFix pendingScript;

} // namespace

QrzClient::QrzClient(QObject *parent)
    : QObject(parent)
    , net(new QNetworkAccessManager(this))
{
}

void QrzClient::setUsername(const QString &name)
{
    const QString next = name.trimmed();
    if (next.compare(username, Qt::CaseInsensitive) != 0) {
        sessionKey.clear();
        noPassword = false;
    }
    username = next;
}

bool QrzClient::configured() const
{
    return !username.isEmpty() && !noPassword;
}

void QrzClient::failNextLookupForTests(bool on)
{
    failNextLookup = on;
}

void QrzClient::scriptNextLookupForTests(double latitude, double longitude, const QString &grid, bool havePoint)
{
    pendingScript = ScriptedFix{true, latitude, longitude, grid, havePoint};
}

void QrzClient::holdScriptedLookupForTests(bool on)
{
    holdScripted = on;
}

void QrzClient::releaseScriptedLookupForTests()
{
    QrzClient *client = scriptedOwner;
    scriptedOwner = nullptr;
    if (client)
        client->deliverScripted();
}

void QrzClient::lookup(const QString &call)
{
    const QString wanted = call.trimmed().toUpper();
    if (wanted.isEmpty()) {
        emit lookupFinished(false);
        return;
    }
    pendingCall = wanted;
    sessionRetried = false;
    if (pendingScript.armed) {
        scriptLat = pendingScript.lat;
        scriptLon = pendingScript.lon;
        scriptGrid = pendingScript.grid;
        scriptPoint = pendingScript.havePoint;
        scripted = true;
        pendingScript.armed = false;
        scriptedOwner = this;
        if (!holdScripted)
            deliverScripted();
        return;
    }
    if (failNextLookup) {
        failNextLookup = false;
        fail(QStringLiteral("QRZ lookup failed"));
        return;
    }
    if (username.isEmpty()) {
        fail(QStringLiteral("QRZ login needed"));
        return;
    }
    // The keychain is read in login(), once, when a session is needed.
    if (sessionKey.isEmpty())
        login();
    else
        fetch(pendingCall);
}

void QrzClient::deliverScripted()
{
    if (!scripted)
        return;
    scripted = false;
    scriptedOwner = nullptr;
    emit located(pendingCall, scriptGrid, scriptLat, scriptLon, scriptPoint);
    emit lookupFinished(true);
}

void QrzClient::login()
{
    if (loggingIn)
        return;
    QString password = CredentialStore::load(username);
    if (username.isEmpty() || password.isEmpty()) {
        noPassword = password.isEmpty();
        fail(QStringLiteral("QRZ login needed"));
        return;
    }
    noPassword = false;
    loggingIn = true;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("username"), username);
    query.addQueryItem(QStringLiteral("password"), password);
    password.fill(QLatin1Char('x'));
    password.clear();
    QUrl url(kEndpoint);
    url.setQuery(query);
    begin(url);
}

void QrzClient::fetch(const QString &call)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("s"), sessionKey);
    query.addQueryItem(QStringLiteral("callsign"), call);
    QUrl url(kEndpoint);
    url.setQuery(query);
    begin(url);
}

void QrzClient::begin(const QUrl &url)
{
    dropInflight();
    inflight = net->get(QNetworkRequest(url));
    if (!limit) {
        limit = new QTimer(this);
        limit->setSingleShot(true);
        connect(limit, &QTimer::timeout, this, [this]() {
            dropInflight();
            loggingIn = false;
            emit note(QStringLiteral("QRZ lookup failed"));
            emit lookupFinished(false);
        });
    }
    limit->start(kLookupTimeoutMs);
    connect(inflight, &QNetworkReply::finished, this, [this]() {
        if (inflight)
            finish(inflight);
    });
}

void QrzClient::dropInflight()
{
    if (limit)
        limit->stop();
    if (!inflight)
        return;
    QNetworkReply *reply = inflight;
    inflight = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void QrzClient::fail(const QString &text)
{
    loggingIn = false;
    dropInflight();
    emit note(text);
    emit lookupFinished(false);
}

void QrzClient::finish(QNetworkReply *reply)
{
    if (reply != inflight)
        return;
    const bool wasLogin = loggingIn;
    loggingIn = false;
    inflight = nullptr;
    if (limit)
        limit->stop();
    const QByteArray body = reply->readAll();
    const bool transportFailed = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();
    const QrzXml parsed = parseQrzXml(body);
    if (transportFailed || !parsed.error.isEmpty() || (wasLogin && parsed.sessionKey.isEmpty())) {
        if (parsed.error.contains(QStringLiteral("Session"), Qt::CaseInsensitive) && !wasLogin && !sessionRetried) {
            sessionKey.clear();
            sessionRetried = true;
            login();
            return;
        }
        sessionKey.clear();
        emit note(QStringLiteral("QRZ lookup failed"));
        emit lookupFinished(false);
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
    emit lookupFinished(true);
}
