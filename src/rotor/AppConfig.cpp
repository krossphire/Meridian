/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "AppConfig.h"

#include <QUuid>

#include <algorithm>

AppConfig AppConfig::defaults()
{
    AppConfig config;
    RotorEndpoint rotor;
    rotor.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    rotor.name = QStringLiteral("RT-21");
    rotor.transport = RotorEndpoint::Transport::Unset;
    rotor.host.clear();
    rotor.tcpPort = 0;
    rotor.serialPort.clear();
    rotor.baud = 4800;
    config.rotors = {rotor};
    config.homeText = QStringLiteral("EM26");
    config.active = 0;
    config.trafficLog = true;
    config.showSun = true;
    config.compassButtons = true;
    config.loggerBeamButton = true;
    config.idlePollSeconds = 30;
    config.networkConnection = false;
    return config;
}

AppConfig AppConfig::load(QSettings &settings)
{
    AppConfig config;
    const bool traffic = settings.value(QStringLiteral("log/traffic"), true).toBool();
    const bool sun = settings.value(QStringLiteral("view/sun"), true).toBool();
    const bool compass = settings.value(QStringLiteral("ui/compassButtons"), true).toBool();
    const bool beam = settings.value(QStringLiteral("ui/loggerBeamButton"), true).toBool();
    const QString qrzUser = settings.value(QStringLiteral("qrz/username")).toString();
    const int wsjt = settings.value(QStringLiteral("sources/wsjtPort"), 0).toInt();
    const QString wsjtGroup = settings.value(QStringLiteral("sources/wsjtMulticast")).toString();
    const int n1mm = settings.value(QStringLiteral("sources/n1mmPort"), 0).toInt();
    const bool keeper = settings.value(QStringLiteral("sources/dxKeeper"), false).toBool();
    const int idle = std::clamp(settings.value(QStringLiteral("link/idlePoll"), 30).toInt(), 30, 60);
    const bool network = settings.value(QStringLiteral("link/networkConnection"), false).toBool();
    config.homeText = settings.value(QStringLiteral("home/text"), QStringLiteral("EM26")).toString();
    const int count = settings.beginReadArray(QStringLiteral("rotors"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RotorEndpoint rotor;
        rotor.id = settings.value(QStringLiteral("id")).toString();
        rotor.name = settings.value(QStringLiteral("name"), QStringLiteral("RT-21")).toString();
        const QString kind = settings.value(QStringLiteral("transport")).toString();
        if (kind == QLatin1String("serial"))
            rotor.transport = RotorEndpoint::Transport::Serial;
        else if (kind == QLatin1String("tcp"))
            rotor.transport = RotorEndpoint::Transport::Tcp;
        else
            rotor.transport = RotorEndpoint::Transport::Unset;
        rotor.host = settings.value(QStringLiteral("host")).toString();
        rotor.tcpPort = settings.value(QStringLiteral("tcpPort"), 0).toInt();
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
        config.showSun = sun;
        config.compassButtons = compass;
        config.loggerBeamButton = beam;
        config.qrzUsername = qrzUser;
        config.wsjtPort = wsjt > 0 && wsjt <= 65535 ? wsjt : 0;
        config.wsjtMulticast = wsjtGroup.trimmed();
        config.n1mmPort = n1mm > 0 && n1mm <= 65535 ? n1mm : 0;
        config.dxKeeper = keeper;
        config.idlePollSeconds = idle;
        config.networkConnection = network;
        return config;
    }
    config.trafficLog = traffic;
    config.showSun = sun;
    config.compassButtons = compass;
    config.loggerBeamButton = beam;
    config.qrzUsername = qrzUser;
    config.wsjtPort = wsjt > 0 && wsjt <= 65535 ? wsjt : 0;
    config.wsjtMulticast = wsjtGroup.trimmed();
    config.n1mmPort = n1mm > 0 && n1mm <= 65535 ? n1mm : 0;
    config.dxKeeper = keeper;
    config.idlePollSeconds = idle;
    config.networkConnection = network;
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
        QString kind;
        if (rotor.transport == RotorEndpoint::Transport::Serial)
            kind = QStringLiteral("serial");
        else if (rotor.transport == RotorEndpoint::Transport::Tcp)
            kind = QStringLiteral("tcp");
        settings.setValue(QStringLiteral("transport"), kind);
        settings.setValue(QStringLiteral("host"), rotor.host);
        settings.setValue(QStringLiteral("tcpPort"), rotor.tcpPort);
        settings.setValue(QStringLiteral("serialPort"), rotor.serialPort);
        settings.setValue(QStringLiteral("baud"), rotor.baud);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("rotors/active"), active);
    settings.setValue(QStringLiteral("log/traffic"), trafficLog);
    settings.setValue(QStringLiteral("view/sun"), showSun);
    settings.setValue(QStringLiteral("ui/compassButtons"), compassButtons);
    settings.setValue(QStringLiteral("ui/loggerBeamButton"), loggerBeamButton);
    settings.setValue(QStringLiteral("qrz/username"), qrzUsername.trimmed());
    settings.setValue(QStringLiteral("sources/wsjtPort"), wsjtPort > 0 ? wsjtPort : 0);
    settings.setValue(QStringLiteral("sources/wsjtMulticast"), wsjtMulticast.trimmed());
    settings.setValue(QStringLiteral("sources/n1mmPort"), n1mmPort > 0 ? n1mmPort : 0);
    settings.setValue(QStringLiteral("sources/dxKeeper"), dxKeeper);
    settings.setValue(QStringLiteral("link/idlePoll"), idlePollSeconds);
    settings.setValue(QStringLiteral("link/networkConnection"), networkConnection);
}

bool RotorEndpoint::configured() const
{
    if (transport == Transport::Tcp)
        return !host.trimmed().isEmpty() && tcpPort > 0 && tcpPort <= 65535;
    if (transport == Transport::Serial)
        return !serialPort.trimmed().isEmpty();
    return false;
}

const RotorEndpoint *AppConfig::activeRotor() const
{
    if (active < 0 || active >= rotors.size())
        return nullptr;
    return &rotors.at(active);
}
