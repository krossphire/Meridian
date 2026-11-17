/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SetupPanel.h"

#include "rotor/Rt21Driver.h"
#include "rotor/Rt21Finder.h"
#include "rotor/Rt21Setup.h"

#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QStackedWidget>
#include <QTcpServer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QLineEdit *roField(const QString &name)
{
    auto *edit = new QLineEdit;
    edit->setObjectName(name);
    edit->setReadOnly(true);
    edit->setPlaceholderText(QStringLiteral("Not read"));
    return edit;
}

QString localSerialPorts()
{
    QList<SerialPortChoice> choices;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString line = info.portName();
        if (!info.description().isEmpty())
            line += QStringLiteral(" — ") + info.description();
        choices.append(SerialPortChoice{info.portName(), line});
    }
    choices = arrangeSerialPorts(choices, QString());
    if (choices.isEmpty())
        return QStringLiteral("No serial or USB ports are visible on this computer.");
    QStringList lines;
    for (const SerialPortChoice &choice : choices)
        lines.append(choice.label);
    return lines.join(QLatin1Char('\n'));
}

QList<int> busyTcpPorts()
{
    QList<int> busy;
    for (int port : Rt21::tcpPortCandidates()) {
        QTcpServer server;
        if (!server.listen(QHostAddress::AnyIPv4, quint16(port)))
            busy.append(port);
    }
    return busy;
}

struct LocalNet {
    QString ip;
    int prefix = -1;
    QString gateway;
};

LocalNet thisComputer()
{
    LocalNet net;
    QFile route(QStringLiteral("/proc/net/route"));
    if (route.open(QIODevice::ReadOnly))
        net.gateway = Rt21::gatewayFromProcRoute(QString::fromLatin1(route.readAll()));
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning))
            continue;
        if (flags & QNetworkInterface::IsLoopBack)
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (entry.ip().isLoopback())
                continue;
            net.ip = entry.ip().toString();
            net.prefix = entry.prefixLength();
            return net;
        }
    }
    return net;
}

} // namespace

SetupPanel::SetupPanel(QObject *parent)
    : QObject(parent)
{
    buildPorts();
    buildNetwork();
}

void SetupPanel::buildPorts()
{
    ports = new QWidget;
    auto *layout = new QVBoxLayout(ports);
    auto *intro = new QLabel(QStringLiteral(
        "The rotor uses the same commands on RS-232, USB, and the network module's serial TCP port. "
        "4800 8N1 is the manual default. 9600 is one bit in advanced options. "
        "The baud menu in the Green Heron utility changes this computer, not the rotor."));
    intro->setWordWrap(true);
    controllerSpeed = new QLabel(QStringLiteral("Controller speed: not read yet."));
    controllerSpeed->setWordWrap(true);
    savedLink = new QLabel;
    savedLink->setWordWrap(true);
    localPorts = new QLabel(localSerialPorts());
    localPorts->setWordWrap(true);
    moduleSerial = new QLabel(QStringLiteral("Module serial TCP port: not read yet."));
    moduleSerial->setWordWrap(true);
    auto *prefer = new QLabel(QStringLiteral(
        "Use USB or serial for a network change when you can. Changing the address over IP drops that connection."));
    prefer->setWordWrap(true);
    layout->addWidget(intro);
    layout->addWidget(controllerSpeed);
    layout->addWidget(savedLink);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("Serial ports on this computer")));
    layout->addWidget(localPorts);
    layout->addWidget(moduleSerial);
    layout->addWidget(prefer);
    layout->addStretch();
}

