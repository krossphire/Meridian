/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Setup-utility command map. Bytes come from the RT-21 manuals and from
 * Setup Utility 4.B12. Nothing here has been confirmed on Andrew's rotor.
 */

#pragma once

#include <QList>
#include <QMap>
#include <QString>

class QHostAddress;

namespace Rt21 {

struct ModuleQuery {
    QByteArray command;
    QString token;
    QString key;
    bool optional = false;
    bool exitStep = false;
};

struct PlannedWrite {
    QByteArray command;
    QByteArray readBack;
    QString token;
    QString expectValue;
    bool module = false;
};

struct StaticAdvice {
    QString address;
    QString mask;
    QString gateway;
    QString explanation;
};

QList<ModuleQuery> networkReadSequence();

QString rotorTypeName(int code);
QString modeName(int code);
QString securityName(int code);
QString wifiSecurityName(int code);
QString addressModeText(const QString &dhcpValue);

// Empty string when the reply is not one of the utility's codes.
int codeFromName(const QString &name, const QStringList &names);

QString planGroup(const QString &group, const QMap<QString, QString> &fields, QList<PlannedWrite> *out);

QString formatSetupBackup(const QMap<QString, QString> &controller, const QMap<QString, QString> &network);
QString saveSetupBackup(const QMap<QString, QString> &controller, const QMap<QString, QString> &network, QString *error);

QByteArray redactForLog(const QByteArray &bytes);

int recommendTcpPort(const QList<int> &busy);
QList<int> tcpPortCandidates();

QString gatewayFromProcRoute(const QString &text);
QString gatewaySubnetNote(const QString &ip, const QString &mask, const QString &gateway);
QString dottedMask(int prefixLength);
StaticAdvice adviseStaticAddress(const QString &localIp, int prefixLength, const QString &gateway,
                                 const QStringList &taken);

bool setupWritesCompiledIn();

// Tested firmware is the public 4.13.2. 4.14 is a pre-release; writes stay off.
enum class FirmwareClass { Tested, PreRelease, Untested };
FirmwareClass classifyFirmware(const QString &report);
QString firmwareNotice(const QString &report);
bool firmwareWritesAllowed(const QString &report);

// Utility combo, in minutes. 0 is Off. 90 seconds is not one of these.
bool screenSaverMinuteAllowed(int minutes);
int recommendedScreenSaverMinutes();
QString screenSaverRecommendation();
QString screenSaverUnsafeNote();
QString formatEeLine(const QString &id, const QString &raw);
QString advancedOptionsText(int value);

} // namespace Rt21
