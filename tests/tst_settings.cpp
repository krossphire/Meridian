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
#include "sources/CredentialStore.h"
#include "sources/QrzClient.h"

#include "GlobeWidget.h"
#include "core/Gridsquare.h"
#include "map/Dxcc.h"
#include "StatusTone.h"
#include "StatusTone.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSerialPort>
#include <QSettings>
#include <QUdpSocket>
#include <QSignalSpy>
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
    void freshStartSendsNothing();
    void compassRowSendsFixedBearings();
    void beamButtonShowsBeamingThenYields();
    void tcpConfigTurnsNetworkConnectionOn();
    void tcpConnectTurnsNetworkConnectionOn();
    void qrzPasswordIsNotWrittenToIni();
    void sourcesAndQrzRoundTrip();
    void findReturnsToIdleWhenLookupFails();
    void qrzLatLonReplacesPrefixBearing();
    void homeCoordinatesDriveTheBearing();
    void findButtonDismisses();
    void longWayAroundKeepsTheBeam();
    void passiveLookupStaysQuiet();
    void qlogGridBeatsAzimuth();
#ifdef Q_OS_UNIX
    void liveSerialFindStopsAndResumesAfterTheProbe();
    void networkConnectionDefaultsOffAndBlocksTheModule();
    void scanNetworkRestoresTcpBeforeTheProbe();
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

void TestSettings::freshStartSendsNothing()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    QSettings native(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("K5PO"),
                     QStringLiteral("Meridian"));
    native.setValue(QStringLiteral("rotors/1/host"), QStringLiteral("192.168.1.106"));
    native.setValue(QStringLiteral("rotors/1/tcpPort"), 6555);
    native.setValue(QStringLiteral("rotors/1/transport"), QStringLiteral("tcp"));
    native.setValue(QStringLiteral("rotors/1/serialPort"), QStringLiteral("COM4"));
    native.sync();

    const AppConfig config = UserFiles::load();
    QVERIFY(config.rotors.at(0).host.isEmpty());
    QCOMPARE(config.rotors.at(0).tcpPort, 0);
    QVERIFY(config.rotors.at(0).serialPort.isEmpty());
    QCOMPARE(config.homeText, QStringLiteral("EM26"));
    MainWindow window(config, nullptr);
    QCOMPARE(window.driver()->link(), Rt21Driver::Link::Off);
    QTest::qWait(300);
    QCOMPARE(window.driver()->link(), Rt21Driver::Link::Off);
    QCOMPARE(Rt21Driver::transportsOpen(), 0);

    SettingsDialog *dialog = window.settingsDialog();
    auto *host = dialog->findChild<QLineEdit *>();
    bool sawHost = false;
    const auto edits = dialog->findChildren<QLineEdit *>();
    for (QLineEdit *edit : edits) {
        if (edit->placeholderText() == QStringLiteral("Host name or IP")) {
            sawHost = true;
            QVERIFY(edit->text().isEmpty());
        }
    }
    QVERIFY(sawHost);
    Q_UNUSED(host);
    bool sawPort = false;
    for (QSpinBox *spin : dialog->findChildren<QSpinBox *>()) {
        if (spin->maximum() == 65535) {
            sawPort = true;
            QCOMPARE(spin->value(), 0);
        }
    }
    QVERIFY(sawPort);
    auto *home = dialog->findChild<QLineEdit *>(QStringLiteral("homeEdit"));
    QVERIFY(home);
    QCOMPARE(home->text(), QStringLiteral("EM26"));
    native.clear();
    native.sync();
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

void TestSettings::tcpConfigTurnsNetworkConnectionOn()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    MainWindow window(AppConfig::defaults(), nullptr);
    SettingsDialog *dialog = window.settingsDialog();
    auto *transport = dialog->findChild<QComboBox *>(QStringLiteral("transportPick"));
    auto *toggle = dialog->findChild<QComboBox *>(QStringLiteral("networkConnection"));
    QVERIFY(transport);
    QVERIFY(toggle);
    QCOMPARE(toggle->currentText(), QStringLiteral("No"));
    transport->setCurrentIndex(1);
    QCOMPARE(toggle->currentText(), QStringLiteral("No"));
    QLineEdit *host = nullptr;
    for (QLineEdit *edit : dialog->findChildren<QLineEdit *>()) {
        if (edit->placeholderText() == QStringLiteral("Host name or IP"))
            host = edit;
    }
    QVERIFY(host);
    host->setText(QStringLiteral("127.0.0.1"));
    QSpinBox *port = nullptr;
    for (QSpinBox *spin : dialog->findChildren<QSpinBox *>()) {
        if (spin->maximum() == 65535)
            port = spin;
    }
    QVERIFY(port);
    port->setValue(9);
    QCOMPARE(toggle->currentText(), QStringLiteral("Yes"));
    QCOMPARE(dialog->config().networkConnection, true);
    dialog->close();
    QCOMPARE(UserFiles::load().networkConnection, true);
    QFile ini(UserFiles::settingsPath());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(ini.readAll());
    QVERIFY(text.contains(QStringLiteral("networkConnection=true")));
    QVERIFY(!text.contains(QStringLiteral("HKEY"), Qt::CaseInsensitive));
}

void TestSettings::tcpConnectTurnsNetworkConnectionOn()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    MockRt21Server server;
    QVERIFY(server.listenTcp(0));
    AppConfig config = AppConfig::defaults();
    QVERIFY(!config.networkConnection);
    config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
    config.rotors[0].host = QStringLiteral("127.0.0.1");
    config.rotors[0].tcpPort = int(server.port());
    MainWindow window(config, nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 3000);
    QCOMPARE(UserFiles::load().networkConnection, true);
    auto *toggle = window.settingsDialog()->findChild<QComboBox *>(QStringLiteral("networkConnection"));
    QVERIFY(toggle);
    QCOMPARE(toggle->currentText(), QStringLiteral("Yes"));
    QVERIFY(window.driver()->networkModuleEnabled());
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
    config.networkConnection = true;
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