void SetupPanel::buildNetwork()
{
    network = new QWidget;
    auto *layout = new QVBoxLayout(network);
    auto *banner = new QLabel(QStringLiteral(
        "A network read talks to the module over USB or serial: AT;, a port cycle, then AT* lines. "
        "It stays off on an IP-only link."));
    banner->setWordWrap(true);

    auto *formHost = new QWidget;
    auto *form = new QFormLayout(formHost);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("id"), QStringLiteral("Rotator name")},
        {QStringLiteral("dhcp"), QStringLiteral("Address mode")},
        {QStringLiteral("ip"), QStringLiteral("Current IP")},
        {QStringLiteral("aip"), QStringLiteral("Static IP")},
        {QStringLiteral("nmask"), QStringLiteral("Current mask")},
        {QStringLiteral("anmask"), QStringLiteral("Static mask")},
        {QStringLiteral("gw"), QStringLiteral("Current gateway")},
        {QStringLiteral("agw"), QStringLiteral("Static gateway")},
        {QStringLiteral("dns"), QStringLiteral("DNS")},
        {QStringLiteral("http"), QStringLiteral("HTTP port")},
        {QStringLiteral("serialPort"), QStringLiteral("Serial TCP port")},
        {QStringLiteral("udp"), QStringLiteral("UDP port")},
        {QStringLiteral("tftp"), QStringLiteral("TFTP port")},
        {QStringLiteral("security"), QStringLiteral("Wi-Fi security")},
        {QStringLiteral("ssid"), QStringLiteral("Wi-Fi name")},
        {QStringLiteral("key"), QStringLiteral("Wi-Fi key")},
        {QStringLiteral("mode"), QStringLiteral("AP or client")},
        {QStringLiteral("user"), QStringLiteral("Web user")},
        {QStringLiteral("pass"), QStringLiteral("Web password")},
        {QStringLiteral("link"), QStringLiteral("Link")},
        {QStringLiteral("hwtype"), QStringLiteral("Module type")},
    };
    for (const auto &row : rows) {
        auto *edit = roField(QStringLiteral("net-") + row.first);
        if (row.first == QLatin1String("key"))
            edit->setEchoMode(QLineEdit::Password);
        if (row.first == QLatin1String("pass"))
            edit->setPlaceholderText(QStringLiteral("not set"));
        if (row.first == QLatin1String("dns"))
            edit->setPlaceholderText(QStringLiteral("Not in the setup utility"));
        if (row.first == QLatin1String("mode"))
            edit->setPlaceholderText(QStringLiteral("Not in the setup utility"));
        values.insert(row.first, edit);
        const bool canWrite = row.first == QLatin1String("dhcp") || row.first == QLatin1String("aip")
                              || row.first == QLatin1String("anmask") || row.first == QLatin1String("agw")
                              || row.first == QLatin1String("http") || row.first == QLatin1String("udp")
                              || row.first == QLatin1String("serialPort");
        if (canWrite) {
            edit->setReadOnly(false);
            edit->setPlaceholderText(QStringLiteral("Not read"));
            auto *rowWidget = new QWidget;
            auto *rowLay = new QHBoxLayout(rowWidget);
            rowLay->setContentsMargins(0, 0, 0, 0);
            auto *write = new QPushButton(QStringLiteral("Write"));
            write->setObjectName(QStringLiteral("write-net-") + row.first);
            rowLay->addWidget(edit, 1);
            rowLay->addWidget(write);
            networkWrites.append(edit);
            networkWrites.append(write);
            const QString key = row.first;
            const QString setting = row.second;
            connect(write, &QPushButton::clicked, this, [this, key, setting, edit]() {
                emit networkWriteRequested(key, edit->text(), setting);
            });
            form->addRow(row.second, rowWidget);
        } else {
            form->addRow(row.second, edit);
        }
    }
    gatewayNote = new QLabel;
    gatewayNote->setObjectName(QStringLiteral("gatewayNote"));
    gatewayNote->setWordWrap(true);
    form->addRow(QString(), gatewayNote);
    auto *noReset = new QLabel(QStringLiteral(
        "Reset to defaults is not offered. Write stays off until a network read succeeds on this connection."));
    noReset->setWordWrap(true);
    form->addRow(QString(), noReset);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(formHost);

    readNetwork = new QPushButton(QStringLiteral("Read network"));
    readNetwork->setObjectName(QStringLiteral("readNetwork"));
    networkLinkNote = new QLabel(QStringLiteral(
        "Network settings can only be read or changed while connected by USB or serial."));
    networkLinkNote->setObjectName(QStringLiteral("networkLinkNote"));
    networkLinkNote->setWordWrap(true);
    auto *save = new QPushButton(QStringLiteral("Save a copy"));
    auto *guide = new QPushButton(QStringLiteral("Guide me"));
    guide->setObjectName(QStringLiteral("openWizard"));
    auto *actions = new QHBoxLayout;
    actions->addWidget(readNetwork);
    actions->addWidget(save);
    actions->addWidget(guide);
    actions->addStretch();

    wizardTitle = new QLabel;
    wizardTitle->setObjectName(QStringLiteral("wizardTitle"));
    wizardBody = new QLabel;
    wizardBody->setObjectName(QStringLiteral("wizardBody"));
    wizardBody->setWordWrap(true);
    wizardBody->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto *back = new QPushButton(QStringLiteral("Back"));
    auto *next = new QPushButton(QStringLiteral("Next"));
    next->setObjectName(QStringLiteral("wizardNext"));
    auto *closeGuide = new QPushButton(QStringLiteral("Close guide"));
    auto *wizardButtons = new QHBoxLayout;
    wizardButtons->addWidget(back);
    wizardButtons->addWidget(next);
    wizardButtons->addStretch();
    wizardButtons->addWidget(closeGuide);
    auto *wizard = new QWidget;
    auto *wizardLayout = new QVBoxLayout(wizard);
    wizardLayout->addWidget(wizardTitle);
    wizardLayout->addWidget(wizardBody, 1);
    wizardLayout->addLayout(wizardButtons);

    auto *formPage = new QWidget;
    auto *formLayout = new QVBoxLayout(formPage);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->addLayout(actions);
    formLayout->addWidget(scroll, 1);

    stack = new QStackedWidget;
    stack->setObjectName(QStringLiteral("setupStack"));
    stack->addWidget(formPage);
    stack->addWidget(wizard);

    layout->addWidget(networkLinkNote);
    layout->addWidget(banner);
    layout->addWidget(stack, 1);

    connect(readNetwork, &QPushButton::clicked, this, [this]() {
        if (!driver) {
            setStatus(QStringLiteral("Connect to a rotor before reading the network module."));
            return;
        }
        if (readNetwork->text() == QStringLiteral("Cancel")) {
            driver->cancelNetwork();
            return;
        }
        if (ipOnlyLink()) {
            setStatus(QStringLiteral("Needs a USB or serial connection; reading over IP drops the link."));
            return;
        }
        setStatus(QStringLiteral("Reading the network module."));
        driver->readNetwork();
    });
    connect(save, &QPushButton::clicked, this, [this]() { emit exportRequested(); });
    connect(guide, &QPushButton::clicked, this, [this]() { showWizard(); });
    connect(closeGuide, &QPushButton::clicked, this, [this]() { showNetwork(); });
    connect(back, &QPushButton::clicked, this, [this]() { showWizardStep(wizardStep - 1); });
    connect(next, &QPushButton::clicked, this, [this]() { showWizardStep(wizardStep + 1); });
    refreshNetworkRead();
}

