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
    enum class Transport { Tcp, Serial } transport = Transport::Tcp;
    QString host = QStringLiteral("192.168.1.106");
    int tcpPort = 6555;
    QString serialPort;
    int baud = 4800;
};

struct AppConfig {
    QString homeText = QStringLiteral("EM26");
    QVector<RotorEndpoint> rotors;
    int active = 0;
    bool trafficLog = true;
    bool showSun = true;
    int idlePollSeconds = 30;

    static AppConfig defaults();
    static AppConfig load(QSettings &settings);
    void save(QSettings &settings) const;

    const RotorEndpoint *activeRotor() const;
};
