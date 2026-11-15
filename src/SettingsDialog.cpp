/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SettingsDialog.h"

#include "AboutDialog.h"
#include "SetupPanel.h"
#include "core/Coordinates.h"
#include "rotor/Rt21Driver.h"
#include "rotor/Rt21Setup.h"
#include "rotor/TrafficLog.h"
#include "rotor/UserFiles.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace {

struct FieldDef {
    QChar id;
    const char *label;
    const char *hint;
    bool writable;
    int minValue;
    int maxValue;
};

const FieldDef kFields[] = {
    {'W', "Screen saver", "Minutes. 0 is Off. Steps are 1, 2, 5, 10, 20, 30, 40, 50, 60. Not written yet.", true, 0, 60},
    {'O', "Brightness", "Manual default 8. OLED modules have no brightness control; the saver blanks them.", true, 0, 15},
    {'D', "Offset", "CCW endpoint in degrees. 180 is north-center, the manual default.", true, 0, 360},
    {'F', "Min speed", "Manual scale is 0 to 10. 4.14.g1 at this station returned 125, so the box allows that.", true, 0, 255},
    {'G', "Max speed", "Manual default 10.", true, 0, 10},
    {'N', "Ramp", "Manual default 3. On relay systems a higher value stops the motor earlier.", true, 0, 10},
    {'E', "Delays", "Manual field E, default 3. Brake and reversal delay are not separate commands.", true, 0, 99},
    {'H', "CCW limit", "Manual default 180.", true, 0, 540},
    {'I', "CW limit", "Manual default 180.", true, 0, 540},
    {'L', "Cal range", "Manual default 360.", true, 0, 540},
    {'K', "Pulse divider", "Manual default 3960 (4015 on some firmware).", true, 1, 20000},
    {'P', "Fail timeout", "Rotor-fail timeout. Manual default 4.", true, 0, 60},
    {'U', "Overtravel", "Degrees of overtravel allowed. Manual default 90.", true, 0, 180},
    {'V', "Counter-rotate delay", "Manual default 10.", true, 0, 999},
    {'Y', "Knob time", "Manual default 40.", true, 0, 999},
    {'A', "CCW cal", "Raw CCW calibration constant. Manual default 0.", true, -99999, 99999},
    {'B', "CW cal", "Raw CW calibration constant. Manual default 950.", true, -99999, 99999},
    {'R', "Alt offset 1", "361 means unused. Manual default 361.", true, 0, 999},
    {'S', "Alt offset 2", "Manual default 361.", true, 0, 999},
    {'T', "Advanced options", "Bitfield from Appendix F. The boxes below edit the documented bits.", true, 0, 65535},
    {'J', "Option", "Rotor type. The utility's codes are 0 pot through 12 sub-lunar. Not written until checked on your rotor.", false, 0, 0},
    {'M', "Mode", "0 normal, 1 master/slave, 2 master counter-rotate, 3 slave counter-rotate, 4 offset, 5 debug.", false, 0, 0},
    {'Q', "Security", "0 user, 1 admin, from the utility. Not written yet.", false, 0, 0},
    {'X', "Limit switch", "Read from the controller. Not written from here.", false, 0, 0},
    {'C', "Reserved", "The 2014 manual reserves C. The utility does not show it. Read only.", false, 0, 0},
    {'Z', "Multiplier", "Utility 4.B12. The 2014 manual said Z was unused. Not written yet.", false, 0, 0},
    {'[', "Magnetic variation", "4.B12, degrees, -60 to +60. For pre-release 4.d14. Not written yet.", false, 0, 0},
};

const char *kBits[] = {
    "Freeze display", "Fast brake release", "Auto go-back", "Elevation mode",
    "Auto dim", "Analog heading out", "EME mode", "Point-and-shoot shortest route",
    "Port is 9600", "2-bit encoder", "Demo", "Allow cal to CCW limit",
};

} // namespace