void TestSettings::networkConnectionDefaultsOffAndBlocksTheModule()
{
    QCOMPARE(AppConfig::defaults().networkConnection, false);
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    QCOMPARE(UserFiles::load().networkConnection, false);
    QVERIFY(!QFileInfo::exists(UserFiles::settingsPath()));

    MockRt21Server serial;
    serial.sim()->setHeading(20);
    serial.sim()->setBenchModuleLines(true);
    const QString pty = serial.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");

    AppConfig config = AppConfig::defaults();
    config.rotors[0].transport = RotorEndpoint::Transport::Serial;
    config.rotors[0].serialPort = pty;
    config.rotors[0].baud = 4800;
    MainWindow window(config, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(window.driver()->heading().has_value(), 3000);
    QVERIFY(!window.driver()->networkModuleEnabled());

    SettingsDialog *dialog = window.settingsDialog();
    auto *toggle = dialog->findChild<QComboBox *>(QStringLiteral("networkConnection"));
    auto *readNetwork = dialog->findChild<QPushButton *>(QStringLiteral("readNetwork"));
    auto *writeGateway = dialog->findChild<QPushButton *>(QStringLiteral("write-net-agw"));
    QVERIFY(toggle);
    QVERIFY(readNetwork);
    QVERIFY(writeGateway);
    QCOMPARE(toggle->currentText(), QStringLiteral("No"));
    QVERIFY(readNetwork->isHidden());
    QVERIFY(!readNetwork->isEnabled());
    QVERIFY(writeGateway->isHidden());
    QVERIFY(!writeGateway->isEnabled());

    serial.sim()->clearTrace();
    QSignalSpy blocked(window.driver(), &Rt21Driver::networkFinished);
    window.driver()->readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(blocked.count(), 1, 1000);
    QCOMPARE(blocked.at(0).at(1).toString(), QStringLiteral("Network Connection is off."));
    QVERIFY(!serial.sim()->trace().contains(QByteArray("AT")));
    QVERIFY(!serial.sim()->trace().contains(QByteArray("T11")));
    QVERIFY(!serial.sim()->trace().contains(QByteArray("T01")));
    QSignalSpy writeBlocked(window.driver(), &Rt21Driver::writeFinished);
    window.driver()->writeNetworkSetting(QStringLiteral("agw"), QStringLiteral("192.168.1.1"),
                                         QStringLiteral("Gateway"));
    QTRY_COMPARE_WITH_TIMEOUT(writeBlocked.count(), 1, 1000);
    QCOMPARE(writeBlocked.at(0).at(1).toString(), QStringLiteral("Network Connection is off."));
    QVERIFY(!serial.sim()->trace().contains(QByteArray("AT*AGW")));

    toggle->setCurrentIndex(0);
    dialog->close();
    QFile ini(UserFiles::settingsPath());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(ini.readAll()).contains(QStringLiteral("networkConnection=false")));
    ini.close();
    QCOMPARE(UserFiles::load().networkConnection, false);

    dialog = window.settingsDialog();
    toggle = dialog->findChild<QComboBox *>(QStringLiteral("networkConnection"));
    QVERIFY(toggle);
    toggle->setCurrentIndex(1);
    QVERIFY(window.driver()->networkModuleEnabled());
    readNetwork = dialog->findChild<QPushButton *>(QStringLiteral("readNetwork"));
    QVERIFY(readNetwork);
    QVERIFY(!readNetwork->isHidden());
    QVERIFY(readNetwork->isEnabled());

    toggle->setCurrentIndex(0);
    QVERIFY(!window.driver()->networkModuleEnabled());
    window.driver()->setNetworkModuleEnabled(true);
    QSignalSpy read(window.driver(), &Rt21Driver::networkFinished);
    window.driver()->readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(read.count(), 1, 20000);
    QVERIFY(read.at(0).at(0).toBool());
    QCOMPARE(UserFiles::load().networkConnection, true);
    QCOMPARE(toggle->currentText(), QStringLiteral("Yes"));
    QVERIFY(window.driver()->networkModuleEnabled());
}