void SetupPanel::setDriver(Rt21Driver *next)
{
    if (wired && driver)
        disconnect(driver, nullptr, this, nullptr);
    driver = next;
    wired = true;
    if (!driver)
        return;
    connect(driver, &Rt21Driver::linkChanged, this, [this](Rt21Driver::Link, const QString &) { refreshNetworkRead(); });
    refreshNetworkRead();
    connect(driver, &Rt21Driver::configField, this, [this](QChar id, const QString &, bool numeric, int value) {
        if (id != QLatin1Char('T') || !numeric || !controllerSpeed)
            return;
        const bool fast = (value & 256) != 0;
        controllerSpeed->setText(fast ? QStringLiteral("Controller speed: 9600 8N1, from the advanced-options bit.")
                                       : QStringLiteral("Controller speed: 4800 8N1. The 9600 bit is off."));
    });
    connect(driver, &Rt21Driver::networkField, this, [this](const QString &key, const QString &value) {
        remember(key, value);
    });
    connect(driver, &Rt21Driver::networkStatus, this, [this](const QString &text) {
        setStatus(text);
        if (readNetwork && !ipOnlyLink()) {
            readNetwork->setText(QStringLiteral("Cancel"));
            readNetwork->setEnabled(true);
        }
        refreshNetworkRead();
    });
    connect(driver, &Rt21Driver::networkFinished, this, [this](bool ok, const QString &message) {
        if (readNetwork)
            readNetwork->setText(QStringLiteral("Read network"));
        refreshNetworkRead();
        if (!message.isEmpty())
            setStatus(message);
        else if (!ok)
            setStatus(QStringLiteral("Network module didn't answer after 8 tries"));
        else if (rawNetwork.value(QStringLiteral("id")).isEmpty())
            setStatus(QStringLiteral("The module didn't return a name. USB is the reliable way to read it."));
        else
            setStatus(QStringLiteral("Network module answered. Nothing was written."));
    });
    connect(driver, &Rt21Driver::writeFinished, this, [this](bool, const QString &) {
        if (readNetwork && readNetwork->text() == QStringLiteral("Cancel"))
            readNetwork->setText(QStringLiteral("Read network"));
        refreshNetworkRead();
    });
}