SettingsDialog::SettingsDialog(const AppConfig &config, const ZipDatabase *zipDb, Rt21Driver *rotor,
                               QWidget *parent)
    : QDialog(parent)
    , draft(config)
    , zips(zipDb)
    , driver(rotor)
{
    setWindowTitle(QStringLiteral("Settings"));
    setMinimumSize(520, 640);
    resize(580, 720);

    auto *layout = new QVBoxLayout(this);
    tabs = new QTabWidget;
    setupTabs = new QTabWidget;
    setupTabs->setObjectName(QStringLiteral("setupTabs"));
    setupTabs->setDocumentMode(true);
    layout->addWidget(tabs, 1);
    buildStation();
    buildRotors();
    buildController();
    setupPanel = new SetupPanel(this);
    setupTabs->addTab(setupPanel->portsPage(), QStringLiteral("Ports"));
    setupTabs->addTab(setupPanel->networkPage(), QStringLiteral("Network"));
    tabs->addTab(setupTabs, QStringLiteral("Setup"));
    connect(setupPanel, &SetupPanel::statusMessage, this, [this](const QString &text) { setStatus(text); });
    connect(setupPanel, &SetupPanel::exportRequested, this, [this]() { exportSetup(); });
    statusLabel = new QLabel;
    statusLabel->setObjectName(QStringLiteral("setupStatus"));
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto *about = new QPushButton(QStringLiteral("About Meridian"));
    about->setFlat(true);
    connect(about, &QPushButton::clicked, this, [this]() {
        AboutDialog dialog(this);
        dialog.exec();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { commitDraft(true); });
    auto *row = new QHBoxLayout;
    row->addWidget(about);
    row->addStretch();
    row->addWidget(buttons);
    layout->addLayout(row);

    setDriver(driver);
    refreshLocation();
}

void SettingsDialog::setDriver(Rt21Driver *next)
{
    if (driverWired && driver == next)
        return;
    if (driverWired && driver)
        disconnect(driver, nullptr, this, nullptr);
    driver = next;
    driverWired = true;
    if (setupPanel)
        setupPanel->setDriver(driver);
    if (!driver)
        return;
    connect(driver, &Rt21Driver::configField, this,
                [this](QChar id, const QString &raw, bool numeric, int value) {
                    eeRaw.insert(QString(id), raw);
                    QString shown = raw;
                    if (numeric && id == QLatin1Char('J'))
                        shown = QStringLiteral("%1 — %2").arg(value).arg(Rt21::rotorTypeName(value));
                    else if (numeric && id == QLatin1Char('M'))
                        shown = QStringLiteral("%1 — %2").arg(value).arg(Rt21::modeName(value));
                    else if (numeric && id == QLatin1Char('Q'))
                        shown = QStringLiteral("%1 — %2").arg(value).arg(Rt21::securityName(value));
                    else if (numeric && id == QLatin1Char('W') && screenSaver)
                        screenSaver->setText(value == 0 ? QStringLiteral("Screen saver: off")
                                                        : QStringLiteral("Screen saver: %1 min").arg(value));
                    else if (numeric && id == QLatin1Char('Z') && multiplier)
                        multiplier->setText(QStringLiteral("Multiplier: %1").arg(value));
                    for (FieldRow &field : fields) {
                        if (field.id != id)
                            continue;
                        field.raw->setText(shown);
                        field.numeric = numeric;
                        if (numeric && field.spin) {
#if MERIDIAN_ALPHA_READONLY
                            field.spin->setReadOnly(true);
                            field.spin->setEnabled(false);
#else
                            field.spin->setEnabled(field.writable);
#endif
                            const QSignalBlocker blocker(field.spin);
                            field.spin->setValue(std::clamp(value, field.spin->minimum(), field.spin->maximum()));
                            field.original = field.spin->value();
                            if (field.id == QLatin1Char('T')) {
                                fillingBits = true;
                                for (int bit = 0; bit < 12; ++bit) {
#if !MERIDIAN_ALPHA_READONLY
                                    bits[bit]->setEnabled(true);
#endif
                                    bits[bit]->setChecked(field.spin->value() & (1 << bit));
                                }
                                fillingBits = false;
                            }
                        }
                    }
                    refreshDisplayCallout();
                });
        connect(driver, &Rt21Driver::configNote, this, [this](const QString &text) {
            if (text.startsWith(QStringLiteral("Version ")))
                versionReport = text.mid(QStringLiteral("Version ").size());
            else if (text.startsWith(QStringLiteral("Pot A/D ")))
                potReport = text.mid(QStringLiteral("Pot A/D ").size());
            else if (text.startsWith(QStringLiteral("Last reset ")))
                resetReport = text.mid(QStringLiteral("Last reset ").size());
            QStringList parts;
            if (!versionReport.isEmpty())
                parts.append(QStringLiteral("Version %1").arg(versionReport));
            if (!potReport.isEmpty())
                parts.append(QStringLiteral("Pot A/D %1").arg(potReport));
            if (!resetReport.isEmpty())
                parts.append(QStringLiteral("Last reset %1").arg(resetReport));
            versionLabel->setText(parts.join(QStringLiteral("   ·   ")));
            const QString notice = Rt21::firmwareNotice(versionReport);
            if (!notice.isEmpty())
                setStatus(notice);
            refreshDisplayCallout();
        });
        connect(driver, &Rt21Driver::configFinished, this, [this](bool ok, const QString &message) {
            if (!ok && !message.isEmpty())
                setStatus(message);
            else if (ok)
                setStatus(QStringLiteral("Controller settings read. Nothing was written."));
        });
        connect(driver, &Rt21Driver::writeFinished, this, [this](bool ok, const QString &message) {
            const QString fallback = ok ? QStringLiteral("Read back the same value.")
                                         : QStringLiteral("The write did not stick.");
            setStatus(message.isEmpty() ? fallback : message);
            if (ok && driver)
                driver->readConfiguration();
        });
}

void SettingsDialog::setStatus(const QString &text)
{
    if (statusLabel)
        statusLabel->setText(text);
    if (driver && !text.isEmpty())
        driver->postNote(text);
}

void SettingsDialog::refreshDisplayCallout()
{
    if (!displayCallout)
        return;
    QString saver = QStringLiteral("Screen saver has not been read yet.");
    for (const FieldRow &field : fields) {
        if (field.id != QLatin1Char('W') || !field.raw)
            continue;
        const QString raw = field.raw->text();
        if (raw == QLatin1String("…") || raw == QLatin1String("—"))
            break;
        bool numeric = false;
        const int minutes = raw.toInt(&numeric);
        if (!numeric)
            saver = QStringLiteral("Screen saver: %1.").arg(raw);
        else if (minutes == 0)
            saver = QStringLiteral("Screen saver is off. The heading stays on the display.");
        else
            saver = QStringLiteral("Screen saver is %1 minutes.").arg(minutes);
        break;
    }
    displayCallout->setText(saver + QLatin1Char(' ') + Rt21::screenSaverRecommendation());
}

void SettingsDialog::exportSetup()
{
    QMap<QString, QString> controller = eeRaw;
    if (!versionReport.isEmpty())
        controller.insert(QStringLiteral("version"), versionReport);
    if (!potReport.isEmpty())
        controller.insert(QStringLiteral("pot"), potReport);
    if (!resetReport.isEmpty())
        controller.insert(QStringLiteral("reset"), resetReport);
    QString error;
    const QMap<QString, QString> network = setupPanel ? setupPanel->networkValues() : QMap<QString, QString>();
    const QString path = Rt21::saveSetupBackup(controller, network, &error);
    if (path.isEmpty())
        setStatus(error.isEmpty() ? QStringLiteral("Couldn't write the setup copy.") : error);
    else
        setStatus(QStringLiteral("Saved a copy at %1").arg(path));
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    commitDraft(false);
    QDialog::closeEvent(event);
}

bool SettingsDialog::commitDraft(bool interactive)
{
    if (committing)
        return true;
    committing = true;
    if (rotorIndex >= 0)
        storeRotor(rotorIndex);
    QString error;
    if (!resolveLocation(homeEdit->text(), zips, &error)) {
        if (interactive) {
            QMessageBox::warning(this, QStringLiteral("Location"), error);
            tabs->setCurrentIndex(0);
            committing = false;
            return false;
        }
    } else {
        draft.homeText = homeEdit->text().trimmed();
    }
    if (trafficBox)
        draft.trafficLog = trafficBox->isChecked();
    if (sunBox)
        draft.showSun = sunBox->isChecked();
    if (idleSpin)
        draft.idlePollSeconds = idleSpin->value();
    emit configSaved(draft);
    committing = false;
    return true;
}

void SettingsDialog::setConfig(const AppConfig &config)
{
    draft = config;
    homeEdit->setText(draft.homeText);
    if (trafficBox)
        trafficBox->setChecked(draft.trafficLog);
    if (sunBox)
        sunBox->setChecked(draft.showSun);
    if (idleSpin)
        idleSpin->setValue(draft.idlePollSeconds);
    rotorList->clear();
    for (const RotorEndpoint &rotor : draft.rotors)
        rotorList->addItem(rotor.name.isEmpty() ? QStringLiteral("RT-21") : rotor.name);
    rotorIndex = -1;
    if (!draft.rotors.isEmpty())
        rotorList->setCurrentRow(std::clamp(draft.active, 0, int(draft.rotors.size()) - 1));
    refreshLocation();
}

void SettingsDialog::showPage(int index)
{
    tabs->setCurrentIndex(index);
}

void SettingsDialog::buildStation()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *intro = new QLabel(QStringLiteral(
        "Grid square, US ZIP code, or coordinates. "
        "Decimal degrees, N/S/E/W, degrees-minutes-seconds, and degrees with decimal minutes all work."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("hint"));
    homeEdit = new QLineEdit(draft.homeText);
    homeEdit->setObjectName(QStringLiteral("homeEdit"));
    homeEdit->setPlaceholderText(QStringLiteral("EM26   or   72714   or   36°28.2'N 94°13.8'W"));
    summary = new QLabel;
    summary->setObjectName(QStringLiteral("summaryLabel"));
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail = new QLabel;
    detail->setObjectName(QStringLiteral("detailLabel"));
    layout->addWidget(intro);
    layout->addWidget(homeEdit);
    layout->addWidget(summary);
    layout->addWidget(detail);
    trafficBox = new QCheckBox(QStringLiteral("Record rotor traffic"));
    trafficBox->setChecked(draft.trafficLog);
    auto *filePath = new QLabel(UserFiles::settingsPath());
    filePath->setObjectName(QStringLiteral("settingsPath"));
    filePath->setWordWrap(true);
    filePath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *openFolder = new QPushButton(QStringLiteral("Open folder"));
    openFolder->setObjectName(QStringLiteral("openSettingsFolder"));
    openFolder->setFlat(true);
    openFolder->setCursor(Qt::PointingHandCursor);
    connect(openFolder, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(UserFiles::root()));
    });
    auto *where = new QLabel(QStringLiteral(
        "Settings, logs, and controller backups are in this folder. Copy meridian.ini to back it up or move it."));
    where->setWordWrap(true);
    where->setObjectName(QStringLiteral("hint"));
    layout->addSpacing(16);
    layout->addWidget(trafficBox);
    layout->addWidget(where);
    layout->addWidget(filePath);
    layout->addWidget(openFolder, 0, Qt::AlignLeft);
    sunBox = new QCheckBox(QStringLiteral("Show sun & day/night"));
    sunBox->setChecked(draft.showSun);
    sunBox->setObjectName(QStringLiteral("showSun"));
    idleSpin = new QSpinBox;
    idleSpin->setRange(30, 60);
    idleSpin->setValue(std::clamp(draft.idlePollSeconds, 30, 60));
    idleSpin->setSuffix(QStringLiteral(" s"));
    idleSpin->setObjectName(QStringLiteral("idlePoll"));
    auto *idleLabel = new QLabel(QStringLiteral("Idle poll"));
    auto *idleRow = new QHBoxLayout;
    idleRow->addWidget(idleLabel);
    idleRow->addWidget(idleSpin);
    idleRow->addStretch();
    layout->addSpacing(8);
    layout->addWidget(sunBox);
    layout->addLayout(idleRow);
    layout->addStretch();
    connect(homeEdit, &QLineEdit::textChanged, this, [this]() { refreshLocation(); });
    tabs->addTab(page, QStringLiteral("Station"));
}

