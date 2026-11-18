/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MainWindow.h"

#include "StatusTone.h"

#include "GlobeWidget.h"
#include "SettingsDialog.h"
#include "core/Coordinates.h"
#include "rotor/Reconnect.h"
#include "rotor/Rt21Driver.h"
#include "rotor/TrafficLog.h"
#include "rotor/UserFiles.h"

#include <QDateTime>
#include <QFile>
#include <QKeyEvent>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QTimer>

MainWindow::MainWindow(AppConfig initial, QSettings *store, QWidget *parent)
    : QMainWindow(parent)
    , config(std::move(initial))
    , settings(store)
{
    setWindowTitle(QStringLiteral("Meridian 0.1.0-alpha23"));
    TrafficLog::instance().setEnabled(config.trafficLog);
    globe = new GlobeWidget;
    setCentralWidget(globe);
    resize(880, 920);

    loadMaps();

    connect(globe, &GlobeWidget::bearingRequested, this, [this](double azimuth) {
        if (link)
            link->goTo(azimuth);
    });
    connect(globe, &GlobeWidget::headingEdited, this, [this](double azimuth) {
        if (link)
            link->goTo(azimuth);
    });
    connect(globe, &GlobeWidget::stopRequested, this, [this]() {
        if (link)
            link->stopMotion();
    });
    connect(globe, &GlobeWidget::settingsRequested, this, &MainWindow::openSettings);
    connect(globe, &GlobeWidget::rotorSelected, this, [this](int index) {
        if (index < 0 || index >= config.rotors.size() || index == config.active)
            return;
        config.active = index;
        UserFiles::save(config);
        connectActive();
    });

    auto *settingsShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), this);
    connect(settingsShortcut, &QShortcut::activated, this, &MainWindow::openSettings);

    applyHome();
    QStringList names;
    for (const RotorEndpoint &rotor : config.rotors)
        names.append(rotor.name);
    globe->setRotors(names, config.active);
    globe->setShowSun(config.showSun);
    connectActive();

    statusTimer = new QTimer(this);
    statusTimer->setInterval(1000);
    connect(statusTimer, &QTimer::timeout, this, [this]() { refreshStatus(); });
    statusTimer->start();
    refreshStatus();
}

MainWindow::~MainWindow()
{
    if (settingsUi)
        settingsUi->setDriver(nullptr);
    const auto owned = drivers.values();
    for (Rt21Driver *driver : owned)
        disconnect(driver, nullptr, this, nullptr);
    link = nullptr;
}

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
    syncDrivers();
}

void MainWindow::applyConfig(const AppConfig &saved)
{
    config = saved;
    TrafficLog::instance().setEnabled(config.trafficLog);
    UserFiles::save(config);
    applyHome();
    QStringList names;
    for (const RotorEndpoint &rotor : config.rotors)
        names.append(rotor.name);
    globe->setRotors(names, config.active);
    globe->setShowSun(config.showSun);
    syncDrivers();
}

void MainWindow::refreshStatus()
{
    if (!globe)
        return;
    const RotorEndpoint *active = config.activeRotor();
    if (!active || !active->configured()) {
        globe->setStatusLine(QStringLiteral("Set up your RT-21 in settings"), StatusTone::Neutral);
        globe->setHeadingStale(false);
        return;
    }
    QString line;
    StatusTone tone = StatusTone::Neutral;
    if (link && !link->transientNote().isEmpty()) {
        line = link->transientNote();
        tone = statusTone(line);
    } else if (link) {
        const auto state = link->link();
        if (state == Rt21Driver::Link::Failed || state == Rt21Driver::Link::Off
            || state == Rt21Driver::Link::Connecting) {
            if (state == Rt21Driver::Link::Failed && link->lastSeenMs() < 0 && !link->detail().isEmpty()) {
                line = link->detail();
            } else if (link->lastSeenMs() >= 0) {
                const qint64 secs = std::max<qint64>(0, (QDateTime::currentMSecsSinceEpoch() - link->lastSeenMs()) / 1000);
                line = lastSeenPhrase(secs);
            } else if (state == Rt21Driver::Link::Connecting) {
                line = link->detail();
            } else {
                line = QStringLiteral("RT-21 not connected");
            }
            tone = state == Rt21Driver::Link::Connecting ? StatusTone::Busy : StatusTone::Failure;
        }
    }
    globe->setStatusLine(line, tone);
    bool stale = false;
    if (link && link->lastSeenMs() >= 0
        && (link->link() == Rt21Driver::Link::On || link->link() == Rt21Driver::Link::Moving)) {
        const qint64 age = QDateTime::currentMSecsSinceEpoch() - link->lastSeenMs();
        stale = age > qint64(link->currentPollMs()) * 2;
    }
    globe->setHeadingStale(stale);
}

