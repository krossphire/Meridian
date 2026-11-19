/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Switching IP / Serial in the form must not touch the live driver.
 * Leaving settings or pressing Save applies once, and a transport change
 * retires the old driver before a new one exists.
 */

#include "FirmwareHexFixture.h"
#include "FirmwarePage.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Driver.h"
#include "rotor/UserFiles.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPort>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class TestSettings : public QObject
{
    Q_OBJECT
private slots:
    void transportFlickKeepsOneConnection();
    void firmwareUpdateBacksUpThenRefuses();
};

void TestSettings::transportFlickKeepsOneConnection()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    MockRt21Server server;
    QVERIFY(server.listenTcp(0));

    AppConfig config = AppConfig::defaults();
    config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
    config.rotors[0].host = QStringLiteral("127.0.0.1");
    config.rotors[0].tcpPort = int(server.port());

    MainWindow window(config, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasClient(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 3000);
    QCOMPARE(window.driverCount(), 1);
    QCOMPARE(Rt21Driver::instanceCount(), 1);
    QCOMPARE(Rt21Driver::transportsOpen(), 1);
    Rt21Driver::resetDiagnostics();
    Rt21Driver *original = window.driver();

    SettingsDialog *dialog = window.settingsDialog();
    auto *transport = dialog->findChild<QComboBox *>(QStringLiteral("transportPick"));
    QVERIFY(transport);
    QCOMPARE(transport->currentIndex(), 1);

    for (int i = 0; i < 3; ++i) {
        transport->setCurrentIndex(2);
        QCoreApplication::processEvents();
        transport->setCurrentIndex(1);
        QCoreApplication::processEvents();
    }
    QCOMPARE(server.accepts(), 1);
    QVERIFY(server.hasClient());
    QCOMPARE(window.driver(), original);
    QCOMPARE(window.driverCount(), 1);
    QCOMPARE(Rt21Driver::instanceCount(), 1);
    QCOMPARE(Rt21Driver::transportsOpen(), 1);
    QCOMPARE(Rt21Driver::transportHighWater(), 1);
    QCOMPARE(original->findChildren<QTcpSocket *>().size(), 1);

    // Leaving settings with the transport back on the same IP must not rebuild.
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCOMPARE(server.accepts(), 1);
    QVERIFY(server.hasClient());
    QCOMPARE(window.driver(), original);
    QCOMPARE(Rt21Driver::instanceCount(), 1);
    QCOMPARE(Rt21Driver::transportHighWater(), 1);

    // Apply a real transport change, then come back to IP. One driver at a time.
    dialog = window.settingsDialog();
    transport = dialog->findChild<QComboBox *>(QStringLiteral("transportPick"));
    QVERIFY(transport);
    transport->setCurrentIndex(2);
    for (QComboBox *box : dialog->findChildren<QComboBox *>()) {
        if (box->isEditable())
            box->setCurrentText(QStringLiteral("meridian-no-such-port"));
    }
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();

    QCOMPARE(window.driverCount(), 1);
    QCOMPARE(Rt21Driver::instanceCount(), 1);
    QVERIFY(window.driver() != original);
    QVERIFY(!server.hasClient());
    QCOMPARE(server.accepts(), 1);
    QCOMPARE(window.findChildren<QTcpSocket *>().size(), 0);
    const int serialPorts = window.driver()->findChildren<QSerialPort *>().size();
    QVERIFY2(serialPorts == Rt21Driver::transportsOpen(), qPrintable(window.driver()->detail()));
    QVERIFY(serialPorts <= 1);
    QCOMPARE(Rt21Driver::transportHighWater(), 1);
    QCOMPARE(window.findChildren<QSerialPort *>().size(), serialPorts);
    int liveTimers = 0;
    for (QTimer *timer : window.driver()->findChildren<QTimer *>()) {
        if (timer->isActive())
            ++liveTimers;
    }
    QVERIFY(liveTimers >= 1);
    QVERIFY(liveTimers <= 2);

    dialog = window.settingsDialog();
    transport = dialog->findChild<QComboBox *>(QStringLiteral("transportPick"));
    transport->setCurrentIndex(1);
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    QVERIFY(buttons);
    QPushButton *save = buttons->button(QDialogButtonBox::Save);
    QVERIFY(save);
    save->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasClient(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(server.accepts(), 2, 3000);
    QCOMPARE(window.driverCount(), 1);
    QCOMPARE(Rt21Driver::instanceCount(), 1);
    QCOMPARE(Rt21Driver::transportsOpen(), 1);
    QCOMPARE(Rt21Driver::transportHighWater(), 1);
    QCOMPARE(window.driver()->findChildren<QTcpSocket *>().size(), 1);
    QCOMPARE(window.findChildren<QSerialPort *>().size(), 0);
}

void TestSettings::firmwareUpdateBacksUpThenRefuses()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    MockRt21Server server;
    QVERIFY(server.listenTcp(0));

    AppConfig config = AppConfig::defaults();
    config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
    config.rotors[0].host = QStringLiteral("127.0.0.1");
    config.rotors[0].tcpPort = int(server.port());

    MainWindow window(config, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasClient(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 3000);

    SettingsDialog *dialog = window.settingsDialog();
    auto *page = dialog->findChild<FirmwarePage *>();
    QVERIFY(page);
    page->loadHexText(validFirmwareHex(), QStringLiteral("fixture.hex"));
    auto *current = dialog->findChild<QLabel *>(QStringLiteral("firmwareCurrent"));
    auto *target = dialog->findChild<QLabel *>(QStringLiteral("firmwareTarget"));
    auto *report = dialog->findChild<QLabel *>(QStringLiteral("firmwareReport"));
    auto *confirm = dialog->findChild<QLineEdit *>(QStringLiteral("firmwareConfirm"));
    auto *update = dialog->findChild<QPushButton *>(QStringLiteral("firmwareUpdate"));
    auto *banner = dialog->findChild<QLabel *>(QStringLiteral("firmwareBanner"));
    QVERIFY(current);
    QVERIFY(target);
    QVERIFY(report);
    QVERIFY(confirm);
    QVERIFY(update);
    QVERIFY(banner);
    QVERIFY(banner->text().contains(QStringLiteral("Experimental")));
    QVERIFY(report->text().contains(QStringLiteral("CRC: 9241")));
    QVERIFY(report->text().contains(QStringLiteral("Created: 2026-11-19")));
    QVERIFY(report->text().contains(QStringLiteral("Hex file is valid.")));
    QCOMPARE(target->text(), QStringLiteral("Target in the hex file: RT-21 4.13.2"));
    QVERIFY(current->text().contains(QStringLiteral("R1;")));
    QVERIFY(!update->isEnabled());

    confirm->setText(QStringLiteral("flash"));
    QVERIFY(!update->isEnabled());
    confirm->setText(QStringLiteral("FLASH"));
    QVERIFY(update->isEnabled());
    update->click();

    auto *status = dialog->findChild<QLabel *>(QStringLiteral("setupStatus"));
    QVERIFY(status);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("Bootloader protocol not yet captured")), 8000);
    QVERIFY(status->styleSheet().contains(statusToneColor(StatusTone::Failure).name()));
    QCOMPARE(current->text(), QStringLiteral("Current controller (R1;): RT-21 4.13.2"));

    const QStringList copies = QDir(UserFiles::backupDirectory()).entryList(QStringList() << QStringLiteral("meridian-setup-*.txt"));
    QCOMPARE(copies.size(), 1);
    QFile copy(QDir(UserFiles::backupDirectory()).filePath(copies.first()));
    QVERIFY(copy.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(copy.readAll());
    QVERIFY(body.contains(QStringLiteral("Firmware: RT-21 4.13.2")));
    QVERIFY(body.contains(QStringLiteral("[controller]")));
    QVERIFY(body.contains(QStringLiteral("Screen saver (W)")));

    for (const QByteArray &command : server.sim()->trace()) {
        QVERIFY(!command.startsWith("B2"));
        QVERIFY(!command.startsWith("BU"));
    }
}

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