void SettingsDialog::refreshLocation()
{
    QString error;
    if (const auto loc = resolveLocation(homeEdit->text(), zips, &error)) {
        summary->setText(loc->summary());
        detail->setText(loc->detail());
        summary->setStyleSheet(QStringLiteral("color: #d5e2db; font-size: 15px;"));
        detail->setStyleSheet(QStringLiteral("color: #8ea099;"));
    } else {
        summary->setText(error);
        detail->clear();
        summary->setStyleSheet(QStringLiteral("color: #e09a86;"));
    }
}

void SettingsDialog::buildRotors()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    rotorList = new QListWidget;
    rotorList->setMaximumHeight(140);
    auto *add = new QPushButton(QStringLiteral("Add"));
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();

    nameEdit = new QLineEdit;
    transport = new QComboBox;
    transport->setObjectName(QStringLiteral("transportPick"));
    transport->addItems({QStringLiteral("Choose connection"), QStringLiteral("IP / TCP"), QStringLiteral("Serial")});
    hostEdit = new QLineEdit;
    hostEdit->setPlaceholderText(QStringLiteral("Host name or IP"));
    portSpin = new QSpinBox;
    portSpin->setRange(0, 65535);
    portSpin->setSpecialValueText(QStringLiteral(" "));
    portSpin->setValue(0);
    portCombo = new QComboBox;
    portCombo->setEditable(true);
    baudCombo = new QComboBox;
    baudCombo->addItems({QStringLiteral("4800"), QStringLiteral("9600")});

    auto *tcp = new QWidget;
    auto *tcpForm = new QFormLayout(tcp);
    tcpForm->addRow(QStringLiteral("Host"), hostEdit);
    tcpForm->addRow(QStringLiteral("Port"), portSpin);
    auto *serial = new QWidget;
    auto *serialForm = new QFormLayout(serial);
    serialForm->addRow(QStringLiteral("Port"), portCombo);
    serialForm->addRow(QStringLiteral("Baud"), baudCombo);
    auto *note = new QLabel(QStringLiteral("8 data bits, no parity, 1 stop bit. The RT-21 default is 4800."));
    note->setWordWrap(true);
    serialForm->addRow(QString(), note);
    auto *blank = new QLabel(QStringLiteral("Pick IP or serial. Nothing is filled in until you do."));
    blank->setWordWrap(true);
    blank->setObjectName(QStringLiteral("hint"));
    transportPages = new QStackedWidget;
    transportPages->addWidget(blank);
    transportPages->addWidget(tcp);
    transportPages->addWidget(serial);

    findButton = new QPushButton(QStringLiteral("Find RT-21"));
    findButton->setObjectName(QStringLiteral("findRt21"));
    findButton->setCursor(Qt::PointingHandCursor);
    findNote = new QLabel;
    findNote->setObjectName(QStringLiteral("findNote"));
    findNote->setWordWrap(true);
    auto *resultsHost = new QWidget;
    findResults = new QVBoxLayout(resultsHost);
    findResults->setContentsMargins(0, 0, 0, 0);
    networkButton = new QPushButton(QStringLiteral("Scan the network"));
    networkButton->setObjectName(QStringLiteral("scanNetwork"));
    networkButton->setFlat(true);
    networkButton->setCursor(Qt::PointingHandCursor);
    customHostEdit = new QLineEdit;
    customHostEdit->setObjectName(QStringLiteral("customHost"));
    customHostEdit->setPlaceholderText(QStringLiteral("Host (optional)"));
    customPortEdit = new QLineEdit;
    customPortEdit->setObjectName(QStringLiteral("customPort"));
    customPortEdit->setPlaceholderText(QStringLiteral("Custom port"));
    auto *customRow = new QHBoxLayout;
    customRow->addWidget(customHostEdit);
    customRow->addWidget(customPortEdit);
    layout->addWidget(findButton);
    layout->addWidget(findNote);
    layout->addWidget(resultsHost);
    layout->addWidget(networkButton);
    layout->addLayout(customRow);
    connect(findButton, &QPushButton::clicked, this, [this]() { startFind(false); });
    connect(networkButton, &QPushButton::clicked, this, [this]() { startFind(true); });

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Name"), nameEdit);
    form->addRow(QStringLiteral("Connection"), transport);
    layout->addWidget(rotorList);
    layout->addLayout(buttons);
    layout->addLayout(form);
    layout->addWidget(transportPages);
    layout->addStretch();
    tabs->addTab(page, QStringLiteral("Rotors"));

    // The combo only swaps the form. Nothing connects, and nothing is saved,
    // until Save or the dialog closes.
    connect(transport, qOverload<int>(&QComboBox::currentIndexChanged), transportPages,
            &QStackedWidget::setCurrentIndex);
    connect(add, &QPushButton::clicked, this, [this]() {
        if (rotorIndex >= 0)
            storeRotor(rotorIndex);
        RotorEndpoint rotor;
        rotor.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        rotor.name = QStringLiteral("RT-21 %1").arg(draft.rotors.size() + 1);
        draft.rotors.append(rotor);
        rotorList->addItem(rotor.name);
        rotorList->setCurrentRow(rotorList->count() - 1);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const int row = rotorList->currentRow();
        if (row < 0 || draft.rotors.isEmpty())
            return;
        rotorIndex = -1;
        draft.rotors.removeAt(row);
        delete rotorList->takeItem(row);
        if (draft.active >= draft.rotors.size())
            draft.active = std::max(0, int(draft.rotors.size()) - 1);
    });
    connect(rotorList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (rotorIndex >= 0 && rotorIndex != row)
            storeRotor(rotorIndex);
        rotorIndex = -1;
        if (row >= 0)
            loadRotor(row);
    });
    connect(nameEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (auto *item = rotorList->currentItem())
            item->setText(text.trimmed().isEmpty() ? QStringLiteral("RT-21") : text.trimmed());
    });

    auto *ports = new QTimer(this);
    ports->setInterval(1500);
    connect(ports, &QTimer::timeout, this, [this]() {
        if (isVisible() && transport->currentIndex() == 2)
            refreshPorts();
    });
    ports->start();
    refreshPorts();
    setConfig(draft);
}

