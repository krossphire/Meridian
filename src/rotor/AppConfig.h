/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QSettings>
#include <QString>
#include <QVector>

struct RotorEndpoint {
    QString id;
    QString name;
    enum class Transport { Unset, Tcp, Serial } transport = Transport::Unset;
    QString host;
    int tcpPort = 0;
    QString serialPort;
    int baud = 4800;

    // True only when this rotor has a transport and the fields that transport needs.
    bool configured() const;
};

struct AppConfig {
    QString homeText = QStringLiteral("EM26");
    QVector<RotorEndpoint> rotors;
    int active = 0;
    bool trafficLog = true;
    bool showSun = true;
    bool compassButtons = true;
    bool loggerBeamButton = true;
    QString qrzUsername;
    int wsjtPort = 0;
    QString wsjtMulticast;
    int n1mmPort = 0;
    int idlePollSeconds = 30;
    // Serial RT-21s with no network board stay No. meridian.ini is the only store.
    bool networkConnection = false;

    static AppConfig defaults();
    static AppConfig load(QSettings &settings);
    void save(QSettings &settings) const;

    const RotorEndpoint *activeRotor() const;
};
