/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SettingsDialog.h"

#include "AboutDialog.h"
#include "core/Coordinates.h"
#include "rotor/Rt21Driver.h"
#include "rotor/TrafficLog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
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
    {'D', "Offset", "CCW endpoint in degrees. 180 is north-center, the manual default.", true, 0, 360},
    {'F', "Min speed", "Manual default 3. 10 is full speed.", true, 0, 10},
    {'G', "Max speed", "Manual default 10.", true, 0, 10},
    {'N', "Ramp", "Manual default 3. On relay systems a higher value stops the motor earlier.", true, 0, 10},
    {'E', "Delays", "Manual field E, default 3. Brake and reversal delay are not separate commands.", true, 0, 99},
    {'H', "CCW limit", "Manual default 180.", true, 0, 540},
    {'I', "CW limit", "Manual default 180.", true, 0, 540},
    {'L', "Cal range", "Manual default 360.", true, 0, 540},
    {'K', "Pulse divider", "Manual default 3960 (4015 on some firmware).", true, 1, 20000},
    {'O', "Brightness", "Manual default 8.", true, 0, 15},
    {'P', "Fail timeout", "Rotor-fail timeout. Manual default 4.", true, 0, 60},
    {'U', "Overtravel", "Degrees of overtravel allowed. Manual default 90.", true, 0, 180},
    {'V', "Counter-rotate delay", "Manual default 10.", true, 0, 999},
    {'Y', "Knob time", "Manual default 40.", true, 0, 999},
    {'A', "CCW cal", "Raw CCW calibration constant. Manual default 0.", true, -99999, 99999},
    {'B', "CW cal", "Raw CW calibration constant. Manual default 950.", true, -99999, 99999},
    {'R', "Alt offset 1", "361 means unused. Manual default 361.", true, 0, 999},
    {'S', "Alt offset 2", "Manual default 361.", true, 0, 999},
    {'T', "Advanced options", "Bitfield from Appendix F. The boxes below edit the documented bits.", true, 0, 65535},
    {'J', "Option", "Rotor type. The manual does not document the numeric codes, so this is not written.", false, 0, 0},
    {'M', "Mode", "Not written. The manual lists the words, not the codes.", false, 0, 0},
    {'Q', "Security", "Not written.", false, 0, 0},
    {'X', "Limit switch", "Read from the controller. Not written from here.", false, 0, 0},
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
    layout->addWidget(tabs, 1);
    buildStation();
    buildRotors();
    buildController();

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
    if (!driver)
        return;
    connect(driver, &Rt21Driver::configField, this,
                [this](QChar id, const QString &raw, bool numeric, int value) {
                    for (FieldRow &field : fields) {
                        if (field.id != id)
                            continue;
                        field.raw->setText(raw);
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
                });
        connect(driver, &Rt21Driver::configNote, this, [this](const QString &text) {
            const QString current = versionLabel->text();
            if (current.isEmpty() || current.startsWith(QStringLiteral("Not read")))
                versionLabel->setText(text);
            else
                versionLabel->setText(current + QStringLiteral("   ·   ") + text);
        });
        connect(driver, &Rt21Driver::configFinished, this, [this](bool ok, const QString &message) {
            if (!ok && !message.isEmpty())
                QMessageBox::warning(this, QStringLiteral("Read setup"), message);
        });
        connect(driver, &Rt21Driver::writeFinished, this, [this](bool ok, const QString &message) {
            QMessageBox::information(this, QStringLiteral("Apply"), message);
            if (ok && driver)
                driver->readConfiguration();
        });
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
    auto *logPath = new QLabel(QStringLiteral("Log folder: %1").arg(TrafficLog::instance().directory()));
    logPath->setWordWrap(true);
    logPath->setObjectName(QStringLiteral("hint"));
    layout->addSpacing(16);
    layout->addWidget(trafficBox);
    layout->addWidget(logPath);
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
    transport->addItems({QStringLiteral("IP / TCP"), QStringLiteral("Serial")});
    hostEdit = new QLineEdit;
    portSpin = new QSpinBox;
    portSpin->setRange(1, 65535);
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
    transportPages = new QStackedWidget;
    transportPages->addWidget(tcp);
    transportPages->addWidget(serial);

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
        if (isVisible() && transport->currentIndex() == 1)
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
    transport->setCurrentIndex(rotor.transport == RotorEndpoint::Transport::Serial ? 1 : 0);
    hostEdit->setText(rotor.host);
    portSpin->setValue(rotor.tcpPort);
    if (!rotor.serialPort.isEmpty() && portCombo->findText(rotor.serialPort) < 0)
        portCombo->addItem(rotor.serialPort);
    portCombo->setCurrentText(rotor.serialPort);
    baudCombo->setCurrentText(QString::number(rotor.baud == 9600 ? 9600 : 4800));
}

void SettingsDialog::storeRotor(int index)
{
    if (index < 0 || index >= draft.rotors.size())
        return;
    RotorEndpoint &rotor = draft.rotors[index];
    rotor.name = nameEdit->text().trimmed().isEmpty() ? QStringLiteral("RT-21") : nameEdit->text().trimmed();
    rotor.transport = transport->currentIndex() == 1 ? RotorEndpoint::Transport::Serial
                                                     : RotorEndpoint::Transport::Tcp;
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

    layout->addWidget(banner);
    layout->addWidget(versionLabel);
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
    tabs->addTab(page, QStringLiteral("Controller"));

    connect(read, &QPushButton::clicked, this, [this]() {
        if (!driver)
            return;
        versionLabel->clear();
        for (FieldRow &field : fields) {
            field.raw->setText(QStringLiteral("…"));
            field.numeric = false;
        }
        driver->readConfiguration();
    });
    connect(backup, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Backup RT-21 setup"),
                                                          QStringLiteral("meridian-rt21-backup.json"),
                                                          QStringLiteral("JSON (*.json)"));
        if (path.isEmpty())
            return;
        const QString error = backupConfig(path);
        if (!error.isEmpty())
            QMessageBox::warning(this, QStringLiteral("Backup"), error);
    });
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
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/meridian-rt21-backup.json");
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Couldn't write %1.").arg(path);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return {};
}