void SettingsDialog::loadRotor(int index)
{
    if (index < 0 || index >= draft.rotors.size())
        return;
    rotorIndex = index;
    const RotorEndpoint &rotor = draft.rotors.at(index);
    nameEdit->setText(rotor.name);
    int transportIndex = 0;
    if (rotor.transport == RotorEndpoint::Transport::Tcp)
        transportIndex = 1;
    else if (rotor.transport == RotorEndpoint::Transport::Serial)
        transportIndex = 2;
    transport->setCurrentIndex(transportIndex);
    hostEdit->setText(rotor.host);
    portSpin->setValue(rotor.tcpPort);
    if (!rotor.serialPort.isEmpty() && portCombo->findText(rotor.serialPort) < 0)
        portCombo->addItem(rotor.serialPort);
    portCombo->setCurrentText(rotor.serialPort);
    baudCombo->setCurrentText(QString::number(rotor.baud == 9600 ? 9600 : 4800));
    if (setupPanel)
        setupPanel->setEndpoint(rotor);
    styleFindButton();
}

void SettingsDialog::styleFindButton()
{
    if (!findButton)
        return;
    const bool fresh = rotorIndex < 0 || rotorIndex >= draft.rotors.size() || !draft.rotors.at(rotorIndex).configured();
    findButton->setMinimumHeight(fresh ? 46 : 32);
    findButton->setStyleSheet(fresh
                                  ? QStringLiteral("QPushButton#findRt21 { font-size: 16px; padding: 8px 18px;"
                                                   " background: #3d5a4c; }")
                                  : QStringLiteral("QPushButton#findRt21 { padding: 6px 12px; }"));
}

