/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Switching IP / Serial in the form must not touch the live driver.
 * Leaving settings or pressing Save applies once, and a transport change
 * retires the old driver before a new one exists.
 */

#include "MainWindow.h"
#include "SettingsDialog.h"
#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Driver.h"
#include "rotor/UserFiles.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
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
    void foundBaudIsKeptWhenItIs4800Or9600();
    void readNetworkStaysOffUntilSerialIsUp();
#ifdef Q_OS_UNIX
    void liveSerialFindStopsAndResumesAfterTheProbe();
#endif
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

void TestSettings::foundBaudIsKeptWhenItIs4800Or9600()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    AppConfig config = AppConfig::defaults();
    config.rotors[0].transport = RotorEndpoint::Transport::Serial;
    config.rotors[0].serialPort = QStringLiteral("COM6");
    config.rotors[0].baud = 4800;
    SettingsDialog dialog(config, nullptr, nullptr);

    FoundRotor hit;
    hit.endpoint.transport = RotorEndpoint::Transport::Serial;
    hit.endpoint.serialPort = QStringLiteral("COM6");
    hit.endpoint.baud = 9600;
    hit.label = QStringLiteral("9600 hit");
    dialog.presentFound(hit);
    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("findResult"));
    QVERIFY(button);
    button->click();
    QCOMPARE(dialog.config().rotors.at(0).baud, 9600);

    FoundRotor fast = hit;
    fast.endpoint.baud = 19200;
    fast.label = QStringLiteral("19200 hit");
    dialog.presentFound(fast);
    const auto buttons = dialog.findChildren<QPushButton *>(QStringLiteral("findResult"));
    QCOMPARE(buttons.size(), 2);
    buttons.last()->click();
    QCOMPARE(dialog.config().rotors.at(0).baud, 9600);
    QVERIFY(dialog.config().rotors.at(0).baud != 19200);
}

void TestSettings::readNetworkStaysOffUntilSerialIsUp()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    SettingsDialog dialog(AppConfig::defaults(), nullptr, nullptr);
    auto *readNetwork = dialog.findChild<QPushButton *>(QStringLiteral("readNetwork"));
    QVERIFY(readNetwork);
    QVERIFY(!readNetwork->isEnabled());
}

#ifdef Q_OS_UNIX
void TestSettings::liveSerialFindStopsAndResumesAfterTheProbe()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    MockRt21Server server;
    server.sim()->setHeading(14);
    const QString pty = server.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");

    AppConfig config = AppConfig::defaults();
    config.rotors[0].transport = RotorEndpoint::Transport::Serial;
    config.rotors[0].serialPort = pty;
    config.rotors[0].baud = 4800;
    MainWindow window(config, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(window.driver()->heading().has_value(), 3000);
    auto *readNetwork = window.settingsDialog()->findChild<QPushButton *>(QStringLiteral("readNetwork"));
    QVERIFY(readNetwork);
    QVERIFY(readNetwork->isEnabled());

    bool settled = false;
    bool released = false;
    bool resumedEarly = false;
    QObject::connect(window.settingsDialog(), &SettingsDialog::findProbeSettled, &window, [&]() { settled = true; });
    QObject::connect(window.driver(), &Rt21Driver::linkChanged, &window,
                     [&](Rt21Driver::Link state, const QString &) {
                         if (state == Rt21Driver::Link::Off)
                             released = true;
                         if (released && state == Rt21Driver::Link::Connecting && !settled)
                             resumedEarly = true;
                     });
    auto *find = window.settingsDialog()->findChild<QPushButton *>(QStringLiteral("findRt21"));
    QVERIFY(find);
    find->click();
    QTRY_VERIFY_WITH_TIMEOUT(server.sim()->trace().contains(QByteArray(";")), 2000);
    QCOMPARE(window.driver()->link(), Rt21Driver::Link::Off);
    find->click();
    QTRY_VERIFY_WITH_TIMEOUT(settled, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 5000);
    QVERIFY(!resumedEarly);
}
#endif

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
