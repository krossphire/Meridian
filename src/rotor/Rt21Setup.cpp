/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Setup.h"

#include "UserFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
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
    step.command = eeCommand(letter, value).toLatin1();
    step.readBack = eeRead(letter).toLatin1();
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
    QList<ModuleQuery> rows;
    ModuleQuery enter;
    enter.command = QByteArray("T11;");
    enter.token = QStringLiteral("OK");
    enter.optional = true;
    rows.append(enter);

    ModuleQuery at;
    at.command = QByteArray("AT;");
    at.token = QStringLiteral("OK");
    rows.append(at);

    rows.append(query("AT*HWTYPE?\r", "HWTYPE=", "hwtype"));
    rows.append(query("AT*DHCP?\r", "DHCP=", "dhcp"));
    rows.append(query("AT*IP?\r", "IP=", "ip"));
    rows.append(query("AT*AIP?\r", "AIP=", "aip"));
    rows.append(query("AT*NMASK?\r", "NMASK=", "nmask"));
    rows.append(query("AT*ANMASK?\r", "ANMASK=", "anmask"));
    rows.append(query("AT*GW?\r", "GW=", "gw"));
    rows.append(query("AT*AGW?\r", "AGW=", "agw"));
    rows.append(query("AT*ID?\r", "ID=", "id"));
    rows.append(query("AT*HTTP?\r", "HTTP=", "http"));
    rows.append(query("AT*UDP?\r", "UDP=", "udp"));
    rows.append(query("AT*TFTP?\r", "TFTP=", "tftp"));
    rows.append(query("AT*NVREG16_0?\r", "NVREG16_0=", "serialPort"));
    rows.append(query("AT*WIFI_SECURITY?\r", "SECURITY=", "security"));
    rows.append(query("AT*WIFI_ASSID?\r", "ASSID=", "ssid"));
    rows.append(query("AT*WIFI_KEY?\r", "KEY=", "key"));
    rows.append(query("AT*HTTP_USER?\r", "USER=", "user"));
    rows.append(query("AT*HTTP_PASS?\r", "PASS=", "pass"));
    rows.append(query("AT*LINK?\r", "LINK=", "link"));

    ModuleQuery esc;
    esc.command = QByteArray("\x1b");
    esc.token = QStringLiteral("OK");
    esc.optional = true;
    esc.exitStep = true;
    rows.append(esc);

    ModuleQuery restore;
    restore.command = QByteArray("T01;");
    restore.token = QStringLiteral("OK");
    restore.optional = true;
    restore.exitStep = true;
    rows.append(restore);
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

    if (group == QLatin1String("display"))
        return ee({{QStringLiteral("brightness"), QLatin1Char('O')}}) ? QString() : error;
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
            addModule(out, QStringLiteral("AT*DHCP=%1\r").arg(dhcp), QStringLiteral("AT*DHCP?\r"), QStringLiteral("DHCP="), dhcp);
            addModule(out, QStringLiteral("AT*AIP=%1\r").arg(ip), QStringLiteral("AT*AIP?\r"), QStringLiteral("AIP="), ip);
            addModule(out, QStringLiteral("AT*ANMASK=%1\r").arg(mask), QStringLiteral("AT*ANMASK?\r"), QStringLiteral("ANMASK="), mask);
            addModule(out, QStringLiteral("AT*AGW=%1\r").arg(gateway), QStringLiteral("AT*AGW?\r"), QStringLiteral("AGW="), gateway);
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
            addModule(out, QStringLiteral("AT*HTTP=%1\r").arg(http), QStringLiteral("AT*HTTP?\r"), QStringLiteral("HTTP="),
                      QString::number(http));
            addModule(out, QStringLiteral("AT*UDP=%1\r").arg(udp), QStringLiteral("AT*UDP?\r"), QStringLiteral("UDP="),
                      QString::number(udp));
            addModule(out, QStringLiteral("AT*NVREG16_0=%1\r").arg(serial), QStringLiteral("AT*NVREG16_0?\r"),
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
            addModule(out, QStringLiteral("AT*WIFI_SECURITY=%1\r").arg(security), QStringLiteral("AT*WIFI_SECURITY?\r"),
                      QStringLiteral("SECURITY="), QString::number(security));
            addModule(out, QStringLiteral("AT*WIFI_ASSID=%1\r").arg(ssid), QStringLiteral("AT*WIFI_ASSID?\r"),
                      QStringLiteral("ASSID="), ssid);
            if (security != 0)
                addModule(out, QStringLiteral("AT*WIFI_KEY=%1\r").arg(key), QStringLiteral("AT*WIFI_KEY?\r"),
                          QStringLiteral("KEY="), key);
            return {};
        }
        const QString id = fields.value(QStringLiteral("id")).trimmed();
        if (id.isEmpty())
            return QStringLiteral("The rotator name is empty.");
        addModule(out, QStringLiteral("AT*ID=%1\r").arg(id), QStringLiteral("AT*ID?\r"), QStringLiteral("ID="), id);
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
    text += QStringLiteral("This file is what was read. Writing it back is not enabled in this build.\n");
    text += QStringLiteral("The Wi-Fi key and the web password are included so a later restore has them.\n\n");
    text += QStringLiteral("[controller]\n");
    for (auto it = controller.cbegin(); it != controller.cend(); ++it)
        text += it.key() + QLatin1Char('=') + it.value() + QLatin1Char('\n');
    text += QStringLiteral("\n[network]\n");
    for (auto it = network.cbegin(); it != network.cend(); ++it)
        text += it.key() + QLatin1Char('=') + it.value() + QLatin1Char('\n');
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

} // namespace Rt21