void SettingsDialog::presentFound(const FoundRotor &found)
{
    if (!findResults)
        return;
    const QString key = found.endpoint.transport == RotorEndpoint::Transport::Tcp
                            ? QStringLiteral("%1:%2").arg(found.endpoint.host).arg(found.endpoint.tcpPort)
                            : found.endpoint.serialPort + QLatin1Char(':') + QString::number(found.endpoint.baud);
    if (foundKeys.contains(key))
        return;
    foundKeys.append(key);
    auto *button = new QPushButton(found.label);
    button->setObjectName(QStringLiteral("findResult"));
    button->setCursor(Qt::PointingHandCursor);
    findResults->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, found]() { applyFound(found); });
    if (findNote)
        findNote->setText(QStringLiteral("Pick the RT-21 to use."));
}

void SettingsDialog::applyFound(const FoundRotor &found)
{
    if (rotorIndex < 0 || rotorIndex >= draft.rotors.size())
        return;
    RotorEndpoint &rotor = draft.rotors[rotorIndex];
    rotor.transport = found.endpoint.transport;
    rotor.host = found.endpoint.host;
    rotor.tcpPort = found.endpoint.tcpPort;
    rotor.serialPort = found.endpoint.serialPort;
    if (found.endpoint.baud > 0)
        rotor.baud = found.endpoint.baud;
    loadRotor(rotorIndex);
    commitDraft(false);
    if (findNote)
        findNote->setText(found.label);
}

