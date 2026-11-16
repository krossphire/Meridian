/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Non-blocking RT-21 driver. Sockets and the poll timer live on the GUI thread.
 */

#pragma once

#include "AppConfig.h"
#include "Rt21Protocol.h"
#include "Rt21Setup.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QQueue>

#include <optional>

class QIODevice;
class QTcpSocket;
class QSerialPort;
class QTimer;

class Rt21Driver : public QObject
{
    Q_OBJECT
public:
    enum class Link { Off, Connecting, On, Moving, Failed };

    explicit Rt21Driver(QObject *parent = nullptr);
    ~Rt21Driver() override;

    void connectEndpoint(const RotorEndpoint &endpoint);
    void disconnectLink();

    void goTo(double azimuth);
    void stopMotion();

    void readConfiguration();
    void readNetwork();
    void writeConfiguration(const QList<QPair<QChar, int>> &changes);
    void writeBrightness(int value);
    void applySetupGroup(const QString &group, const QMap<QString, QString> &fields, const QString &backupPath);
    void sendCalibrate(bool clockwise);
    void sendPulseHeading(double azimuth);
    void postNote(const QString &note);

    Link link() const { return link_; }
    RotorEndpoint linkEndpoint() const { return endpoint; }
    std::optional<double> heading() const { return heading_; }
    std::optional<double> target() const { return target_; }
    QString detail() const { return detail_; }
    QString transientNote() const { return note_; }
    qint64 lastSeenMs() const { return lastReplyMs; }
    int currentPollMs() const;

    void setIdlePollSeconds(int seconds);

    // True once this driver has been bound to this exact transport.
    // A name-only edit does not count as a different connection.
    bool serves(const RotorEndpoint &endpoint) const;

    // Stop timers, drop signals, shut the socket down, and refuse later use.
    // The caller deleteLater's the driver after this returns.
    void retire();

    static int instanceCount();
    static int transportHighWater();
    static int transportsOpen();
    static void resetDiagnostics();

signals:
    void linkChanged(Rt21Driver::Link state, const QString &detail);
    void headingChanged(double azimuth);
    void targetChanged(double azimuth, bool active);
    void configField(QChar id, const QString &raw, bool numeric, int value);
    void configNote(const QString &text);
    void configFinished(bool ok, const QString &message);
    void networkField(const QString &key, const QString &value);
    void networkFinished(bool ok, const QString &message);
    void writeFinished(bool ok, const QString &message);
    void statusNote(const QString &text);

private:
    enum class Kind { Motion, Heading, Status, Read, Write, Note, Module, Probe };

    struct Job {
        Kind kind = Kind::Motion;
        QByteArray bytes;
        QChar id;
        QString note;
        QString moduleKey;
        QString expectToken;
        QString expectValue;
        bool moduleOptional = false;
        bool moduleExit = false;
        bool moduleRetried = false;
        bool brightness = false;
    };

    void setLink(Link state, const QString &detail);
    void closeIo(const QString &reason);
    void tearDownSocket();
    void onReadyRead();
    void onConnected();
    void onSocketError();
    void onConnectTimeout();
    void scheduleReconnect();
    void beginAttempt();
    void endAttempt(const QString &why);
    void pump();
    void finishInflight();
    void handleFrame(const QByteArray &frame);
    void failLink(const QString &message, bool linkDown = false);
    void startPoll();
    void pollTick();
    void enqueue(Job job);
    void noteArrival(double azimuth, bool haveStatus, int status);
    void sendQuery();
    void sendArmedGoto();
    void dropCommand(const QString &note);
    void showNote(const QString &note);
    void finishNetwork(bool ok, const QString &message);
    void enqueueModule(const Rt21::ModuleQuery &query);
    void startNetworkPass();
    void beginGuardedRead(bool network);
    void enqueueConfiguration();
    void enqueueNetworkQueries();
    void sendNetworkExit();
    void beginBrightnessWatch(bool sawRead, bool matched, const QString &readText);
    void finishBrightnessWatch(bool portClosed);
    void scheduleBrightnessWrite();
    void sendBrightnessRead();
    void holdPollsForNetworkExit();
    void resumePollsAfterNetwork();
    QString noReplyMessage() const;
    bool moduleLineMatches(const QByteArray &frame) const;
    void startFastPoll();
    void startIdlePoll();
    void enableKeepAlive();
    bool transportOpen() const;

    RotorEndpoint endpoint;
    bool wantLink = false;
    Link link_ = Link::Off;
    QString detail_;
    QTcpSocket *tcp = nullptr;
    QSerialPort *serial = nullptr;
    QIODevice *io = nullptr;
    Rt21::Framer framer;
    QQueue<Job> queue;
    std::optional<Job> inflight;
    QTimer *replyTimer = nullptr;
    QTimer *pollTimer = nullptr;
    QTimer *reconnectTimer = nullptr;
    QTimer *gapTimer = nullptr;
    QTimer *commandTimer = nullptr;
    QTimer *noteTimer = nullptr;
    QTimer *connectTimer = nullptr;
    int failures = 0;
    int idlePollMs = 30000;
    int fastPollMs = 400;
    bool fastPace = false;
    bool awaitPrePoll = false;
    bool commandArmed = false;
    bool useAiNext = false;
    bool lastQueryWasAi = false;
    qint64 commandDeadlineMs = 0;
    qint64 lastApMs = 0;
    qint64 lastReplyMs = -1;
    qint64 fastSinceMs = -1;
    int stablePolls = 0;
    double stableHeading = -999;
    QString note_;
    int configTimeouts = 0;
    bool configMode = false;
    bool networkMode = false;
    int networkTries = 0;
    bool probeForNetwork = false;
    bool networkHandshakeDone = false;
    bool countingHandshakeBytes = false;
    bool handshakeSawBytes = false;
    bool writeCompareFailed = false;
    bool brightnessCapture = false;
    bool brightnessWatching = false;
    bool brightnessHold = false;
    int brightnessQueued = -1;
    int brightnessWrote = 0;
    qint64 lastModuleTxMs = -1;
    bool awaitHeadingAfterNetwork = false;
    bool networkHeadingRetried = false;
    QString firmwareReport;
    bool brightnessSawRead = false;
    bool brightnessMatched = false;
    QString brightnessReadText;
    int brightnessPollReplies = 0;
    QTimer *brightnessWatchTimer = nullptr;
    QTimer *brightnessPollTimer = nullptr;
    QTimer *brightnessDeferTimer = nullptr;
    int pollsSinceStatus = 0;
    bool statusDisabled = false;
    std::optional<double> heading_;
    std::optional<double> target_;
    std::optional<double> pendingGoto;
    bool moving = false;
    double lastHeading = -1;
    bool retired = false;
    bool bound = false;
    int suppress = 0;
    qint64 downSinceMs = -1;
    qint64 attemptStartedMs = 0;
    int backoffStep = 0;
    bool linkDownOutage = false;
};
