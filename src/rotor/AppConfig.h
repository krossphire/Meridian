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
    int idlePollSeconds = 30;

    static AppConfig defaults();
    static AppConfig load(QSettings &settings);
    void save(QSettings &settings) const;
    // The module on this station listens on 6665. An ini saved at the old
    // factory port 6555 is rewritten when it is loaded.
    bool migrateModulePort();

    const RotorEndpoint *activeRotor() const;
};