void SettingsDialog::startFind(bool networkOnly)
{
    if (findThread)
        return;
    findButton->setEnabled(false);
    networkButton->setEnabled(false);
    if (!networkOnly) {
        while (QLayoutItem *item = findResults->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        foundKeys.clear();
        findNote->setText(QStringLiteral("Checking USB and COM ports…"));
    } else {
        findNote->setText(QStringLiteral("Checking the network…"));
    }
    QList<int> extra;
    bool portOk = false;
    const int customPort = customPortEdit ? customPortEdit->text().trimmed().toInt(&portOk) : 0;
    if (portOk)
        extra.append(customPort);
    for (const RotorEndpoint &rotor : draft.rotors) {
        if (rotor.transport == RotorEndpoint::Transport::Tcp && rotor.tcpPort > 0)
            extra.append(rotor.tcpPort);
    }
    const QString typedHost = customHostEdit ? customHostEdit->text().trimmed() : QString();
    FoundRotor held;
    bool haveHeld = false;
    if (driver && (driver->link() == Rt21Driver::Link::On || driver->link() == Rt21Driver::Link::Moving)
        && driver->heading()) {
        const RotorEndpoint endpoint = driver->linkEndpoint();
        if (endpoint.transport == RotorEndpoint::Transport::Tcp && endpoint.configured()) {
            held.endpoint = endpoint;
            held.heading = *driver->heading();
            held.label = foundRotorLabel(QStringLiteral("%1:%2").arg(endpoint.host).arg(endpoint.tcpPort), QString(), 0,
                                         held.heading);
            haveHeld = true;
        }
    }
    QPointer<SettingsDialog> self(this);
    auto *thread = QThread::create([self, networkOnly, extra, typedHost, held, haveHeld]() {
        auto post = [self](const FoundRotor &hit) {
            if (!self)
                return;
            QMetaObject::invokeMethod(
                self.data(), [self, hit]() {
                    if (self)
                        self->presentFound(hit);
                },
                Qt::QueuedConnection);
        };
        if (!networkOnly)
            Rt21Finder::scanSerialPorts(650, post);
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(), [self]() {
                if (self && self->findNote)
                    self->findNote->setText(QStringLiteral("Checking the network…"));
            },
            Qt::QueuedConnection);
        if (haveHeld)
            post(held);
        QStringList hosts = Rt21Finder::localSubnetHosts();
        if (!typedHost.isEmpty() && !hosts.contains(typedHost))
            hosts.prepend(typedHost);
        Rt21Finder::scanHosts(hosts, networkCandidatePorts(extra), 300, post);
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(), [self]() {
                if (self)
                    self->finishFind({}, false);
            },
            Qt::QueuedConnection);
    });
    findThread = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (findThread == thread)
            findThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
}

void SettingsDialog::finishFind(const QList<FoundRotor> &found, bool network)
{
    Q_UNUSED(network);
    findButton->setEnabled(true);
    networkButton->setEnabled(true);
    for (const FoundRotor &hit : found)
        presentFound(hit);
    const bool any = findResults && findResults->count() > 0;
    if (!any) {
        findNote->setText(QStringLiteral(
            "Nothing found. Check the USB cable or driver, power, and close other apps using the port."));
    } else {
        findNote->setText(QStringLiteral("Pick the RT-21 to use."));
    }
}

void SettingsDialog::storeRotor(int index)
{
    if (index < 0 || index >= draft.rotors.size())
        return;
    RotorEndpoint &rotor = draft.rotors[index];
    rotor.name = nameEdit->text().trimmed().isEmpty() ? QStringLiteral("RT-21") : nameEdit->text().trimmed();
    if (transport->currentIndex() == 1)
        rotor.transport = RotorEndpoint::Transport::Tcp;
    else if (transport->currentIndex() == 2)
        rotor.transport = RotorEndpoint::Transport::Serial;
    else
        rotor.transport = RotorEndpoint::Transport::Unset;
    rotor.host = hostEdit->text().trimmed();
    rotor.tcpPort = portSpin->value();
    QString serialName = portCombo->currentData().toString();
    if (serialName.isEmpty())
        serialName = portCombo->currentText().section(QStringLiteral(" — "), 0, 0).trimmed();
    rotor.serialPort = serialName;
    rotor.baud = baudCombo->currentText().toInt() == 9600 ? 9600 : 4800;
    draft.active = rotorList->currentRow() >= 0 ? rotorList->currentRow() : index;
}