void TestSettings::scanNetworkRestoresTcpBeforeTheProbe()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    MockRt21Server serial;
    serial.sim()->setBenchModuleLines(true);
    serial.sim()->setColdBootTcp(true);
    serial.sim()->setHeading(11.5);
    const QString pty = serial.listenPty();
    if (pty.isEmpty())
        QSKIP("pty was not available");

    MockRt21Server tcp;
    tcp.sim()->setHeading(24.1);
    QVERIFY(tcp.listenTcp(0));

    AppConfig config = AppConfig::defaults();
    config.networkConnection = true;
    config.rotors[0].transport = RotorEndpoint::Transport::Serial;
    config.rotors[0].serialPort = pty;
    config.rotors[0].baud = 4800;
    MainWindow window(config, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(window.driver()->heading().has_value(), 3000);

    QSignalSpy readDone(window.driver(), &Rt21Driver::networkFinished);
    window.driver()->readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(readDone.count(), 1, 40000);
    QVERIFY(readDone.at(0).at(0).toBool());
    QVERIFY(window.driver()->hasPendingTcpRestore());

    SettingsDialog *dialog = window.settingsDialog();
    auto *host = dialog->findChild<QLineEdit *>(QStringLiteral("customHost"));
    auto *port = dialog->findChild<QLineEdit *>(QStringLiteral("customPort"));
    auto *scan = dialog->findChild<QPushButton *>(QStringLiteral("scanNetwork"));
    auto *find = dialog->findChild<QPushButton *>(QStringLiteral("findRt21"));
    QVERIFY(host);
    QVERIFY(port);
    QVERIFY(scan);
    QVERIFY(find);
    host->setText(QStringLiteral("127.0.0.1"));
    port->setText(QString::number(tcp.port()));

    const int t01Before = serial.sim()->trace().count(QByteArray("T01"));
    scan->click();
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::Off, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(serial.sim()->trace().count(QByteArray("T01")), t01Before + 1, 1000);
    QCOMPARE(tcp.accepts(), 0);
    find->click();
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 5000);
    QTest::qWait(1500);
    QCOMPARE(tcp.accepts(), 0);
    QCOMPARE(window.driver()->linkEndpoint().serialPort, pty);
    QVERIFY(!window.driver()->hasPendingTcpRestore());

    window.driver()->readNetwork();
    QTRY_COMPARE_WITH_TIMEOUT(readDone.count(), 2, 40000);
    QVERIFY(readDone.at(1).at(0).toBool());
    QVERIFY(window.driver()->hasPendingTcpRestore());

    QElapsedTimer clock;
    clock.start();
    scan->click();
    QTRY_VERIFY_WITH_TIMEOUT(tcp.accepts() > 0, 15000);
    QVERIFY2(clock.elapsed() >= 1000, qPrintable(QString::number(clock.elapsed())));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QPushButton *>(QStringLiteral("findResult")) != nullptr, 8000);
    auto *hit = dialog->findChild<QPushButton *>(QStringLiteral("findResult"));
    QVERIFY(hit->text().contains(QStringLiteral("127.0.0.1")));
    QVERIFY(hit->text().contains(QString::number(tcp.port())));
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 8000);
    QCOMPARE(window.driver()->linkEndpoint().transport, RotorEndpoint::Transport::Serial);
    QCOMPARE(window.driver()->linkEndpoint().serialPort, pty);
    QVERIFY(!window.driver()->hasPendingTcpRestore());
    QCOMPARE(serial.sim()->trace().count(QByteArray("T01")), t01Before + 2);
    QVERIFY(serial.sim()->trace().contains(QByteArray("T11")));
}
#endif

