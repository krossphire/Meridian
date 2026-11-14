/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "AppConfig.h"

#include <QUuid>

AppConfig AppConfig::defaults()
{
    AppConfig config;
    RotorEndpoint rotor;
    rotor.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rotor.name = QStringLiteral("RT-21");
    rotor.transport = RotorEndpoint::Transport::Tcp;
    rotor.host = QStringLiteral("192.168.1.106");
    rotor.tcpPort = 6555;
    rotor.baud = 4800;
    config.rotors = {rotor};
    config.homeText = QStringLiteral("EM26");
    config.active = 0;
    config.trafficLog = true;
    return config;
}

AppConfig AppConfig::load(QSettings &settings)
{
    AppConfig config;
    const bool traffic = settings.value(QStringLiteral("log/traffic"), true).toBool();
    config.homeText = settings.value(QStringLiteral("home/text"), QStringLiteral("EM26")).toString();
    const int count = settings.beginReadArray(QStringLiteral("rotors"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RotorEndpoint rotor;
        rotor.id = settings.value(QStringLiteral("id")).toString();
        rotor.name = settings.value(QStringLiteral("name"), QStringLiteral("RT-21")).toString();
        rotor.transport = settings.value(QStringLiteral("transport")).toString() == QLatin1String("serial")
                              ? RotorEndpoint::Transport::Serial
                              : RotorEndpoint::Transport::Tcp;
        rotor.host = settings.value(QStringLiteral("host"), QStringLiteral("192.168.1.106")).toString();
        rotor.tcpPort = settings.value(QStringLiteral("tcpPort"), 6555).toInt();
        rotor.serialPort = settings.value(QStringLiteral("serialPort")).toString();
        rotor.baud = settings.value(QStringLiteral("baud"), 4800).toInt();
        if (rotor.id.isEmpty())
            rotor.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        config.rotors.append(rotor);
    }
    settings.endArray();
    if (config.rotors.isEmpty()) {
        config = defaults();
        config.trafficLog = traffic;
        return config;
    }
    config.trafficLog = traffic;
    config.active = settings.value(QStringLiteral("rotors/active"), 0).toInt();
    if (config.active < 0 || config.active >= config.rotors.size())
        config.active = 0;
    return config;
}

void AppConfig::save(QSettings &settings) const
{
    settings.setValue(QStringLiteral("home/text"), homeText);
    settings.beginWriteArray(QStringLiteral("rotors"), rotors.size());
    for (int i = 0; i < rotors.size(); ++i) {
        settings.setArrayIndex(i);
        const RotorEndpoint &rotor = rotors.at(i);
        settings.setValue(QStringLiteral("id"), rotor.id);
        settings.setValue(QStringLiteral("name"), rotor.name);
        settings.setValue(QStringLiteral("transport"),
                          rotor.transport == RotorEndpoint::Transport::Serial ? QStringLiteral("serial")
                                                                              : QStringLiteral("tcp"));
        settings.setValue(QStringLiteral("host"), rotor.host);
        settings.setValue(QStringLiteral("tcpPort"), rotor.tcpPort);
        settings.setValue(QStringLiteral("serialPort"), rotor.serialPort);
        settings.setValue(QStringLiteral("baud"), rotor.baud);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("rotors/active"), active);
    settings.setValue(QStringLiteral("log/traffic"), trafficLog);
}

const RotorEndpoint *AppConfig::activeRotor() const
{
    if (active < 0 || active >= rotors.size())
        return nullptr;
    return &rotors.at(active);
}