void SettingsDialog::refreshPorts()
{
    const QString selected = portCombo->currentText();
    portCombo->blockSignals(true);
    portCombo->clear();
    portCombo->addItem(QString());
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        const QString label = info.description().isEmpty()
                                  ? info.portName()
                                  : QStringLiteral("%1 — %2").arg(info.portName(), info.description());
        portCombo->addItem(label, info.portName());
    }
    int match = portCombo->findData(selected);
    if (match < 0)
        match = portCombo->findText(selected);
    if (match >= 0)
        portCombo->setCurrentIndex(match);
    else if (!selected.isEmpty())
        portCombo->setCurrentText(selected);
    portCombo->blockSignals(false);
}

void SettingsDialog::buildController()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *banner = new QLabel(
#if MERIDIAN_ALPHA_READONLY
        QStringLiteral(
            "Read-only in this alpha. Reads and backups work. Apply, EEPROM writes, and calibration "
            "commands stay off until they have been checked against a real RT-21.")
#else
        QStringLiteral(
            "Reads use the commands in the RT-21 manual, Appendix F, and the version 4 addendum. "
            "Writes use the documented WXn form with n = 1 (store, do not reboot). "
            "The exact digits on the wire have not been checked against a real controller. "
            "Apply saves a backup first and will not send a factory reset.")
#endif
    );
    banner->setWordWrap(true);
    versionLabel = new QLabel(QStringLiteral("Not read yet."));
    versionLabel->setObjectName(QStringLiteral("versionLabel"));
    versionLabel->setWordWrap(true);
    displayCallout = new QLabel(Rt21::screenSaverRecommendation());
    displayCallout->setObjectName(QStringLiteral("displayCallout"));
    displayCallout->setWordWrap(true);

    auto *read = new QPushButton(QStringLiteral("Read from rotor"));
    auto *backup = new QPushButton(QStringLiteral("Backup…"));
    auto *apply = new QPushButton(QStringLiteral("Apply"));
    auto *actions = new QHBoxLayout;
    actions->addWidget(read);
    actions->addWidget(backup);
    actions->addWidget(apply);
    actions->addStretch();

    auto *formHost = new QWidget;
    auto *form = new QFormLayout(formHost);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const FieldDef &def : kFields) {
        auto *raw = new QLabel(QStringLiteral("—"));
        raw->setMinimumWidth(72);
        FieldRow row;
        row.id = def.id;
        row.writable = def.writable;
        row.raw = raw;
        if (def.writable) {
            auto *spin = new QSpinBox;
            spin->setRange(def.minValue, def.maxValue);
            spin->setEnabled(false);
            spin->setToolTip(QString::fromUtf8(def.hint));
            row.spin = spin;
            auto *pair = new QHBoxLayout;
            auto *wrap = new QWidget;
            pair->addWidget(spin);
            pair->addWidget(raw, 1);
            wrap->setLayout(pair);
            form->addRow(QString::fromUtf8(def.label), wrap);
            if (def.id == QLatin1Char('T')) {
                connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
                    if (fillingBits)
                        return;
                    fillingBits = true;
                    for (int bit = 0; bit < 12; ++bit)
                        bits[bit]->setChecked(value & (1 << bit));
                    fillingBits = false;
                });
            }
        } else {
            form->addRow(QString::fromUtf8(def.label), raw);
        }
        fields.append(row);
    }

    screenSaver = new QLabel(QStringLiteral("Screen saver: not read yet."));
    multiplier = new QLabel(QStringLiteral("Multiplier: not read yet."));
    magnetic = new QLabel(QStringLiteral("Magnetic variation: not read yet."));
    auto *extra = new QLabel(QStringLiteral(
        "Screen saver and the multiplier are utility fields on W and Z. Magnetic variation is R[1 / W[0 in utility 4.B12, range −60 to +60. None of these are written yet."));
    extra->setWordWrap(true);

    auto *bitHost = new QWidget;
    auto *bitForm = new QFormLayout(bitHost);
    for (int bit = 0; bit < 12; ++bit) {
        bits[bit] = new QCheckBox(QString::fromUtf8(kBits[bit]));
        bits[bit]->setEnabled(false);
        bitForm->addRow(bits[bit]);
        connect(bits[bit], &QCheckBox::toggled, this, [this]() {
            if (fillingBits)
                return;
            int value = 0;
            for (int i = 0; i < 12; ++i) {
                if (bits[i]->isChecked())
                    value |= (1 << i);
            }
            for (FieldRow &field : fields) {
                if (field.id == QLatin1Char('T') && field.spin)
                    field.spin->setValue(value);
            }
        });
    }

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *scrollBody = new QWidget;
    auto *scrollLayout = new QVBoxLayout(scrollBody);
    scrollLayout->addWidget(formHost);
    scrollLayout->addWidget(screenSaver);
    scrollLayout->addWidget(multiplier);
    scrollLayout->addWidget(magnetic);
    scrollLayout->addWidget(extra);
    scrollLayout->addWidget(bitHost);
    scrollLayout->addStretch();
    scroll->setWidget(scrollBody);

    auto *calRow = new QHBoxLayout;
    auto *ccw = new QPushButton(QStringLiteral("Cal CCW here"));
    auto *cw = new QPushButton(QStringLiteral("Cal CW here"));
    auto *pulse = new QPushButton(QStringLiteral("Set pulse heading"));
    auto *pulseValue = new QLineEdit;
    pulseValue->setPlaceholderText(QStringLiteral("123.4"));
    pulseValue->setMaximumWidth(90);
    calRow->addWidget(ccw);
    calRow->addWidget(cw);
    calRow->addWidget(pulseValue);
    calRow->addWidget(pulse);
    calRow->addStretch();

    banner->setText(banner->text() + QStringLiteral(
        " Designed and tested with firmware 4.14.g1, which is not the public download. "
        "Posted current firmware is 4.13.2. That line is expected to behave the same, and it has not been tested here."));
    layout->addWidget(banner);
    layout->addWidget(versionLabel);
    layout->addWidget(displayCallout);
    layout->addLayout(actions);
