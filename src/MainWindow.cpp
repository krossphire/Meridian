/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MainWindow.h"

#include "GlobeWidget.h"
#include "SettingsDialog.h"
#include "core/Coordinates.h"
#include "rotor/Rt21Driver.h"
#include "rotor/TrafficLog.h"

#include <QFile>
#include <QKeyEvent>
#include <QSettings>
#include <QShortcut>

MainWindow::MainWindow(AppConfig initial, QSettings *store, QWidget *parent)
    : QMainWindow(parent)
    , config(std::move(initial))
    , settings(store)
{
    setWindowTitle(QStringLiteral("Meridian 0.1.0-alpha1"));
    TrafficLog::instance().setEnabled(config.trafficLog);
    globe = new GlobeWidget;
    setCentralWidget(globe);
    resize(880, 920);

    link = new Rt21Driver(this);
    loadMaps();

    connect(link, &Rt21Driver::headingChanged, this, [this](double azimuth) { globe->setHeading(azimuth); });
    connect(link, &Rt21Driver::targetChanged, this, [this](double azimuth, bool active) {
        globe->setTarget(active ? std::optional<double>(azimuth) : std::nullopt);
    });
    connect(link, &Rt21Driver::linkChanged, this, [this](Rt21Driver::Link state, const QString &detail) {
        GlobeWidget::Link shown = GlobeWidget::Link::Off;
        switch (state) {
        case Rt21Driver::Link::Connecting:
            shown = GlobeWidget::Link::Connecting;
            break;
        case Rt21Driver::Link::On:
            shown = GlobeWidget::Link::On;
            break;
        case Rt21Driver::Link::Moving:
            shown = GlobeWidget::Link::Moving;
            break;
        case Rt21Driver::Link::Failed:
            shown = GlobeWidget::Link::Failed;
            break;
        case Rt21Driver::Link::Off:
            shown = GlobeWidget::Link::Off;
            break;
        }
        globe->setLink(shown, detail);
    });
    connect(globe, &GlobeWidget::bearingRequested, link, &Rt21Driver::goTo);
    connect(globe, &GlobeWidget::headingEdited, link, &Rt21Driver::goTo);
    connect(globe, &GlobeWidget::stopRequested, link, &Rt21Driver::stopMotion);
    connect(globe, &GlobeWidget::settingsRequested, this, &MainWindow::openSettings);
    connect(globe, &GlobeWidget::rotorSelected, this, [this](int index) {
        if (index < 0 || index >= config.rotors.size() || index == config.active)
            return;
        config.active = index;
        if (settings)
            config.save(*settings);
        connectActive();
    });

    auto *settingsShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), this);
    connect(settingsShortcut, &QShortcut::activated, this, &MainWindow::openSettings);

    applyHome();
    QStringList names;
    for (const RotorEndpoint &rotor : config.rotors)
        names.append(rotor.name);
    globe->setRotors(names, config.active);
    connectActive();
}

MainWindow::~MainWindow() = default;

void MainWindow::loadMaps()
{
    auto read = [](const QString &path, GeoLayer &layer) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        return loadGeoLayer(file.readAll(), layer);
    };
    QFile zipsFile(QStringLiteral(":/data/zcta.bin"));
    mapsOk = read(QStringLiteral(":/data/map110.bin"), low) && read(QStringLiteral(":/data/map50.bin"), high)
             && zipsFile.open(QIODevice::ReadOnly) && zips.load(zipsFile.readAll());
    if (mapsOk)
        globe->setLayers(&low, &high);
}

void MainWindow::applyHome()
{
    QString error;
    auto location = resolveLocation(config.homeText, &zips, &error);
    if (!location)
        location = resolveLocation(QStringLiteral("EM26"), &zips, &error);
    if (location)
        globe->setHome(location->latitude, location->longitude);
}

void MainWindow::connectActive()
{
    if (const RotorEndpoint *rotor = config.activeRotor())
        link->connectEndpoint(*rotor);
    else
        link->disconnectLink();
}

void MainWindow::openSettings()
{
    if (!settingsUi) {
        settingsUi = new SettingsDialog(config, &zips, link, this);
        connect(settingsUi, &SettingsDialog::configSaved, this, [this](const AppConfig &saved) {
            config = saved;
            TrafficLog::instance().setEnabled(config.trafficLog);
            if (settings)
                config.save(*settings);
            applyHome();
            QStringList names;
            for (const RotorEndpoint &rotor : config.rotors)
                names.append(rotor.name);
            globe->setRotors(names, config.active);
            connectActive();
        });
    } else {
        settingsUi->setConfig(config);
    }
    settingsUi->show();
    settingsUi->raise();
    settingsUi->activateWindow();
}

SettingsDialog *MainWindow::settingsDialog()
{
    openSettings();
    return settingsUi;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) {
        link->stopMotion();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
