/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "FirmwarePage.h"

#include "StatusTone.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

FirmwarePage::FirmwarePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("firmwarePage"));
    auto *layout = new QVBoxLayout(this);

    banner = new QLabel(QStringLiteral(
        "Experimental. This page checks a hex file and saves the settings it can read. "
        "It does not enter the bootloader and it does not write firmware."));
    banner->setObjectName(QStringLiteral("firmwareBanner"));
    banner->setWordWrap(true);
    banner->setStyleSheet(QStringLiteral("color: #e0b15c;"));

    auto *hint = new QLabel(QStringLiteral(
        "Green Heron advises saving settings before a flash and doing an EE Reset afterward. "
        "The copy made here is the readable settings. Nothing is reset."));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8ea099;"));

    auto *pick = new QPushButton(QStringLiteral("Choose a hex file"));
    pick->setObjectName(QStringLiteral("firmwarePick"));
    pick->setAutoDefault(false);
    connect(pick, &QPushButton::clicked, this, &FirmwarePage::chooseFile);

    fileName = new QLabel(QStringLiteral("No file chosen."));
    fileName->setObjectName(QStringLiteral("firmwareFile"));
    fileName->setWordWrap(true);
    fileName->setTextInteractionFlags(Qt::TextSelectableByMouse);

    report = new QLabel(QStringLiteral("Validation will appear here."));
    report->setObjectName(QStringLiteral("firmwareReport"));
    report->setWordWrap(true);
    report->setTextInteractionFlags(Qt::TextSelectableByMouse);
    report->setStyleSheet(QStringLiteral("color: #d5e2db;"));

    currentVersion = new QLabel;
    currentVersion->setObjectName(QStringLiteral("firmwareCurrent"));
    currentVersion->setWordWrap(true);
    setCurrentVersion(QString());

    targetVersion = new QLabel(QStringLiteral("Target in the hex file: —"));
    targetVersion->setObjectName(QStringLiteral("firmwareTarget"));
    targetVersion->setWordWrap(true);

    auto *versionNote = new QLabel(QStringLiteral(
        "The current version is the R1; read. Meridian sends it as R11;, rotor address 1."));
    versionNote->setWordWrap(true);
    versionNote->setStyleSheet(QStringLiteral("color: #8ea099;"));

    confirm = new QLineEdit;
    confirm->setObjectName(QStringLiteral("firmwareConfirm"));
    confirm->setPlaceholderText(QStringLiteral("Type FLASH to confirm"));
    connect(confirm, &QLineEdit::textChanged, this, [this]() { refresh(); });

    update = new QPushButton(QStringLiteral("Update firmware"));
    update->setObjectName(QStringLiteral("firmwareUpdate"));
    update->setAutoDefault(false);
    update->setEnabled(false);
    connect(update, &QPushButton::clicked, this, &FirmwarePage::requestUpdate);

    backup = new QLabel;
    backup->setObjectName(QStringLiteral("firmwareBackup"));
    backup->setWordWrap(true);
    backup->setTextInteractionFlags(Qt::TextSelectableByMouse);

    outcome = new QLabel;
    outcome->setObjectName(QStringLiteral("firmwareOutcome"));
    outcome->setWordWrap(true);

    layout->addWidget(banner);
    layout->addWidget(hint);
    layout->addWidget(pick, 0, Qt::AlignLeft);
    layout->addWidget(fileName);
    layout->addWidget(report);
    layout->addSpacing(8);
    layout->addWidget(currentVersion);
    layout->addWidget(versionNote);
    layout->addWidget(targetVersion);
    layout->addSpacing(8);
    layout->addWidget(confirm);
    layout->addWidget(update, 0, Qt::AlignLeft);
    layout->addWidget(backup);
    layout->addWidget(outcome);
    layout->addStretch();
}

void FirmwarePage::chooseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Firmware hex"), QString(),
        QStringLiteral("Intel HEX (*.hex);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusMessage(QStringLiteral("Couldn't read %1.").arg(path));
        return;
    }
    loadHexText(QString::fromUtf8(file.readAll()), QFileInfo(path).fileName());
}

void FirmwarePage::loadHexText(const QString &text, const QString &sourceName)
{
    image_ = Rt21::loadFirmwareHex(text);
    fileName->setText(sourceName.isEmpty() ? QStringLiteral("Hex file") : sourceName);
    backup->clear();
    outcome->clear();

    QStringList lines;
    lines << QStringLiteral("Device trailer: %1")
                 .arg(image_.device.isEmpty() ? QStringLiteral("missing") : image_.device);
    lines << QStringLiteral("CRC: %1").arg(image_.crc.isEmpty() ? QStringLiteral("missing") : image_.crc);
    lines << QStringLiteral("Created: %1")
                 .arg(image_.created.isEmpty() ? QStringLiteral("missing") : image_.created);
    lines << (image_.version.isEmpty()
                  ? QStringLiteral("Version string at 0x0676: not found")
                  : QStringLiteral("Version string at 0x0676: %1").arg(image_.version));
    lines << QStringLiteral("Application bytes: %1").arg(image_.applicationBytes);
    lines << QStringLiteral("Config bytes: %1").arg(image_.configBytes);
    lines << (image_.ok ? QStringLiteral("Hex file is valid.") : image_.error);
    report->setText(lines.join(QLatin1Char('\n')));
    report->setStyleSheet(QStringLiteral("color: %1;")
                              .arg(image_.ok ? QStringLiteral("#d5e2db")
                                             : statusToneColor(StatusTone::Failure).name()));
    targetVersion->setText(QStringLiteral("Target in the hex file: %1")
                               .arg(image_.version.isEmpty() ? QStringLiteral("—") : image_.version));
    refresh();
    if (!image_.ok)
        emit statusMessage(image_.error);
}

void FirmwarePage::setCurrentVersion(const QString &version)
{
    const QString shown = version.trimmed();
    currentVersion->setText(shown.isEmpty()
                                ? QStringLiteral("Current controller (R1;): not read yet")
                                : QStringLiteral("Current controller (R1;): %1").arg(shown));
}

void FirmwarePage::setBackupPath(const QString &path)
{
    backup->setText(path.isEmpty() ? QString() : QStringLiteral("Settings copy: %1").arg(path));
}

void FirmwarePage::setOutcome(const QString &text)
{
    outcome->setText(text);
    outcome->setStyleSheet(QStringLiteral("color: %1;").arg(statusToneColor(statusTone(text)).name()));
}

void FirmwarePage::refresh()
{
    const bool confirmed = confirm && confirm->text() == QLatin1String("FLASH");
    if (update)
        update->setEnabled(image_.ok && confirmed);
}

void FirmwarePage::requestUpdate()
{
    if (!image_.ok) {
        emit statusMessage(image_.error.isEmpty() ? QStringLiteral("Choose a valid hex file.") : image_.error);
        return;
    }
    if (!confirm || confirm->text() != QLatin1String("FLASH")) {
        emit statusMessage(QStringLiteral("Type FLASH to confirm."));
        return;
    }
    emit preflightRequested();
}