#if MERIDIAN_ALPHA_READONLY
    apply->setEnabled(false);
    apply->setToolTip(QStringLiteral("Read-only in this alpha"));
    ccw->setEnabled(false);
    cw->setEnabled(false);
    pulse->setEnabled(false);
    pulseValue->setReadOnly(true);
#endif
    layout->addWidget(scroll, 1);
    layout->addLayout(calRow);
    setupTabs->addTab(page, QStringLiteral("Controller"));

    connect(read, &QPushButton::clicked, this, [this]() {
        if (!driver)
            return;
        versionLabel->clear();
        versionReport.clear();
        potReport.clear();
        resetReport.clear();
        for (FieldRow &field : fields) {
            field.raw->setText(QStringLiteral("…"));
            field.numeric = false;
        }
        driver->readConfiguration();
    });
    connect(backup, &QPushButton::clicked, this, [this]() { exportSetup(); });
    connect(apply, &QPushButton::clicked, this, [this]() {
        if (!driver)
            return;
        QList<QPair<QChar, int>> changes;
        for (const FieldRow &field : fields) {
            if (!field.writable || !field.numeric || !field.spin)
                continue;
            if (field.spin->value() != field.original)
                changes.append({field.id, field.spin->value()});
        }
        if (changes.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Apply"), QStringLiteral("Nothing changed."));
            return;
        }
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("Apply to the RT-21"),
            QStringLiteral("This sends %1 EEPROM write%2. A backup file is written first. "
                           "The write format is inferred from Appendix F and has not been verified on hardware.")
                .arg(changes.size())
                .arg(changes.size() == 1 ? QString() : QStringLiteral("s")),
            QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Apply)
            return;
        const QString path = QDir(UserFiles::backupDirectory())
                                 .filePath(QStringLiteral("meridian-rt21-%1.json")
                                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
        const QString error = backupConfig(path);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Backup"), error);
            return;
        }
        driver->writeConfiguration(changes);
    });

    const auto confirmCal = [this](const QString &title, const QString &body, auto send) {
        if (!driver)
            return;
        if (QMessageBox::warning(this, title, body, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            != QMessageBox::Yes)
            return;
        send();
    };
    connect(ccw, &QPushButton::clicked, this, [=]() {
        confirmCal(QStringLiteral("Calibrate CCW"),
                   QStringLiteral("Send C01; and set the CCW calibration to the antenna's current position?"),
                   [this]() { driver->sendCalibrate(false); });
    });
    connect(cw, &QPushButton::clicked, this, [=]() {
        confirmCal(QStringLiteral("Calibrate CW"),
                   QStringLiteral("Send C11; and set the CW calibration to the antenna's current position?"),
                   [this]() { driver->sendCalibrate(true); });
    });
    connect(pulse, &QPushButton::clicked, this, [this, pulseValue, confirmCal]() {
        bool ok = false;
        const double value = pulseValue->text().toDouble(&ok);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("Pulse heading"), QStringLiteral("Enter a heading in degrees."));
            return;
        }
        confirmCal(QStringLiteral("Pulse calibration"),
                   QStringLiteral("Send AW1 and set the pulse counter's displayed heading to %1°?")
                       .arg(value, 0, 'f', 1),
                   [this, value]() { driver->sendPulseHeading(value); });
    });
}

QString SettingsDialog::backupConfig(const QString &path) const
{
    QJsonObject fieldsJson;
    for (const FieldRow &field : fields)
        fieldsJson.insert(QString(field.id), field.raw->text());
    QJsonObject root;
    root.insert(QStringLiteral("versionNote"), versionLabel->text());
    root.insert(QStringLiteral("fields"), fieldsJson);
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString tmp = path + QStringLiteral(".tmp");
    QFile file(tmp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Couldn't write %1.").arg(path);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    QFile::remove(path);
    if (!QFile::rename(tmp, path))
        return QStringLiteral("Couldn't write %1.").arg(path);
    return {};
}
