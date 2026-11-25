/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "map/GeoMap.h"
#include "rotor/AppConfig.h"
#include "core/ZipCodes.h"

#include <QHash>
#include <QMainWindow>

class GlobeWidget;
class Rt21Driver;
class SettingsDialog;
class QrzClient;
class QSettings;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(AppConfig config, QSettings *settings, QWidget *parent = nullptr);
    ~MainWindow() override;

    GlobeWidget *globeWidget() const { return globe; }
    Rt21Driver *driver() const { return link; }
    int driverCount() const { return drivers.size(); }
    SettingsDialog *settingsDialog();
    bool mapsLoaded() const { return mapsOk; }

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void loadMaps();
    void applyHome();
    void connectActive();
    void openSettings();
    void applyConfig(const AppConfig &saved);
    void syncDrivers();
    void retireDriver(const QString &id);
    void adopt(Rt21Driver *driver);
    void refreshStatus();
    void offerStation(const QString &call, const QString &grid, double latitude = 0, double longitude = 0,
                      bool havePoint = false, bool allowLookup = true);
    void refreshQrz();

    AppConfig config;
    QSettings *settings = nullptr;
    GeoLayer low;
    GeoLayer high;
    ZipDatabase zips;
    bool mapsOk = false;
    GlobeWidget *globe = nullptr;
    QHash<QString, Rt21Driver *> drivers;
    Rt21Driver *link = nullptr;
    SettingsDialog *settingsUi = nullptr;
    QTimer *statusTimer = nullptr;
    QrzClient *qrz = nullptr;
    QString pendingCall;
};
