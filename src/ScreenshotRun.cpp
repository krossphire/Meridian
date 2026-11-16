/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GlobeWidget.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "rotor/Rt21Finder.h"
#include "rotor/UsbHint.h"
#include "rotor/UserFiles.h"
#include "mock/Rt21Simulator.h"
#include "rotor/Rt21Driver.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>

#include <functional>

namespace {

bool waitUntil(const std::function<bool()> &ready, int timeoutMs)
{
    if (ready())
        return true;
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(20);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        if (ready())
            loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    timer.start();
    loop.exec();
    return ready();
}

void save(const QImage &image, const QString &path)
{
    image.save(path);
}

} // namespace

int runScreenshots(const QString &directory, qreal dpr)
{
    QDir().mkpath(directory);
    UserFiles::setRoot(directory);
    QSettings settings(directory + QStringLiteral("/shot.ini"), QSettings::IniFormat);
    AppConfig config = AppConfig::defaults();
    config.homeText = QStringLiteral("EM26");
    config.rotors[0].name = QStringLiteral("Station");
    config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
    config.rotors[0].host = QStringLiteral("127.0.0.1");
    config.rotors[0].tcpPort = 17621;
    RotorEndpoint tower;
    tower.id = QStringLiteral("tower");
    tower.name = QStringLiteral("Tower");
    tower.transport = RotorEndpoint::Transport::Tcp;
    tower.host = QStringLiteral("127.0.0.1");
    tower.tcpPort = 17621;
    config.rotors.append(tower);
    config.active = 0;

    MockRt21Server mock;
    mock.sim()->setHeading(52);
    mock.sim()->setFrozen(true);
    if (!mock.listenTcp(17621))
        return 2;

    MainWindow window(config, &settings);
    window.show();

    const auto ready = [&]() {
        return window.mapsLoaded() && window.globeWidget()->mapReady() && window.driver()->heading().has_value();
    };
    if (!waitUntil(ready, 20000))
        return 3;

    auto shoot = [&](const QSize &size, const QString &name) {
        window.resize(size);
        QApplication::processEvents();
        if (!waitUntil([&]() { return window.globeWidget()->mapReady(); }, 20000))
            return false;
        QApplication::processEvents();
        return window.grab().save(directory + QLatin1Char('/') + name);
    };

    window.resize(180, 180);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(window.globeWidget(), &leave);
    if (!shoot(QSize(180, 180), QStringLiteral("main-180.png")))
        return 4;

    window.driver()->goTo(128);
    if (!shoot(QSize(400, 400), QStringLiteral("main-400.png")))
        return 5;
    if (!shoot(QSize(1100, 1100), QStringLiteral("main-large.png")))
        return 6;

    window.globeWidget()->setShowSun(true);
    window.globeWidget()->setSolarPreview(QDateTime(QDate(2024, 6, 21), QTime(1, 30), Qt::UTC));
    if (!shoot(QSize(180, 180), QStringLiteral("sun-180.png")))
        return 13;
    if (!shoot(QSize(400, 400), QStringLiteral("sun-400.png")))
        return 14;
    if (!shoot(QSize(1100, 1100), QStringLiteral("sun-large.png")))
        return 15;

    MainWindow fresh(AppConfig::defaults(), nullptr);
    fresh.resize(900, 940);
    fresh.show();
    QApplication::processEvents();
    if (!fresh.grab().save(directory + QStringLiteral("/first-run.png")))
        return 16;
    SettingsDialog *setup = fresh.settingsDialog();
    setup->showPage(1);
    setup->resize(680, 780);
    QApplication::processEvents();
    if (!setup->grab().save(directory + QStringLiteral("/find-rt21.png")))
        return 17;
    UsbDeviceFact missing;
    missing.vid = 0x10C4;
    missing.pid = 0xEA60;
    missing.instanceId = QStringLiteral("cp210x");
    missing.name = QStringLiteral("CP2102 USB to UART Bridge");
    missing.problemCode = 28;
    missing.lastArrival = QDateTime::currentDateTimeUtc();
    UsbDeviceFact failed;
    failed.vid = 0x0000;
    failed.pid = 0x0002;
    failed.instanceId = QStringLiteral("descriptor");
    failed.name = QStringLiteral("Unknown USB Device (Device Descriptor Request Failed)");
    failed.problemCode = 43;
    failed.lastArrival = QDateTime::currentDateTimeUtc();
    setup->showUsbFacts({missing, failed});
    QApplication::processEvents();
    if (!setup->grab().save(directory + QStringLiteral("/find-usb.png")))
        return 25;
    FoundRotor sample;
    sample.endpoint.transport = RotorEndpoint::Transport::Serial;
    sample.endpoint.serialPort = QStringLiteral("COM6");
    sample.endpoint.baud = 4800;
    sample.heading = 132;
    sample.label = foundRotorLabel(QStringLiteral("COM6"), QStringLiteral("USB"), 4800, 132);
    setup->presentFound(sample);
    QApplication::processEvents();
    if (!setup->grab().save(directory + QStringLiteral("/find-result.png")))
        return 18;
    setup->close();
    fresh.close();

    const qreal sharpDpr = dpr > 0 ? dpr : 2.0;
    window.resize(760, 760);
    window.globeWidget()->setForcedDevicePixelRatio(sharpDpr);
    if (!waitUntil([&]() { return window.globeWidget()->mapReady(); }, 20000))
        return 7;
    const QImage sharp = window.globeWidget()->renderSharp();
    if (sharp.devicePixelRatio() < 1.5 || sharp.width() < 1400)
        return 8;
    save(sharp, directory + QStringLiteral("/main-2x.png"));
    window.globeWidget()->setForcedDevicePixelRatio(0);

    window.resize(760, 820);
    if (!waitUntil([&]() { return window.globeWidget()->mapReady(); }, 20000))
        return 9;
    window.globeWidget()->openHeadingEditor();
    QApplication::processEvents();
    if (!window.grab().save(directory + QStringLiteral("/heading-edit.png")))
        return 10;

    SettingsDialog *dialog = window.settingsDialog();
    dialog->resize(680, 860);
    dialog->showPage(0);
    QApplication::processEvents();
    if (!dialog->grab().save(directory + QStringLiteral("/settings.png")))
        return 11;

    dialog->showPage(2);
    window.driver()->readConfiguration();
    const bool read = waitUntil([&]() {
        auto *label = dialog->findChild<QLabel *>(QStringLiteral("versionLabel"));
        auto *callout = dialog->findChild<QLabel *>(QStringLiteral("displayCallout"));
        return label && label->text().contains(QStringLiteral("4.13.2")) && callout
               && callout->text().contains(QStringLiteral("Screen saver is off"));
    }, 8000);
    QApplication::processEvents();
    if (!read || !dialog->grab().save(directory + QStringLiteral("/settings-controller.png")))
        return 12;

    auto *scroll = dialog->findChild<QScrollArea *>(QStringLiteral("setupScroll"));
    auto *writeBrightness = dialog->findChild<QPushButton *>(QStringLiteral("writeBrightness"));
    auto *brightnessNote = dialog->findChild<QLabel *>(QStringLiteral("brightnessNote"));
    auto *saver = dialog->findChild<QComboBox *>(QStringLiteral("screenSaver"));
    if (scroll && saver)
        scroll->ensureWidgetVisible(saver);
    else if (scroll && writeBrightness)
        scroll->ensureWidgetVisible(writeBrightness);
    if (!saver || !saver->isEnabled() || saver->findText(QStringLiteral("Off")) < 0)
        return 26;
    QApplication::processEvents();
    if (!writeBrightness || !brightnessNote || !brightnessNote->text().contains(QStringLiteral("OLED"))
        || !dialog->grab().save(directory + QStringLiteral("/settings-brightness.png")))
        return 25;

    auto *networkGroup = dialog->findChild<QWidget *>(QStringLiteral("networkGroup"));
    if (scroll && networkGroup)
        scroll->ensureWidgetVisible(networkGroup);
    auto *readNetwork = dialog->findChild<QPushButton *>(QStringLiteral("readNetwork"));
    auto *ipNote = dialog->findChild<QLabel *>(QStringLiteral("networkLinkNote"));
    if (scroll && readNetwork)
        scroll->ensureWidgetVisible(readNetwork);
    QApplication::processEvents();
    if (!readNetwork || readNetwork->isEnabled() || !ipNote
        || !ipNote->text().contains(QStringLiteral("Needs a USB or serial connection"))
        || !dialog->grab().save(directory + QStringLiteral("/setup-network.png")))
        return 21;
    auto *guide = dialog->findChild<QPushButton *>(QStringLiteral("openWizard"));
    if (!guide)
        return 22;
    guide->click();
    auto *next = dialog->findChild<QPushButton *>(QStringLiteral("wizardNext"));
    if (!next)
        return 23;
    next->click();
    next->click();
    QApplication::processEvents();
    if (!dialog->grab().save(directory + QStringLiteral("/setup-wizard.png")))
        return 24;
    return 0;
}