void SetupPanel::setEndpoint(const RotorEndpoint &next)
{
    endpoint = next;
    if (!savedLink)
        return;
    if (!endpoint.configured()) {
        savedLink->setText(QStringLiteral("No rotor address is saved."));
        refreshNetworkRead();
        refreshLocalPorts();
        return;
    }
    if (endpoint.transport == RotorEndpoint::Transport::Serial) {
        savedLink->setText(QStringLiteral("Meridian is using %1 at %2.").arg(endpoint.serialPort).arg(endpoint.baud));
    } else {
        savedLink->setText(QStringLiteral("Meridian is using %1 port %2.").arg(endpoint.host).arg(endpoint.tcpPort));
    }
    refreshNetworkRead();
    refreshLocalPorts();
}

void SetupPanel::refreshLocalPorts()
{
    if (localPorts)
        localPorts->setText(localSerialPorts());
}

bool SetupPanel::ipOnlyLink() const
{
    if (driver && driver->link() != Rt21Driver::Link::Off) {
        const RotorEndpoint live = driver->linkEndpoint();
        return live.transport == RotorEndpoint::Transport::Tcp && live.configured();
    }
    return endpoint.transport == RotorEndpoint::Transport::Tcp && endpoint.configured();
}

void SetupPanel::refreshNetworkRead()
{
    const bool ip = ipOnlyLink();
    const bool readOk = driver && driver->networkReadOk();
    const bool reading = readNetwork && readNetwork->text() == QStringLiteral("Cancel");
    if (readNetwork && !reading)
        readNetwork->setEnabled(!ip);
    for (QLineEdit *edit : values)
        edit->setEnabled(!ip);
    for (QWidget *widget : networkWrites) {
        if (qobject_cast<QPushButton *>(widget))
            widget->setEnabled(!ip && readOk && !reading);
        else
            widget->setEnabled(!ip);
    }
    if (!networkLinkNote)
        return;
    networkLinkNote->setVisible(true);
    networkLinkNote->setStyleSheet(ip ? QString() : QStringLiteral("color: #8ea099;"));
}

QMap<QString, QString> SetupPanel::networkValues() const
{
    return rawNetwork;
}

void SetupPanel::showNetwork()
{
    if (stack)
        stack->setCurrentIndex(0);
}

void SetupPanel::showWizard()
{
    wizardStep = 0;
    if (stack)
        stack->setCurrentIndex(1);
    showWizardStep(0);
}

