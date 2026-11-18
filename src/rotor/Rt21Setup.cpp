/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Setup.h"

#include "Rt21Protocol.h"
#include "UserFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>

namespace Rt21 {
namespace {

ModuleQuery query(const char *command, const char *token, const char *key)
{
    ModuleQuery row;
    row.command = QByteArray(command);
    row.token = QString::fromLatin1(token);
    row.key = QString::fromLatin1(key);
    return row;
}

QString eeCommand(QChar letter, int value)
{
    return QStringLiteral("W%1%2%3;").arg(letter).arg(1).arg(value);
}

QString eeRead(QChar letter)
{
    return QStringLiteral("R%1%2;").arg(letter).arg(1);
}

bool takeInt(const QMap<QString, QString> &fields, const QString &key, int *value, QString *error)
{
    bool ok = false;
    const int parsed = fields.value(key).trimmed().toInt(&ok);
    if (!ok) {
        *error = QStringLiteral("Missing a number for %1.").arg(key);
        return false;
    }
    *value = parsed;
    return true;
}

void addEe(QList<PlannedWrite> *out, QChar letter, int value)
{
    PlannedWrite step;
    step.command = registerWriteCommand(letter, value);
    step.readBack = registerReadCommand(letter);
    step.expectValue = QString::number(value);
    out->append(step);
}

void addModule(QList<PlannedWrite> *out, const QString &command, const QString &readBack, const QString &token,
               const QString &expect)
{
    PlannedWrite step;
    step.command = command.toLatin1();
    step.readBack = readBack.toLatin1();
    step.token = token;
    step.expectValue = expect;
    step.module = true;
    out->append(step);
}

} // namespace

QList<ModuleQuery> networkReadSequence()
{
    // Captured from Setup Utility 4.B12 on 4.13.2. Each query is the key plus '?' plus LF.
    // No semicolon, no T11, no T01. AT; and ESC are sent around this list, not inside it.
    QList<ModuleQuery> rows;
    rows.append(query("AT*HWTYPE?\n", "HWTYPE=", "hwtype"));
    rows.append(query("AT*DHCP?\n", "DHCP=", "dhcp"));
    rows.append(query("AT*IP?\n", "IP=", "ip"));
    rows.append(query("AT*AIP?\n", "AIP=", "aip"));
    rows.append(query("AT*NMASK?\n", "NMASK=", "nmask"));
    rows.append(query("AT*GW?\n", "GW=", "gw"));
    rows.append(query("AT*AGW?\n", "AGW=", "agw"));
    rows.append(query("AT*ID?\n", "ID=", "id"));
    rows.append(query("AT*HTTP?\n", "HTTP=", "http"));
    rows.append(query("AT*UDP?\n", "UDP=", "udp"));
    rows.append(query("AT*TFTP?\n", "TFTP=", "tftp"));
    rows.append(query("AT*HTTP_USER?\n", "HTTP_USER=", "user"));
    rows.append(query("AT*HTTP_PASS?\n", "HTTP_PASS=", "pass"));
    rows.append(query("AT*NVREG16_0?\n", "NVREG16_0=", "serialPort"));
    rows.append(query("AT*LINK?\n", "LINK=", "link"));
    return rows;
}

QString rotorTypeName(int code)
{
    static const char *names[] = {
        "Pot", "Counter", "Ham-x", "T2X", "TIC-PST", "Orion", "Spid", "Yaesu G-800/1000",
        "Yaesu G-5400", "Yaesu G-5500", "Yaesu G-2800", "DX Engineering", "Sub-Lunar",
    };
    if (code < 0 || code >= int(sizeof(names) / sizeof(names[0])))
        return QStringLiteral("unknown code");
    return QString::fromLatin1(names[code]);
}

QString modeName(int code)
{
    static const char *names[] = {
        "Normal", "Master/slave", "Master counter-rotate", "Slave counter-rotate", "Offset", "Debug",
    };
    if (code < 0 || code >= int(sizeof(names) / sizeof(names[0])))
        return QStringLiteral("unknown code");
    return QString::fromLatin1(names[code]);
}

QString securityName(int code)
{
    if (code == 0)
        return QStringLiteral("User");
    if (code == 1)
        return QStringLiteral("Admin");
    return QStringLiteral("unknown code");
}

QString wifiSecurityName(int code)
{
    switch (code) {
    case 6:
        return QStringLiteral("WPA2-PSK");
    case 4:
        return QStringLiteral("WPA-PSK");
    case 8:
        return QStringLiteral("WPA Auto");
    case 0:
        return QStringLiteral("None");
    default:
        return QStringLiteral("unknown code");
    }
}

QString addressModeText(const QString &dhcpValue)
{
    if (dhcpValue == QLatin1String("0"))
        return QStringLiteral("DHCP — the router assigns the address");
    if (dhcpValue == QLatin1String("1"))
        return QStringLiteral("Static — the saved address is the one below");
    if (dhcpValue.isEmpty())
        return QStringLiteral("Not read");
    return QStringLiteral("Unrecognised DHCP reply: %1").arg(dhcpValue);
}

QString planGroup(const QString &group, const QMap<QString, QString> &fields, QList<PlannedWrite> *out)
{
    out->clear();
    QString error;
    auto ee = [&](const QList<QPair<QString, QChar>> &rows) {
        for (const auto &row : rows) {
            int value = 0;
            if (!takeInt(fields, row.first, &value, &error))
                return false;
            addEe(out, row.second, value);
        }
        return true;
    };

    if (group == QLatin1String("display")) {
        // Brightness (O) and screen saver (W) only. No other group may write W.
        bool any = false;
        if (fields.contains(QStringLiteral("brightness"))) {
            int level = 0;
            if (!takeInt(fields, QStringLiteral("brightness"), &level, &error))
                return error;
            PlannedWrite step;
            step.command = brightnessWriteCommand(level);
            step.readBack = brightnessReadCommand();
            step.expectValue = QString::number(level);
            out->append(step);
            any = true;
        }
        if (fields.contains(QStringLiteral("screenSaver"))) {
            int minutes = 0;
            if (!takeInt(fields, QStringLiteral("screenSaver"), &minutes, &error))
                return error;
            if (!screenSaverMinuteAllowed(minutes))
                return QStringLiteral("Screen saver is whole minutes: Off, 1, 2, 5, 10, 20, 30, 40, 50, or 60. 90 seconds is not a value this rotor stores.");
            addEe(out, QLatin1Char('W'), minutes);
            any = true;
        }
        return any ? QString() : QStringLiteral("Nothing to write in Display.");
    }
    if (group == QLatin1String("motion")) {
        return ee({{QStringLiteral("minSpeed"), QLatin1Char('F')},
                   {QStringLiteral("maxSpeed"), QLatin1Char('G')},
                   {QStringLiteral("ramp"), QLatin1Char('N')},
                   {QStringLiteral("delays"), QLatin1Char('E')},
                   {QStringLiteral("counterDelay"), QLatin1Char('V')}})
                   ? QString()
                   : error;
    }
    if (group == QLatin1String("stops")) {
        return ee({{QStringLiteral("ccwLimit"), QLatin1Char('H')},
                   {QStringLiteral("cwLimit"), QLatin1Char('I')},
                   {QStringLiteral("calRange"), QLatin1Char('L')},
                   {QStringLiteral("overtravel"), QLatin1Char('U')}})
                   ? QString()
                   : error;
    }
    if (group == QLatin1String("calibration")) {
        return ee({{QStringLiteral("ccwCal"), QLatin1Char('A')},
                   {QStringLiteral("cwCal"), QLatin1Char('B')},
                   {QStringLiteral("offset"), QLatin1Char('D')},
                   {QStringLiteral("divider"), QLatin1Char('K')},
                   {QStringLiteral("alt1"), QLatin1Char('R')},
                   {QStringLiteral("alt2"), QLatin1Char('S')}})
                   ? QString()
                   : error;
    }
    if (group == QLatin1String("advanced"))
        return ee({{QStringLiteral("advanced"), QLatin1Char('T')}}) ? QString() : error;
    if (group == QLatin1String("rotor-type")) {
        return ee({{QStringLiteral("option"), QLatin1Char('J')},
                   {QStringLiteral("mode"), QLatin1Char('M')},
                   {QStringLiteral("security"), QLatin1Char('Q')}})
                   ? QString()
                   : error;
    }
    if (group == QLatin1String("network-address") || group == QLatin1String("network-ports")
        || group == QLatin1String("network-wifi") || group == QLatin1String("network-name")) {
        if (group == QLatin1String("network-address")) {
            const QString dhcp = fields.value(QStringLiteral("dhcp")).trimmed();
            if (dhcp != QLatin1String("0") && dhcp != QLatin1String("1"))
                return QStringLiteral("DHCP must be 0 or 1.");
            const QString ip = fields.value(QStringLiteral("aip")).trimmed();
            const QString mask = fields.value(QStringLiteral("anmask")).trimmed();
            const QString gateway = fields.value(QStringLiteral("agw")).trimmed();
            QHostAddress check;
            if (!check.setAddress(ip) || !check.setAddress(mask) || !check.setAddress(gateway))
                return QStringLiteral("Static address, mask, and gateway each need an IPv4 address.");
            addModule(out, QStringLiteral("AT*DHCP=%1\n").arg(dhcp), QStringLiteral("AT*DHCP?\n"), QStringLiteral("DHCP="), dhcp);
            addModule(out, QStringLiteral("AT*AIP=%1\n").arg(ip), QStringLiteral("AT*AIP?\n"), QStringLiteral("AIP="), ip);
            addModule(out, QStringLiteral("AT*ANMASK=%1\n").arg(mask), QStringLiteral("AT*ANMASK?\n"), QStringLiteral("ANMASK="), mask);
            addModule(out, QStringLiteral("AT*AGW=%1\n").arg(gateway), QStringLiteral("AT*AGW?\n"), QStringLiteral("AGW="), gateway);
            return {};
        }
        if (group == QLatin1String("network-ports")) {
            int http = 0;
            int udp = 0;
            int serial = 0;
            if (!takeInt(fields, QStringLiteral("http"), &http, &error)
                || !takeInt(fields, QStringLiteral("udp"), &udp, &error)
                || !takeInt(fields, QStringLiteral("serialPort"), &serial, &error))
                return error;
            if (http < 1 || http > 65535 || serial < 1 || serial > 65535 || udp < 0 || udp > 65535)
                return QStringLiteral("Ports have to sit between 1 and 65535.");
            addModule(out, QStringLiteral("AT*HTTP=%1\n").arg(http), QStringLiteral("AT*HTTP?\n"), QStringLiteral("HTTP="),
                      QString::number(http));
            addModule(out, QStringLiteral("AT*UDP=%1\n").arg(udp), QStringLiteral("AT*UDP?\n"), QStringLiteral("UDP="),
                      QString::number(udp));
            addModule(out, QStringLiteral("AT*NVREG16_0=%1\n").arg(serial), QStringLiteral("AT*NVREG16_0?\n"),
                      QStringLiteral("NVREG16_0="), QString::number(serial));
            return {};
        }
        if (group == QLatin1String("network-wifi")) {
            int security = 0;
            if (!takeInt(fields, QStringLiteral("security"), &security, &error))
                return error;
            if (wifiSecurityName(security) == QLatin1String("unknown code"))
                return QStringLiteral("Wi-Fi security has to be one of 0, 4, 6, or 8.");
            const QString ssid = fields.value(QStringLiteral("ssid"));
            const QString key = fields.value(QStringLiteral("key"));
            if (ssid.trimmed().isEmpty())
                return QStringLiteral("The network name is empty.");
            if (security != 0 && key.size() < 8)
                return QStringLiteral("The setup utility refuses a passphrase shorter than 8 characters.");
            addModule(out, QStringLiteral("AT*WIFI_SECURITY=%1\n").arg(security), QStringLiteral("AT*WIFI_SECURITY?\n"),
                      QStringLiteral("SECURITY="), QString::number(security));
            addModule(out, QStringLiteral("AT*WIFI_ASSID=%1\n").arg(ssid), QStringLiteral("AT*WIFI_ASSID?\n"),
                      QStringLiteral("ASSID="), ssid);
            if (security != 0)
                addModule(out, QStringLiteral("AT*WIFI_KEY=%1\n").arg(key), QStringLiteral("AT*WIFI_KEY?\n"),
                          QStringLiteral("KEY="), key);
            return {};
        }
        const QString id = fields.value(QStringLiteral("id")).trimmed();
        if (id.isEmpty())
            return QStringLiteral("The rotator name is empty.");
        addModule(out, QStringLiteral("AT*ID=%1\n").arg(id), QStringLiteral("AT*ID?\n"), QStringLiteral("ID="), id);
        return {};
    }
    if (group == QLatin1String("factory") || group == QLatin1String("bootloader") || group == QLatin1String("module-reset")
        || group == QLatin1String("tftp"))
        return QStringLiteral("That command is not available.");
    return QStringLiteral("Unknown settings group.");
}

QString formatSetupBackup(const QMap<QString, QString> &controller, const QMap<QString, QString> &network)
{
    QString text;
    text += QStringLiteral("Meridian RT-21 setup copy\n");
    text += QStringLiteral("Saved %1\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    text += QStringLiteral("Designed and tested with firmware 4.13.2. 4.14 is a pre-release; its screen saver and register writes hang serial.\n");
    text += QStringLiteral("This file is what was read. Nothing was written back.\n");
    text += QStringLiteral("The Wi-Fi key and the web password are included so a later restore has them.\n\n");
    if (controller.contains(QStringLiteral("version")))
        text += QStringLiteral("Firmware: %1\n").arg(controller.value(QStringLiteral("version")));
    if (controller.contains(QStringLiteral("pot")))
        text += QStringLiteral("Pot A/D: %1\n").arg(controller.value(QStringLiteral("pot")));
    if (controller.contains(QStringLiteral("reset")))
        text += QStringLiteral("Last abnormal reset (R41): %1\n").arg(controller.value(QStringLiteral("reset")));
    text += QLatin1Char('\n');
    text += screenSaverRecommendation();
    text += QStringLiteral("\n\n[controller]\n");
    for (auto it = controller.cbegin(); it != controller.cend(); ++it) {
        if (it.key().size() == 1)
            text += formatEeLine(it.key(), it.value()) + QLatin1Char('\n');
    }
    text += QStringLiteral("\n[network]\n");
    for (auto it = network.cbegin(); it != network.cend(); ++it) {
        const QString shown = (it.key() == QLatin1String("key") || it.key() == QLatin1String("pass"))
                                  ? it.value()
                                  : it.value();
        text += it.key() + QStringLiteral(" = ") + shown + QLatin1Char('\n');
    }
    text += QStringLiteral("\nDNS is not a field in the setup utility.\n");
    text += QStringLiteral("AP or client mode is not a field in the setup utility.\n");
    return text;
}

QString saveSetupBackup(const QMap<QString, QString> &controller, const QMap<QString, QString> &network, QString *error)
{
    const QString path = QDir(UserFiles::backupDirectory())
                             .filePath(QStringLiteral("meridian-setup-%1.txt")
                                           .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString tmp = path + QStringLiteral(".tmp");
    QFile file(tmp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("Couldn't write %1.").arg(path);
        return {};
    }
    file.write(formatSetupBackup(controller, network).toUtf8());
    file.close();
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        if (error)
            *error = QStringLiteral("Couldn't write %1.").arg(path);
        return {};
    }
    QJsonObject machine;
    for (auto it = controller.cbegin(); it != controller.cend(); ++it)
        machine.insert(it.key(), it.value());
    QJsonObject net;
    for (auto it = network.cbegin(); it != network.cend(); ++it)
        net.insert(it.key(), it.value());
    QJsonObject root;
    root.insert(QStringLiteral("controller"), machine);
    root.insert(QStringLiteral("network"), net);
    const QString jsonPath = path.endsWith(QStringLiteral(".txt")) ? path.chopped(4) + QStringLiteral(".json")
                                                                    : path + QStringLiteral(".json");
    QFile json(jsonPath + QStringLiteral(".tmp"));
    if (json.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        json.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        json.close();
        QFile::remove(jsonPath);
        QFile::rename(jsonPath + QStringLiteral(".tmp"), jsonPath);
    }
    return path;
}

QByteArray redactForLog(const QByteArray &bytes)
{
    const QList<QByteArray> markers = {"AT*WIFI_KEY=", "AT*HTTP_PASS=", "KEY=", "PASS="};
    QByteArray out = bytes;
    for (const QByteArray &marker : markers) {
        int from = 0;
        while (true) {
            const int at = out.indexOf(marker, from);
            if (at < 0)
                break;
            int end = at + marker.size();
            while (end < out.size()) {
                const char c = out.at(end);
                if (c == '\r' || c == '\n' || c == ';')
                    break;
                ++end;
            }
            out.replace(at + marker.size(), end - (at + marker.size()), "****");
            from = at + marker.size() + 4;
        }
    }
    return out;
}

QList<int> tcpPortCandidates()
{
    QList<int> ports;
    for (int port = 6555; port <= 6564; ++port)
        ports.append(port);
    ports << 4001 << 5000 << 8080 << 10001 << 20001;
    return ports;
}

int recommendTcpPort(const QList<int> &busy)
{
    for (int port : tcpPortCandidates()) {
        if (!busy.contains(port))
            return port;
    }
    return 0;
}

QString gatewayFromProcRoute(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList parts = lines.at(i).split(QLatin1Char('\t'));
        if (parts.size() < 3 || parts.at(1) != QLatin1String("00000000"))
            continue;
        bool ok = false;
        const quint32 word = parts.at(2).toUInt(&ok, 16);
        if (!ok || word == 0)
            continue;
        const quint32 address = ((word & 0xffu) << 24) | ((word & 0xff00u) << 8) | ((word & 0xff0000u) >> 8) | (word >> 24);
        return QHostAddress(address).toString();
    }
    return {};
}

QString gatewaySubnetNote(const QString &ip, const QString &mask, const QString &gateway)
{
    QHostAddress address;
    QHostAddress netmask;
    QHostAddress gate;
    if (!address.setAddress(ip.trimmed()) || !netmask.setAddress(mask.trimmed()) || !gate.setAddress(gateway.trimmed()))
        return {};
    if (address.protocol() != QAbstractSocket::IPv4Protocol || netmask.protocol() != QAbstractSocket::IPv4Protocol
        || gate.protocol() != QAbstractSocket::IPv4Protocol)
        return {};
    const quint32 network = address.toIPv4Address() & netmask.toIPv4Address();
    const quint32 gateNet = gate.toIPv4Address() & netmask.toIPv4Address();
    if (network == gateNet)
        return {};
    return QStringLiteral("The gateway is not on the same subnet as the static address. That is often a typo.");
}

QString dottedMask(int prefixLength)
{
    if (prefixLength < 0 || prefixLength > 32)
        return {};
    const quint32 mask = prefixLength == 0 ? 0u : (0xffffffffu << (32 - prefixLength));
    return QHostAddress(mask).toString();
}

StaticAdvice adviseStaticAddress(const QString &localIp, int prefixLength, const QString &gateway,
                                 const QStringList &taken)
{
    StaticAdvice advice;
    advice.gateway = gateway.trimmed();
    advice.mask = dottedMask(prefixLength);
    QHostAddress local;
    if (!local.setAddress(localIp) || local.protocol() != QAbstractSocket::IPv4Protocol || prefixLength != 24) {
        advice.explanation = QStringLiteral(
            "This computer doesn't have a simple IPv4 network to copy. Pick an address your router will not hand out, "
            "or reserve one in the router if it allows that.");
        return advice;
    }
    const quint32 host = local.toIPv4Address();
    const quint32 network = host & 0xffffff00u;
    auto busy = [&](quint32 address) {
        const QString text = QHostAddress(address).toString();
        if (text == localIp || text == advice.gateway)
            return true;
        return taken.contains(text);
    };
    const QList<int> preferred = [] {
        QList<int> hosts;
        for (int n = 20; n <= 49; ++n)
            hosts.append(n);
        for (int n = 50; n <= 99; ++n)
            hosts.append(n);
        for (int n = 200; n <= 250; ++n)
            hosts.append(n);
        return hosts;
    }();
    for (int hostByte : preferred) {
        const quint32 address = network | quint32(hostByte);
        if (!busy(address)) {
            advice.address = QHostAddress(address).toString();
            break;
        }
    }
    if (advice.address.isEmpty()) {
        advice.explanation = QStringLiteral("Every quiet address on this subnet looks taken.");
        return advice;
    }
    advice.explanation = QStringLiteral(
        "This computer is %1, mask %2%3. %4 is outside the .100–.199 range many home routers give out automatically. "
        "If your router uses a different pool, pick an address it will not give away, or reserve this one.")
                             .arg(localIp, advice.mask.isEmpty() ? QStringLiteral("unknown") : advice.mask,
                                  advice.gateway.isEmpty() ? QString() : QStringLiteral(", gateway %1").arg(advice.gateway),
                                  advice.address);
    return advice;
}

bool setupWritesCompiledIn()
{
#if MERIDIAN_ALPHA_READONLY
    return false;
#else
    return true;
#endif
}

FirmwareClass classifyFirmware(const QString &report)
{
    if (report.contains(QStringLiteral("4.13.2")))
        return FirmwareClass::Tested;
    if (report.contains(QStringLiteral("4.14")))
        return FirmwareClass::PreRelease;
    return FirmwareClass::Untested;
}

QString firmwareNotice(const QString &report)
{
    const FirmwareClass kind = classifyFirmware(report);
    if (kind == FirmwareClass::Tested)
        return {};
    if (kind == FirmwareClass::PreRelease)
        return QStringLiteral(
            "Firmware 4.14 is a pre-release; on 4.14.g1 the screen saver and register writes hang serial. 4.13.2 is recommended.");
    const QString shown = report.trimmed().isEmpty() ? QStringLiteral("nothing") : report.trimmed();
    return QStringLiteral("Writes stay off. 4.13.2 is the tested firmware. This unit reports %1.").arg(shown);
}

bool firmwareWritesAllowed(const QString &report)
{
    return classifyFirmware(report) == FirmwareClass::Tested;
}

bool screenSaverMinuteAllowed(int minutes)
{
    switch (minutes) {
    case 0:
    case 1:
    case 2:
    case 5:
    case 10:
    case 20:
    case 30:
    case 40:
    case 50:
    case 60:
        return true;
    default:
        return false;
    }
}

int recommendedScreenSaverMinutes()
{
    // Off. On 4.14.g1 a 1-minute saver stops serial until a power cycle.
    return 0;
}

QString screenSaverUnsafeNote()
{
    return QStringLiteral(
        "Screen saver is on. On firmware 4.14.g1 it stops serial and network replies after the timeout. Set it to Off.");
}

QString screenSaverRecommendation()
{
    return QStringLiteral(
        "On 4.13.2 the screen saver blanks the panel and serial keeps answering. Polls do not count as activity, and a turn command wakes the display. "
        "On 4.14.g1 the same 1-minute saver stops serial until a power cycle. 4.13.2 is the firmware to run, especially on an OLED unit. Off leaves the heading on the display. "
        "The Display page writes this register as WW0 plus the minutes, and only after a read reports 4.13.2.");
}

QString advancedOptionsText(int value)
{
    static const char *names[] = {
        "freeze display", "fast brake release", "auto go-back", "elevation",
        "auto dim", "analog heading out", "EME", "point-and-shoot",
        "port is 9600", "2-bit encoder", "demo", "allow cal to the CCW limit",
    };
    QStringList on;
    for (int bit = 0; bit < 12; ++bit) {
        if (value & (1 << bit))
            on.append(QString::fromLatin1(names[bit]));
    }
    if (on.isEmpty())
        return QStringLiteral("no documented bits set");
    return on.join(QStringLiteral(", "));
}

QString formatEeLine(const QString &id, const QString &raw)
{
    const QString token = raw.trimmed();
    bool numeric = false;
    const int value = token.toInt(&numeric);
    auto named = [&](const QString &name, const QString &detail) {
        return QStringLiteral("%1 (%2) = %3").arg(name, id, detail);
    };
    if (!numeric)
        return named(id == QLatin1String("W") ? QStringLiteral("Screen saver") : id, token.isEmpty() ? QStringLiteral("no reply") : token);
    if (id == QLatin1String("W")) {
        const QString state = value == 0 ? QStringLiteral("Off (0 minutes)") : QStringLiteral("%1 minutes").arg(value);
        QString extra;
        if (value == 0)
            extra = QStringLiteral(". Off is the factory default. An OLED will keep showing the heading.");
        else if (!screenSaverMinuteAllowed(value))
            extra = QStringLiteral(". That is not one of the utility's steps.");
        else if (value > recommendedScreenSaverMinutes())
            extra = QStringLiteral(". On 4.13.2 this blanks the panel and serial keeps answering. On 4.14.x it stops serial until a power cycle.");
        return named(QStringLiteral("Screen saver"), state + extra);
    }
    if (id == QLatin1String("J"))
        return named(QStringLiteral("Rotor type"), QStringLiteral("%1 (%2)").arg(value).arg(rotorTypeName(value)));
    if (id == QLatin1String("M"))
        return named(QStringLiteral("Mode"), QStringLiteral("%1 (%2)").arg(value).arg(modeName(value)));
    if (id == QLatin1String("Q"))
        return named(QStringLiteral("Security"), QStringLiteral("%1 (%2)").arg(value).arg(securityName(value)));
    if (id == QLatin1String("T"))
        return named(QStringLiteral("Advanced options"), QStringLiteral("%1 (%2)").arg(value).arg(advancedOptionsText(value)));
    if (id == QLatin1String("R") || id == QLatin1String("S")) {
        const QString which = id == QLatin1String("R") ? QStringLiteral("Alt offset 1") : QStringLiteral("Alt offset 2");
        const QString note = value == 361 ? QStringLiteral(" (361 means unused)") : QString();
        return named(which, QStringLiteral("%1 degrees%2").arg(value).arg(note));
    }
    if (id == QLatin1String("K"))
        return named(QStringLiteral("Pulse divider"), QStringLiteral("%1 (manual default is 3960)").arg(value));
    if (id == QLatin1String("F"))
        return named(QStringLiteral("Min speed"), QStringLiteral("%1 (the 2014 manual's scale is 0 to 10)").arg(value));
    if (id == QLatin1String("O"))
        return named(QStringLiteral("Brightness"), QString::number(value));
    if (id == QLatin1String("D") || id == QLatin1String("H") || id == QLatin1String("I") || id == QLatin1String("L") || id == QLatin1String("U")) {
        const QString name = id == QLatin1String("D") ? QStringLiteral("Offset")
                             : id == QLatin1String("H") ? QStringLiteral("CCW limit")
                             : id == QLatin1String("I") ? QStringLiteral("CW limit")
                             : id == QLatin1String("L") ? QStringLiteral("Cal range")
                                                        : QStringLiteral("Overtravel");
        return named(name, QStringLiteral("%1 degrees").arg(value));
    }
    if (id == QLatin1String("C"))
        return named(QStringLiteral("Reserved"), QString::number(value));
    if (id == QLatin1String("Z"))
        return named(QStringLiteral("Multiplier"), QString::number(value));
    if (id == QLatin1String("["))
        return named(QStringLiteral("Magnetic variation"), QStringLiteral("%1 degrees").arg(value));
    if (id == QLatin1String("X"))
        return named(QStringLiteral("Limit switch"), QString::number(value));
    return named(id, QString::number(value));
}

} // namespace Rt21
