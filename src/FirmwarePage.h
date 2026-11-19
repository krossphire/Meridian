/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Experimental firmware page. It checks a hex file and asks Settings to
 * back up the readable settings. It does not talk to the bootloader.
 */

#pragma once

#include "rotor/IntelHex.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

class FirmwarePage : public QWidget
{
    Q_OBJECT
public:
    explicit FirmwarePage(QWidget *parent = nullptr);

    void loadHexText(const QString &text, const QString &sourceName);
    const Rt21::FirmwareImage &image() const { return image_; }
    void setCurrentVersion(const QString &version);
    void setBackupPath(const QString &path);
    void setOutcome(const QString &text);

signals:
    void statusMessage(const QString &text);
    void preflightRequested();

private:
    void chooseFile();
    void refresh();
    void requestUpdate();

    Rt21::FirmwareImage image_;
    QLabel *banner = nullptr;
    QLabel *fileName = nullptr;
    QLabel *report = nullptr;
    QLabel *currentVersion = nullptr;
    QLabel *targetVersion = nullptr;
    QLabel *backup = nullptr;
    QLabel *outcome = nullptr;
    QLineEdit *confirm = nullptr;
    QPushButton *update = nullptr;
};
