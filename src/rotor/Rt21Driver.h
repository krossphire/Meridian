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

#include <QHash>
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
    void writeConfiguration(const QList<QPair<QChar, int>> &changes);
    void sendCalibrate(bool clockwise);
    void sendPulseHeading(double azimuth);

    Link link() const { return link_; }
    std::optional<double> heading() const { return heading_; }
    std::optional<double> target() const { return target_; }
    QString detail() const { return detail_; }

signals:
    void linkChanged(Rt21Driver::Link state, const QString &detail);
    void headingChanged(double azimuth);
    void targetChanged(double azimuth, bool active);
    void configField(QChar id, const QString &raw, bool numeric, int value);
    void configNote(const QString &text);
    void configFinished(bool ok, const QString &message);
    void writeFinished(bool ok, const QString &message);

private:
    enum class Kind { Motion, Heading, Status, Read, Write, Note };

    struct Job {
        Kind kind = Kind::Motion;
        QByteArray bytes;
        QChar id;
        QString note;
    };

    void setLink(Link state, const QString &detail);
    void tearDownSocket();
    void onReadyRead();
    void onConnected();
    void onSocketError();
    void scheduleReconnect();
    void pump();
    void finishInflight();
    void handleFrame(const QByteArray &frame);
    void failLink(const QString &message);
    void startPoll();
    void pollTick();
    void enqueue(Job job);
    void noteArrival(double azimuth, bool haveStatus, int status);

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
    int failures = 0;
    int configTimeouts = 0;
    bool configMode = false;
    int pollsSinceStatus = 0;
    bool statusDisabled = false;
    std::optional<double> heading_;
    std::optional<double> target_;
    std::optional<double> pendingGoto;
    bool moving = false;
    double lastHeading = -1;
};