void TestSettings::qrzPasswordIsNotWrittenToIni()
{
    CredentialStore::useMemoryForTests(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserFiles::setRoot(dir.path());
    AppConfig config = AppConfig::defaults();
    QSettings settings(dir.filePath(QStringLiteral("shot.ini")), QSettings::IniFormat);
    SettingsDialog dialog(config, nullptr, nullptr);
    auto *user = dialog.findChild<QLineEdit *>(QStringLiteral("qrzUsername"));
    auto *pass = dialog.findChild<QLineEdit *>(QStringLiteral("qrzPassword"));
    QVERIFY(user && pass);
    QCOMPARE(pass->echoMode(), QLineEdit::Password);
    user->setText(QStringLiteral("K5PO"));
    pass->setText(QStringLiteral("s3cret-not-for-ini"));
    QObject::connect(&dialog, &SettingsDialog::configSaved, &dialog, [](const AppConfig &saved) {
        UserFiles::save(saved);
    });
    auto *box = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(box);
    box->button(QDialogButtonBox::Save)->click();
    QFile file(UserFiles::settingsPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(file.readAll());
    QVERIFY(body.contains(QStringLiteral("K5PO")));
    QVERIFY(!body.contains(QStringLiteral("s3cret-not-for-ini")));
    QVERIFY(!body.contains(QStringLiteral("password="), Qt::CaseInsensitive));
    QCOMPARE(CredentialStore::load(QStringLiteral("K5PO")), QStringLiteral("s3cret-not-for-ini"));
    CredentialStore::useMemoryForTests(false);
}

namespace {

bool traceHasGoto(MockRt21Server &server, double azimuth)
{
    const QByteArray want = QByteArray("AP1") + QString::asprintf("%05.1f", azimuth).toLatin1() + QByteArray("\r");
    return server.sim()->trace().contains(want);
}

} // namespace

void TestSettings::compassRowSendsFixedBearings()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    MockRt21Server server;
    QVERIFY(server.listenTcp(0));

    AppConfig config = AppConfig::defaults();
    config.compassButtons = true;
    config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
    config.rotors[0].host = QStringLiteral("127.0.0.1");
    config.rotors[0].tcpPort = int(server.port());

    MainWindow window(config, nullptr);
    window.resize(900, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(server.hasClient(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 3000);

    auto *globe = window.findChild<GlobeWidget *>();
    QVERIFY(globe);
    const char *names[] = {"compassN", "compassNE", "compassE", "compassSE",
                           "compassS", "compassSW", "compassW", "compassNW"};
    const double bearings[] = {0, 45, 90, 135, 180, 225, 270, 315};
    for (int i = 0; i < 8; ++i) {
        auto *button = globe->findChild<QAbstractButton *>(QString::fromLatin1(names[i]));
        QVERIFY2(button, names[i]);
        QVERIFY2(button->isVisible(), names[i]);
        const QPoint center = button->rect().center();
        QCOMPARE(globe->childAt(button->mapTo(globe, center)), static_cast<QWidget *>(button));
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(window.driver()->target(), std::optional<double>(bearings[i]), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(traceHasGoto(server, bearings[i]), 4000);
    }
}

void TestSettings::beamButtonShowsBeamingThenYields()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    MockRt21Server server;
    QVERIFY(server.listenTcp(0));
    server.sim()->setHeading(0);
    server.sim()->setSpeed(15);

    AppConfig config = AppConfig::defaults();
    config.compassButtons = true;
    config.loggerBeamButton = true;
    config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
    config.rotors[0].host = QStringLiteral("127.0.0.1");
    config.rotors[0].tcpPort = int(server.port());

    MainWindow window(config, nullptr);
    window.resize(900, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY_WITH_TIMEOUT(server.hasClient(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(window.driver()->link(), Rt21Driver::Link::On, 3000);

    auto *globe = window.findChild<GlobeWidget *>();
    QVERIFY(globe);
    globe->setBeamOffer(QStringLiteral("C91RU"), 92.4);
    auto *button = globe->findChild<QAbstractButton *>(QStringLiteral("beamButton"));
    QVERIFY(button);
    QVERIFY(button->isVisible());
    QCOMPARE(button->text(), QStringLiteral("Beam toward:\nC91RU at 92.4°?"));

    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(traceHasGoto(server, 92.4), 4000);
    QCOMPARE(button->text(), QStringLiteral("Beaming toward:\nC91RU"));
    QCOMPARE(button->property("fill").value<QColor>(), onHeadingGreen());
    QVERIFY(button->isVisible());

    // The mock is turning toward 92°. Those reports are Meridian's own move.
    QTRY_VERIFY_WITH_TIMEOUT(window.driver()->heading() && *window.driver()->heading() > 2.0
                                 && *window.driver()->heading() < 80.0,
                             4000);
    QVERIFY(button->isVisible());
    QCOMPARE(button->text(), QStringLiteral("Beaming toward:\nC91RU"));
    server.sim()->setFrozen(true);
    server.sim()->setHeading(40);

    // A heading that keeps closing on the beam stays. A couple of degrees of
    // jitter stays. The long way around a limit grows the gap while the rotor
    // is still moving toward the beam, so that does not hide the status.
    // Once the move has settled, a heading that is not the beam does.
    globe->setLink(GlobeWidget::Link::Moving, QStringLiteral("Turning"));
    globe->setTarget(92.4);
    globe->setHeading(92.4);
    QVERIFY(button->isVisible());
    globe->setHeading(93.9);
    QVERIFY(button->isVisible());
    globe->setHeading(200.0);
    QVERIFY(button->isVisible());
    QCOMPARE(button->text(), QStringLiteral("Beaming toward:\nC91RU"));
    globe->setLink(GlobeWidget::Link::On, QStringLiteral("Stopped"));
    globe->setHeading(110.0);
    QVERIFY(!button->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(window.driver()->heading() && std::abs(*window.driver()->heading() - 40.0) < 1.0, 3000);

    globe->setBeamOffer(QStringLiteral("K5PO"), 45.0);
    QVERIFY(button->isVisible());
    QCOMPARE(button->text(), QStringLiteral("Beam toward:\nK5PO at 45.0°?"));
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(traceHasGoto(server, 45.0), 4000);
    QCOMPARE(button->text(), QStringLiteral("Beaming toward:\nK5PO"));

    QSignalSpy extra(globe, &GlobeWidget::bearingRequested);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(extra.size(), 0);
    QCOMPARE(button->text(), QStringLiteral("Beaming toward:\nK5PO"));

    auto *east = globe->findChild<QAbstractButton *>(QStringLiteral("compassE"));
    QVERIFY(east);
    QTest::mouseClick(east, Qt::LeftButton);
    QVERIFY(!button->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(traceHasGoto(server, 90.0), 4000);

    globe->setBeamOffer(QStringLiteral("C91RU"), 92.4);
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(button->text(), QStringLiteral("Beaming toward:\nC91RU"), 4000);
    globe->openHeadingEditor();
    auto *edit = globe->findChild<QLineEdit *>(QStringLiteral("headingEdit"));
    QVERIFY(edit);
    edit->setText(QStringLiteral("10"));
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(!button->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(traceHasGoto(server, 10.0), 4000);

    globe->setBeamOffer(QStringLiteral("C91RU"), 92.4);
    QTest::mouseClick(button, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(button->text(), QStringLiteral("Beaming toward:\nC91RU"), 4000);
    const QPoint eastOnDisc = globe->rect().center() + QPoint(120, 0);
    QTest::mouseClick(globe, Qt::LeftButton, Qt::NoModifier, eastOnDisc);
    QVERIFY(!button->isVisible());
}

void TestSettings::sourcesAndQrzRoundTrip()
{
    CredentialStore::useMemoryForTests(true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    UserFiles::setRoot(dir.path());

    AppConfig config = AppConfig::defaults();
    SettingsDialog dialog(config, nullptr, nullptr);
    auto *user = dialog.findChild<QLineEdit *>(QStringLiteral("qrzUsername"));
    auto *pass = dialog.findChild<QLineEdit *>(QStringLiteral("qrzPassword"));
    QVERIFY(user && pass);
    user->setText(QStringLiteral("K5PO"));
    pass->setText(QStringLiteral("s3cret-not-for-ini"));
    auto *wsjtPort = dialog.findChild<QLineEdit *>(QStringLiteral("wsjtPort"));
    auto *wsjtGroup = dialog.findChild<QLineEdit *>(QStringLiteral("wsjtMulticast"));
    auto *n1mmPort = dialog.findChild<QLineEdit *>(QStringLiteral("n1mmPort"));
    auto *dxKeeper = dialog.findChild<QCheckBox *>(QStringLiteral("dxKeeper"));
    auto *qlogPort = dialog.findChild<QLineEdit *>(QStringLiteral("qlogPort"));
    QVERIFY(wsjtPort && wsjtGroup && n1mmPort && dxKeeper && qlogPort);
    wsjtPort->setText(QStringLiteral("2238"));
    wsjtGroup->setText(QStringLiteral("239.255.0.1"));
    n1mmPort->setText(QStringLiteral("12060"));
    dxKeeper->setChecked(true);
    QVERIFY(dxKeeper->isChecked());
    qlogPort->setText(QStringLiteral("2333"));
    bool wrote = false;
    QObject::connect(&dialog, &SettingsDialog::configSaved, &dialog, [&](const AppConfig &saved) {
        wrote = UserFiles::save(saved);
    });
    auto *box = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(box);
    box->button(QDialogButtonBox::Save)->click();
    QVERIFY(wrote);

    QFile file(UserFiles::settingsPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(body.contains(QStringLiteral("K5PO")));
    QVERIFY(body.contains(QStringLiteral("2238")));
    QVERIFY(body.contains(QStringLiteral("239.255.0.1")));
    QVERIFY(body.contains(QStringLiteral("12060")));
    QVERIFY(body.contains(QStringLiteral("2333")));
    QVERIFY(!body.contains(QStringLiteral("s3cret-not-for-ini")));
    QVERIFY(!body.contains(QStringLiteral("password="), Qt::CaseInsensitive));
    QCOMPARE(CredentialStore::serviceName(), QStringLiteral("Meridian QRZ"));
    QCOMPARE(CredentialStore::load(QStringLiteral("K5PO")), QStringLiteral("s3cret-not-for-ini"));

    const AppConfig loaded = UserFiles::load();
    QCOMPARE(loaded.dxKeeper, true);
    SettingsDialog reopened(loaded, nullptr, nullptr);
    auto *userAgain = reopened.findChild<QLineEdit *>(QStringLiteral("qrzUsername"));
    auto *passAgain = reopened.findChild<QLineEdit *>(QStringLiteral("qrzPassword"));
    QVERIFY(userAgain && passAgain);
    QCOMPARE(userAgain->text(), QStringLiteral("K5PO"));
    QCOMPARE(passAgain->text(), QString());
    QCOMPARE(passAgain->placeholderText(), QStringLiteral("••••• saved"));
    QCOMPARE(reopened.findChild<QLineEdit *>(QStringLiteral("wsjtPort"))->text(), QStringLiteral("2238"));
    QCOMPARE(reopened.findChild<QLineEdit *>(QStringLiteral("wsjtMulticast"))->text(), QStringLiteral("239.255.0.1"));
    QCOMPARE(reopened.findChild<QLineEdit *>(QStringLiteral("n1mmPort"))->text(), QStringLiteral("12060"));
    QCOMPARE(reopened.findChild<QCheckBox *>(QStringLiteral("dxKeeper"))->isChecked(), true);
    QCOMPARE(reopened.findChild<QLineEdit *>(QStringLiteral("qlogPort"))->text(), QStringLiteral("2333"));

    CredentialStore::failStoresForTests(true);
    user->setText(QStringLiteral("NOCALL"));
    pass->setText(QStringLiteral("nope"));
    box->button(QDialogButtonBox::Save)->click();
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("setupStatus"));
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("Couldn't save the QRZ password")));
    QVERIFY(status->styleSheet().contains(statusToneColor(StatusTone::Failure).name()));
    QCOMPARE(pass->placeholderText(), QStringLiteral("Password"));
    QVERIFY(CredentialStore::load(QStringLiteral("NOCALL")).isEmpty());
    CredentialStore::failStoresForTests(false);

    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    MainWindow window(AppConfig::defaults(), nullptr);
    SettingsDialog *settings = window.settingsDialog();
    auto *save = settings->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
    QVERIFY(save);
    save->click();
    auto *windowStatus = settings->findChild<QLabel *>(QStringLiteral("setupStatus"));
    QVERIFY(windowStatus);
    QVERIFY(windowStatus->text().contains(QStringLiteral("Couldn't write meridian.ini.")));
    QVERIFY(windowStatus->styleSheet().contains(statusToneColor(StatusTone::Failure).name()));
    QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    CredentialStore::useMemoryForTests(false);
}

void TestSettings::findReturnsToIdleWhenLookupFails()
{
    CredentialStore::useMemoryForTests(true);
    QrzClient::failNextLookupForTests(true);
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    AppConfig config = AppConfig::defaults();
    config.qrzUsername = QStringLiteral("K5PO");
    CredentialStore::store(QStringLiteral("K5PO"), QStringLiteral("s3cret-not-for-ini"));

    QFile dxccNames(QString::fromLatin1(MERIDIAN_DATA_DIR) + QStringLiteral("/dxcc-names.tsv"));
    QFile dxccBin(QString::fromLatin1(MERIDIAN_DATA_DIR) + QStringLiteral("/dxcc.bin"));
    QVERIFY(dxccNames.open(QIODevice::ReadOnly));
    QVERIFY(dxccBin.open(QIODevice::ReadOnly));
    QVERIFY(loadDxcc(dxccBin.readAll(), dxccNames.readAll()));

    MainWindow window(config, nullptr);
    window.resize(900, 900);
    window.show();
    auto *globe = window.findChild<GlobeWidget *>();
    QVERIFY(globe);
    QSignalSpy turns(globe, &GlobeWidget::bearingRequested);
    globe->revealCallsign();
    auto *edit = globe->findChild<QLineEdit *>(QStringLiteral("callsignEdit"));
    auto *find = globe->findChild<QAbstractButton *>(QStringLiteral("findCall"));
    QVERIFY(edit && find);
    QVERIFY(find->isVisible());
    QCOMPARE(find->text(), QStringLiteral("Find >"));
    QVERIFY(find->isEnabled());
    edit->setText(QStringLiteral("KL7AA"));
    QTest::mouseClick(find, Qt::LeftButton);

    QCOMPARE(find->text(), QStringLiteral("Find >"));
    QVERIFY(find->isEnabled());
    QCOMPARE(globe->statusText(), QStringLiteral("QRZ lookup failed"));
    QCOMPARE(globe->statusToneValue(), StatusTone::Failure);
    QCOMPARE(turns.size(), 0);
    auto *beam = globe->findChild<QAbstractButton *>(QStringLiteral("beamButton"));
    QVERIFY(beam);
    QVERIFY(beam->isVisible());
    QVERIFY(beam->text().startsWith(QStringLiteral("Beam toward:")));
    QVERIFY(beam->text().contains(QStringLiteral("KL7AA")));
    QVERIFY(!window.driver() || window.driver()->link() == Rt21Driver::Link::Off);
    QVERIFY(!window.driver() || !window.driver()->target());

    QrzClient::failNextLookupForTests(false);
    CredentialStore::forget(QStringLiteral("K5PO"));
    globe->revealCallsign();
    edit->setText(QStringLiteral("KL7AA"));
    find = globe->findChild<QAbstractButton *>(QStringLiteral("findCall"));
    QTest::mouseClick(find, Qt::LeftButton);
    QCOMPARE(find->text(), QStringLiteral("Find >"));
    QVERIFY(find->isEnabled());
    QCOMPARE(globe->statusText(), QStringLiteral("QRZ login needed"));
    QCOMPARE(globe->statusToneValue(), StatusTone::Busy);
    QCOMPARE(turns.size(), 0);

    CredentialStore::useMemoryForTests(false);
}

void TestSettings::qrzLatLonReplacesPrefixBearing()
{
    CredentialStore::useMemoryForTests(true);
    CredentialStore::store(QStringLiteral("K5PO"), QStringLiteral("s3cret-not-for-ini"));
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    QFile dxccNames(QString::fromLatin1(MERIDIAN_DATA_DIR) + QStringLiteral("/dxcc-names.tsv"));
    QFile dxccBin(QString::fromLatin1(MERIDIAN_DATA_DIR) + QStringLiteral("/dxcc.bin"));
    QVERIFY(dxccNames.open(QIODevice::ReadOnly));
    QVERIFY(dxccBin.open(QIODevice::ReadOnly));
    QVERIFY(loadDxcc(dxccBin.readAll(), dxccNames.readAll()));

    AppConfig config = AppConfig::defaults();
    config.qrzUsername = QStringLiteral("K5PO");
    config.homeText = QStringLiteral("EM26ul");
    QrzClient::holdScriptedLookupForTests(true);
    QrzClient::scriptNextLookupForTests(36.43, -94.27, QStringLiteral("EM26ul"), true);

    MainWindow window(config, nullptr);
    window.resize(900, 900);
    window.show();
    auto *globe = window.findChild<GlobeWidget *>();
    QVERIFY(globe);
    QSignalSpy turns(globe, &GlobeWidget::bearingRequested);
    globe->revealCallsign();
    auto *edit = globe->findChild<QLineEdit *>(QStringLiteral("callsignEdit"));
    auto *find = globe->findChild<QAbstractButton *>(QStringLiteral("findCall"));
    QVERIFY(edit && find);
    edit->setText(QStringLiteral("KL7AA"));
    QTest::mouseClick(find, Qt::LeftButton);

    auto *beam = globe->findChild<QAbstractButton *>(QStringLiteral("beamButton"));
    auto *note = globe->findChild<QLabel *>(QStringLiteral("beamApprox"));
    QVERIFY(beam && beam->isVisible());
    QVERIFY(note && note->isVisible());
    QCOMPARE(note->text(), QStringLiteral("approx. — country center"));
    const QString coarse = beam->text();

    QrzClient::releaseScriptedLookupForTests();
    QVERIFY(beam->isVisible());
    QVERIFY(beam->text() != coarse);
    QVERIFY(beam->text().contains(QStringLiteral("KL7AA")));
    double expect = 0;
    QVERIFY(Gridsquare::bearingTo(globe->homeLat(), globe->homeLon(), 36.43, -94.27, expect));
    QVERIFY(beam->text().contains(QString::number(expect, 'f', 1)));
    QVERIFY(!note->isVisible());
    QCOMPARE(turns.size(), 0);
    QVERIFY(!window.driver() || window.driver()->link() == Rt21Driver::Link::Off);

    QrzClient::holdScriptedLookupForTests(false);
    CredentialStore::useMemoryForTests(false);
}

void TestSettings::homeCoordinatesDriveTheBearing()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());

    AppConfig config = AppConfig::defaults();
    MainWindow window(config, nullptr);
    window.resize(900, 900);
    window.show();
    SettingsDialog *dialog = window.settingsDialog();
    auto *hint = dialog->findChild<QLabel *>(QStringLiteral("homePrecisionHint"));
    auto *home = dialog->findChild<QLineEdit *>(QStringLiteral("homeEdit"));
    auto *lat = dialog->findChild<QLineEdit *>(QStringLiteral("homeLatitude"));
    auto *lon = dialog->findChild<QLineEdit *>(QStringLiteral("homeLongitude"));
    auto *box = dialog->findChild<QDialogButtonBox *>();
    QVERIFY(hint && home && lat && lon && box);
    QCOMPARE(hint->text(), QStringLiteral(
                              "For accurate beam headings, enter your lat/long or at least a 6-character grid."));
    QVERIFY(hint->styleSheet().contains(statusToneColor(StatusTone::Busy).name(), Qt::CaseInsensitive));
    QVERIFY(lat->text().isEmpty());
    QVERIFY(lon->text().isEmpty());
    QCOMPARE(home->text(), QStringLiteral("EM26"));

    auto *save = box->button(QDialogButtonBox::Save);
    QVERIFY(save);
    home->setText(QStringLiteral("36.25 N 94.25 W"));
    save->click();
    QCOMPARE(window.globeWidget()->hasHome(), true);

    QFile ini(UserFiles::settingsPath());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    const QString saved = QString::fromUtf8(ini.readAll());
    ini.close();
    QVERIFY(saved.contains(QStringLiteral("36.25")));
    QVERIFY(saved.contains(QStringLiteral("-94.25")));

    auto *globe = window.globeWidget();
    globe->revealCallsign();
    auto *edit = globe->findChild<QLineEdit *>(QStringLiteral("callsignEdit"));
    auto *find = globe->findChild<QAbstractButton *>(QStringLiteral("findCall"));
    QVERIFY(edit && find);
    QrzClient::scriptNextLookupForTests(36.0, -94.25, QStringLiteral("EM26"), true);
    edit->setText(QStringLiteral("W1AW"));
    QTest::keyClick(edit, Qt::Key_Return);
    auto *beam = globe->findChild<QAbstractButton *>(QStringLiteral("beamButton"));
    QVERIFY2(beam && beam->isVisible(), qPrintable(beam ? beam->text() : QStringLiteral("missing")));
    QVERIFY(beam->text().contains(QStringLiteral("180.0")));
    QVERIFY(!beam->text().contains(QStringLiteral("187.1")));

    home->setText(QStringLiteral("EM26"));
    lat->setText(QStringLiteral("36.25"));
    lon->setText(QStringLiteral("-94.25"));
    save->click();
    globe->revealCallsign();
    edit = globe->findChild<QLineEdit *>(QStringLiteral("callsignEdit"));
    find = globe->findChild<QAbstractButton *>(QStringLiteral("findCall"));
    QrzClient::scriptNextLookupForTests(36.0, -94.25, QStringLiteral("EM26"), true);
    edit->setText(QStringLiteral("W1AW"));
    QTest::keyClick(edit, Qt::Key_Return);
    QVERIFY(beam->text().contains(QStringLiteral("180.0")));
    QVERIFY(!beam->text().contains(QStringLiteral("110.6")));

    home->setText(QStringLiteral("FN31"));
    QCOMPARE(lat->text(), QString());
    QCOMPARE(lon->text(), QString());
    home->setText(QStringLiteral("JO01"));
    save->click();
    globe->revealCallsign();
    edit = globe->findChild<QLineEdit *>(QStringLiteral("callsignEdit"));
    find = globe->findChild<QAbstractButton *>(QStringLiteral("findCall"));
    edit->setText(QStringLiteral("T1ST KG54"));
    QTest::mouseClick(find, Qt::LeftButton);
    QVERIFY(beam->text().contains(QStringLiteral("152.8")));

    home->setText(QStringLiteral("FN31"));
    save->click();
    QVERIFY(beam->text().contains(QStringLiteral("101.5")));
    QVERIFY(!beam->text().contains(QStringLiteral("152.8")));

    home->clear();
    lat->clear();
    lon->clear();
    save->click();
    auto *note = globe->findChild<QLabel *>(QStringLiteral("beamApprox"));
    QVERIFY(note && note->isVisible());
    QCOMPARE(note->text(), QStringLiteral("Set your grid in Settings"));
    QVERIFY(!beam->isVisible());
    QCOMPARE(globe->hasHome(), false);
}

void TestSettings::findButtonDismisses()
{
    GlobeWidget globe;
    globe.resize(900, 900);
    globe.show();
    globe.revealCallsign();
    auto *edit = globe.findChild<QLineEdit *>(QStringLiteral("callsignEdit"));
    auto *find = globe.findChild<QAbstractButton *>(QStringLiteral("findCall"));
    QVERIFY(edit && find);
    QVERIFY(find->isVisible());
    QTest::keyClick(&globe, Qt::Key_Escape);
    QVERIFY(!edit->isVisible());
    QVERIFY(!find->isVisible());

    globe.revealCallsign();
    edit->setText(QStringLiteral("W1AW FN20"));
    QVERIFY(find->isVisible());
    QTest::mouseClick(find, Qt::LeftButton);
    QVERIFY(!edit->isVisible());
    QVERIFY(!find->isVisible());
}

void TestSettings::longWayAroundKeepsTheBeam()
{
    GlobeWidget globe;
    globe.resize(900, 900);
    globe.show();
    globe.setBeamOffer(QStringLiteral("W1AW"), 10.0);
    globe.setTarget(10.0);
    globe.setLink(GlobeWidget::Link::Moving, QStringLiteral("Turning"));
    auto *button = globe.findChild<QAbstractButton *>(QStringLiteral("beamButton"));
    QVERIFY(button && button->isVisible());
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(button->text(), QStringLiteral("Beaming toward:\nW1AW"));

    globe.setHeading(350.0);
    QVERIFY(button->isVisible());
    globe.setHeading(200.0);
    QVERIFY(button->isVisible());
    QCOMPARE(button->text(), QStringLiteral("Beaming toward:\nW1AW"));
    globe.setHeading(10.0);
    QVERIFY(button->isVisible());

    globe.setTarget(90.0);
    QVERIFY(!button->isVisible());

    globe.setBeamOffer(QStringLiteral("W1AW"), 10.0);
    globe.setTarget(10.0);
    globe.setLink(GlobeWidget::Link::Moving, QStringLiteral("Turning"));
    QTest::mouseClick(button, Qt::LeftButton);
    globe.setHeading(350.0);
    globe.setLink(GlobeWidget::Link::On, QStringLiteral("Stopped"));
    globe.setHeading(200.0);
    QVERIFY(!button->isVisible());
}

void TestSettings::passiveLookupStaysQuiet()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    CredentialStore::useMemoryForTests(true);

    AppConfig config = AppConfig::defaults();
    config.n1mmPort = 23921;
    MainWindow window(config, nullptr);
    window.resize(900, 900);
    window.show();
    auto *globe = window.globeWidget();
    QVERIFY(globe);
    const QString idle = globe->statusText();

    QUdpSocket sender;
    QVERIFY(sender.writeDatagram(QByteArray("<lookupinfo><call>W1AW</call></lookupinfo>"),
                                 QHostAddress::LocalHost, 23921) > 0);
    QTest::qWait(250);
    QCOMPARE(globe->statusText(), idle);
    QVERIFY(globe->statusText() != QStringLiteral("QRZ login needed"));

    QVERIFY(sender.writeDatagram(QByteArray("<lookupinfo><call>W1AW</call><grid>RR73</grid></lookupinfo>"),
                                 QHostAddress::LocalHost, 23921) > 0);
    QTest::qWait(250);
    QCOMPARE(globe->statusText(), idle);

    globe->revealCallsign();
    auto *edit = globe->findChild<QLineEdit *>(QStringLiteral("callsignEdit"));
    auto *find = globe->findChild<QAbstractButton *>(QStringLiteral("findCall"));
    edit->setText(QStringLiteral("W1AW"));
    QTest::mouseClick(find, Qt::LeftButton);
    QCOMPARE(globe->statusText(), QStringLiteral("QRZ login needed"));
    QCOMPARE(globe->statusToneValue(), StatusTone::Busy);

    CredentialStore::store(QStringLiteral("K5PO"), QStringLiteral("s3cret-not-for-ini"));
    AppConfig ready = AppConfig::defaults();
    ready.n1mmPort = 23922;
    ready.qrzUsername = QStringLiteral("K5PO");
    MainWindow refined(ready, nullptr);
    refined.resize(900, 900);
    refined.show();
    auto *refinedGlobe = refined.globeWidget();
    QrzClient::failNextLookupForTests(true);
    QVERIFY(sender.writeDatagram(QByteArray("<lookupinfo><call>W1AW</call><grid>FN20</grid></lookupinfo>"),
                                 QHostAddress::LocalHost, 23922) > 0);
    auto *beam = refinedGlobe->findChild<QAbstractButton *>(QStringLiteral("beamButton"));
    auto *note = refinedGlobe->findChild<QLabel *>(QStringLiteral("beamApprox"));
    QTRY_VERIFY_WITH_TIMEOUT(beam && beam->isVisible() && beam->text().contains(QStringLiteral("W1AW")), 2000);
    QCOMPARE(note->text(), QStringLiteral("approx. — 4-character grid"));
    QTest::qWait(200);
    QVERIFY(refinedGlobe->statusText() != QStringLiteral("QRZ lookup failed"));
    QCOMPARE(refinedGlobe->statusToneValue(), StatusTone::Neutral);
    QVERIFY(beam->isVisible());

    QrzClient::failNextLookupForTests(false);
    QrzClient::holdScriptedLookupForTests(true);
    QrzClient::scriptNextLookupForTests(41.2, -73.4, QStringLiteral("FN20"), true);
    QVERIFY(sender.writeDatagram(QByteArray("<lookupinfo><call>W1BX</call><grid>FN20</grid></lookupinfo>"),
                                 QHostAddress::LocalHost, 23922) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(beam->text().contains(QStringLiteral("W1BX"))
                                 && note->text() == QStringLiteral("approx. — 4-character grid"),
                             2000);
    QrzClient::releaseScriptedLookupForTests();
    QTRY_VERIFY_WITH_TIMEOUT(note->text().isEmpty(), 2000);
    double expect = 0;
    QVERIFY(Gridsquare::bearingTo(refinedGlobe->homeLat(), refinedGlobe->homeLon(), 41.2, -73.4, expect));
    QVERIFY(beam->text().contains(QString::number(expect, 'f', 1)));
    QVERIFY(refinedGlobe->statusText() != QStringLiteral("QRZ lookup failed"));

    QrzClient::holdScriptedLookupForTests(false);
    CredentialStore::forget(QStringLiteral("K5PO"));
    CredentialStore::useMemoryForTests(false);
}

void TestSettings::qlogGridBeatsAzimuth()
{
    QTemporaryDir files;
    QVERIFY(files.isValid());
    UserFiles::setRoot(files.path());
    AppConfig config = AppConfig::defaults();
    config.homeText = QStringLiteral("EM26");
    config.qlogPort = 23923;
    MainWindow window(config, nullptr);
    window.resize(900, 900);
    window.show();
    auto *globe = window.globeWidget();
    QUdpSocket sender;
    const QByteArray both = QByteArray(
        R"({"msgtype":"currentcontact","data":{"callsign":"C91RU","grid":"KG54","azimuth":1.0}})");
    QVERIFY(sender.writeDatagram(both, QHostAddress::LocalHost, 23923) > 0);
    auto *beam = globe->findChild<QAbstractButton *>(QStringLiteral("beamButton"));
    QTRY_VERIFY_WITH_TIMEOUT(beam && beam->isVisible() && beam->text().contains(QStringLiteral("92.4")), 2000);
    QVERIFY(!beam->text().contains(QStringLiteral("1.0")));

    const QByteArray onlyAzimuth = QByteArray(
        R"({"msgtype":"currentcontact","data":{"callsign":"C91RU","grid":"","azimuth":45.0}})");
    QVERIFY(sender.writeDatagram(onlyAzimuth, QHostAddress::LocalHost, 23923) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(beam->text().contains(QStringLiteral("45.0")), 2000);

    const QByteArray bad = QByteArray(
        R"({"msgtype":"currentcontact","data":{"callsign":"C91RU","grid":"nope","azimuth":9999}})");
    QVERIFY(sender.writeDatagram(bad, QHostAddress::LocalHost, 23923) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(beam->text().contains(QStringLiteral("279.0")), 2000);
}

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