void MainWindow::retireDriver(const QString &id)
{
    Rt21Driver *old = drivers.take(id);
    if (!old)
        return;
    if (link == old) {
        disconnect(old, nullptr, this, nullptr);
        link = nullptr;
    }
    old->retire();
    old->deleteLater();
}

void MainWindow::adopt(Rt21Driver *driver)
{
    if (link == driver)
        return;
    if (link)
        disconnect(link, nullptr, this, nullptr);
    link = driver;
    if (!link)
        return;
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
        refreshStatus();
    });
    connect(link, &Rt21Driver::statusNote, this, [this](const QString &) { refreshStatus(); });
    connect(link, &Rt21Driver::headingChanged, this, [this](double) { refreshStatus(); });
    link->setIdlePollSeconds(config.idlePollSeconds);
}

void MainWindow::syncDrivers()
{
    QSet<QString> keep;
    Rt21Driver *want = nullptr;
    const RotorEndpoint *active = config.activeRotor();
    for (const RotorEndpoint &rotor : config.rotors) {
        if (rotor.id.isEmpty())
            continue;
        keep.insert(rotor.id);
        const bool isActive = active && active->id == rotor.id;
        Rt21Driver *driver = drivers.value(rotor.id, nullptr);
        if (driver && isActive && !driver->serves(rotor)) {
            // Transport, host, or port changed. The old driver is fully shut
            // down before the replacement exists in the map.
            retireDriver(rotor.id);
            driver = nullptr;
        }
        if (!driver) {
            driver = new Rt21Driver(this);
            drivers.insert(rotor.id, driver);
        }
        driver->setIdlePollSeconds(config.idlePollSeconds);
        if (isActive)
            want = driver;
        else if (driver->link() != Rt21Driver::Link::Off)
            driver->disconnectLink();
        if (isActive && !rotor.configured() && driver->link() != Rt21Driver::Link::Off)
            driver->disconnectLink();
    }
    const QStringList ids = drivers.keys();
    for (const QString &id : ids) {
        if (!keep.contains(id))
            retireDriver(id);
    }
    if (want) {
        adopt(want);
        const RotorEndpoint *rotor = config.activeRotor();
        if (rotor && rotor->configured() && !want->serves(*rotor))
            want->connectEndpoint(*rotor);
        else if (rotor && rotor->configured()
                 && (want->link() == Rt21Driver::Link::Off || want->link() == Rt21Driver::Link::Failed))
            want->connectEndpoint(*rotor);
        else if (rotor && !rotor->configured() && want->link() != Rt21Driver::Link::Off)
            want->disconnectLink();
    } else {
        adopt(nullptr);
        globe->setHeading(std::nullopt);
        globe->setTarget(std::nullopt);
        globe->setLink(GlobeWidget::Link::Off, QStringLiteral("Disconnected"));
    }
    if (settingsUi)
        settingsUi->setDriver(link);
}

void MainWindow::openSettings()
{
    if (!settingsUi) {
        settingsUi = new SettingsDialog(config, &zips, link, this);
        connect(settingsUi, &SettingsDialog::configSaved, this, [this](const AppConfig &saved) {
            applyConfig(saved);
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
