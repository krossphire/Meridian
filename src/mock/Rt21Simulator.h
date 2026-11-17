/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A small RT-21 stand-in for tests and screenshots. One TCP client at a time:
 * the listening socket closes while a client is connected, so a second connect
 * is refused.
 */

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;

class Rt21Simulator : public QObject
{
    Q_OBJECT
public:
    explicit Rt21Simulator(QObject *parent = nullptr);

    void setHeading(double azimuth);
    void setSpeed(double degreesPerSecond);
    void setFrozen(bool frozen);
    void setIgnoreQueries(bool ignore);
    void setIgnoreModule(bool ignore);
    void clearTrace();
    QList<QByteArray> trace() const { return trace_; }
    double heading() const { return heading_; }
    double target() const { return target_; }
    bool moving() const { return moving_; }

    void feed(const QByteArray &bytes);
    void advance(double seconds);

signals:
    void output(const QByteArray &bytes);

private:
    void handle(const QByteArray &command, bool immediate);
    void reply(const QByteArray &payload, bool soh);
    void replyLine(const QByteArray &line);
    void startMotion();
    void restoreDefaults();

    double heading_ = 0;
    double target_ = 0;
    double preset_ = 0;
    double speed_ = 6;
    bool frozen_ = false;
    bool moving_ = false;
    bool sawCr = false;
    QByteArray buffer;
    QHash<QChar, int> ee;
    QHash<QByteArray, QByteArray> module;
    bool atMode = false;
    qint64 uart2AtMs = 0;
    int pot = 512;
    int biReplies = 0;
    bool ignoreQueries = false;
    bool ignoreModule = false;
    QList<QByteArray> trace_;
};

class MockRt21Server : public QObject
{
    Q_OBJECT
public:
    explicit MockRt21Server(QObject *parent = nullptr);

    Rt21Simulator *sim() { return &sim_; }
    bool listenTcp(quint16 port);
    quint16 port() const;
    int accepts() const { return accepts_; }
    bool hasClient() const;
    // Returns the slave path, or empty if a pty could not be opened.
    QString listenPty();

private:
    void acceptOne();
    void clientGone();

    Rt21Simulator sim_;
    QTcpServer *server = nullptr;
    QTcpSocket *client = nullptr;
    quint16 tcpPort = 0;
    int accepts_ = 0;
    QTimer *motion = nullptr;
    int ptyMaster = -1;
    QString ptyName;
    class QSocketNotifier *ptyNotifier = nullptr;
};