void SetupPanel::showWizardStep(int step)
{
    wizardStep = std::clamp(step, 0, 4);
    if (stack)
        stack->setCurrentIndex(1);
    if (wizardStep == 0) {
        wizardTitle->setText(QStringLiteral("What the network settings do"));
        wizardBody->setText(QStringLiteral(
            "DHCP lets the router pick the rotor's address. That address can change, and Meridian then has to be pointed at the new one.\n\n"
            "A static address stays put. Use one the router's automatic pool will not give to a phone or a laptop, or reserve it in the router if the router allows that.\n\n"
            "The serial TCP port is what Meridian and other programs use. The HTTP port is only for a browser. DNS and an AP-or-client switch are not in the Green Heron utility.\n\n"
            "Do this over USB when you can. Changing the address on an IP connection drops that connection."));
        return;
    }
    if (wizardStep == 1) {
        const QList<int> busy = busyTcpPorts();
        suggestedPort = Rt21::recommendTcpPort(busy);
        wizardTitle->setText(QStringLiteral("A free TCP port on this computer"));
        QString text;
        if (suggestedPort == 6555) {
            text = QStringLiteral("6555 is free on this computer. That is the port the setup utility writes when someone resets the module. It is not printed as a factory default in the manual.");
        } else if (suggestedPort > 0) {
            text = QStringLiteral("6555 is already in use on this computer. %1 is free. This check does not see every other device on the network.").arg(suggestedPort);
        } else {
            text = QStringLiteral("Every candidate port is in use on this computer.");
        }
        if (!busy.isEmpty())
            text += QStringLiteral("\n\nIn use here: %1.").arg([&] {
                QStringList names;
                for (int port : busy)
                    names.append(QString::number(port));
                return names.join(QStringLiteral(", "));
            }());
        wizardBody->setText(text);
        return;
    }
    if (wizardStep == 2) {
        const LocalNet net = thisComputer();
        const Rt21::StaticAdvice advice = Rt21::adviseStaticAddress(net.ip, net.prefix, net.gateway, {});
        suggestedAddress = advice.address;
        wizardTitle->setText(QStringLiteral("An address on this network"));
        wizardBody->setText(advice.explanation + QStringLiteral(
            "\n\nThis is only a suggestion. It is not written into the connection box, and it is not sent to the rotor."));
        return;
    }
    if (wizardStep == 3) {
        wizardTitle->setText(QStringLiteral("Which connection to use"));
        const QString ports = localSerialPorts();
        QString text;
        if (endpoint.transport == RotorEndpoint::Transport::Serial && endpoint.configured())
            text = QStringLiteral("You are on %1. A network change from here does not drop Meridian's connection. That is the better way.\n\n").arg(endpoint.serialPort);
        else if (endpoint.transport == RotorEndpoint::Transport::Tcp && endpoint.configured())
            text = QStringLiteral("You are connected to %1 port %2. Network settings stay greyed out on an IP link, because changing them can drop the connection.\n\n").arg(endpoint.host).arg(endpoint.tcpPort);
        else
            text = QStringLiteral("No rotor connection is saved yet.\n\n");
        text += ports;
        text += QStringLiteral("\n\nPrefer USB or a serial port for the change when one is available.");
        wizardBody->setText(text);
        return;
    }
    wizardTitle->setText(QStringLiteral("Nothing will be sent"));
    QString summary = QStringLiteral("Suggested serial TCP port: %1.\nSuggested static address: %2.\n\nThese suggestions are not written into the fields and are not sent. Use a Write button on a serial connection to store one setting.")
                          .arg(suggestedPort > 0 ? QString::number(suggestedPort) : QStringLiteral("none"))
                          .arg(suggestedAddress.isEmpty() ? QStringLiteral("none") : suggestedAddress);
    wizardBody->setText(summary);
    setStatus(QStringLiteral("Guide only. Nothing was written."));
}

void SetupPanel::remember(const QString &key, const QString &value)
{
    QString stored = value;
    if (key == QLatin1String("pass"))
        stored = value.trimmed().isEmpty() ? QStringLiteral("not set") : QStringLiteral("set");
    rawNetwork.insert(key, stored);
    QLineEdit *edit = values.value(key);
    if (edit) {
        if (key == QLatin1String("dhcp"))
            edit->setText(Rt21::addressModeText(value));
        else if (key == QLatin1String("security")) {
            bool ok = false;
            const int code = value.toInt(&ok);
            edit->setText(ok ? QStringLiteral("%1 — %2").arg(value, Rt21::wifiSecurityName(code)) : value);
        } else if (key == QLatin1String("link")) {
            if (value == QLatin1String("1"))
                edit->setText(QStringLiteral("Link OK"));
            else if (value == QLatin1String("0"))
                edit->setText(QStringLiteral("Not linked"));
            else
                edit->setText(value);
        } else if (key == QLatin1String("hwtype"))
            edit->setText(value.isEmpty() ? value : QStringLiteral("Type %1").arg(value));
        else if (key == QLatin1String("key"))
            edit->setText(value);
        else
            edit->setText(stored);
    }
    if (key == QLatin1String("serialPort") && moduleSerial)
        moduleSerial->setText(QStringLiteral("Module serial TCP port: %1").arg(value.isEmpty() ? QStringLiteral("no reply") : value));
    if (gatewayNote) {
        const QString ip = rawNetwork.value(QStringLiteral("aip"));
        const QString mask = rawNetwork.value(QStringLiteral("anmask"));
        const QString gate = rawNetwork.value(QStringLiteral("agw"));
        gatewayNote->setText(Rt21::gatewaySubnetNote(ip, mask.isEmpty() ? rawNetwork.value(QStringLiteral("nmask")) : mask, gate));
    }
}

void SetupPanel::setStatus(const QString &text)
{
    emit statusMessage(text);
}
