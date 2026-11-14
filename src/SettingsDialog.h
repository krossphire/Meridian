/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "rotor/AppConfig.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class Rt21Driver;
class ZipDatabase;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(const AppConfig &config, const ZipDatabase *zips, Rt21Driver *driver, QWidget *parent = nullptr);

    void setConfig(const AppConfig &config);
    AppConfig config() const { return draft; }
    void showPage(int index);

signals:
    void configSaved(const AppConfig &config);

private:
    struct FieldRow {
        QChar id;
        bool writable = false;
        QSpinBox *spin = nullptr;
        QLabel *raw = nullptr;
        int original = 0;
        bool numeric = false;
    };

    void buildStation();
    void buildRotors();
    void buildController();
    void refreshLocation();
    void loadRotor(int index);
    void storeRotor(int index);
    void refreshPorts();
    QString backupConfig(const QString &path) const;

    AppConfig draft;
    const ZipDatabase *zips = nullptr;
    Rt21Driver *driver = nullptr;
    int rotorIndex = -1;

    class QTabWidget *tabs = nullptr;
    QLineEdit *homeEdit = nullptr;
    QLabel *summary = nullptr;
    QLabel *detail = nullptr;
    QListWidget *rotorList = nullptr;
    QLineEdit *nameEdit = nullptr;
    QComboBox *transport = nullptr;
    QStackedWidget *transportPages = nullptr;
    QLineEdit *hostEdit = nullptr;
    QSpinBox *portSpin = nullptr;
    QComboBox *portCombo = nullptr;
    QComboBox *baudCombo = nullptr;
    QLabel *versionLabel = nullptr;
    QVector<FieldRow> fields;
    class QCheckBox *bits[12] = {};
    bool fillingBits = false;
};
