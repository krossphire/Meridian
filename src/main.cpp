/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MainWindow.h"
#include "mock/Rt21Simulator.h"
#include "rotor/AppConfig.h"
#include "rotor/TrafficLog.h"
#include "rotor/UserFiles.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPainter>
#include <QSettings>

#include <memory>

int runScreenshots(const QString &directory, qreal dpr);

namespace {

QIcon appIcon()
{
    QPixmap pixmap(128, 128);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(16, 22, 27));
    painter.setPen(QPen(QColor(214, 222, 214), 4));
    painter.drawEllipse(QRectF(8, 8, 112, 112));
    painter.setPen(QPen(QColor(255, 191, 0), 6, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(64, 64), QPointF(96, 34));
    painter.setBrush(QColor(255, 191, 0));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(64, 64), 5, 5);
    return QIcon(pixmap);
}

void installFonts()
{
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-Medium.ttf"));
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(14);
    QApplication::setFont(font);
}

void installPalette(QApplication &app)
{
    QPalette palette;
    const QColor window(18, 23, 28);
    const QColor text(232, 236, 234);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, QColor(22, 28, 34));
    palette.setColor(QPalette::AlternateBase, QColor(28, 36, 42));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, QColor(36, 48, 56));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, QColor(58, 92, 78));
    palette.setColor(QPalette::HighlightedText, QColor(244, 246, 244));
    palette.setColor(QPalette::ToolTipBase, QColor(22, 28, 34));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, QColor(140, 152, 148));
    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral(
        "QWidget { color: #e8eeea; }"
        "QDialog, QWidget#settings { background: #12171c; }"
        "QLineEdit#headingEdit { background: transparent; border: none; border-bottom: 2px solid rgba(255,191,0,200);"
        " border-radius: 0; padding: 0; selection-background-color: rgba(255,191,0,70); selection-color: #f7f4ea; }"
        "QLineEdit, QComboBox, QSpinBox, QListWidget, QPlainTextEdit {"
        "  background: #1a2128; border: 1px solid #2c3844; border-radius: 6px; padding: 6px 8px;"
        "  selection-background-color: #3d5a4c; }"
        "QPushButton { background: #24303a; border: 1px solid #33414d; border-radius: 6px; padding: 6px 12px; }"
        "QPushButton:hover { background: #2d3c48; }"
        "QPushButton:flat { border: none; color: #b7c4be; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: transparent; color: #8ea099; padding: 8px 14px; }"
        "QTabBar::tab:selected { color: #f2f6f4; border-bottom: 2px solid #ffbf00; }"
        "QScrollArea { border: none; background: transparent; }"
        "QLabel#hint, QLabel#detailLabel, QLabel#usbHint { color: #8ea099; }"
        "QLabel#usbHint a { color: #ffbf00; }"
        "QCheckBox { spacing: 8px; }"));
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("K5PO"));
    QApplication::setOrganizationDomain(QStringLiteral("k5po.example"));
    QApplication::setApplicationName(QStringLiteral("Meridian"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0-alpha25g"));
    QApplication::setWindowIcon(appIcon());
    installFonts();
    installPalette(app);
    app.setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Rotator controller for the Green Heron RT-21."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption configOpt(QStringList{QStringLiteral("config")},
                                 QStringLiteral("Folder for meridian.ini, logs, and backups."),
                                 QStringLiteral("dir"));
    QCommandLineOption connectOpt(QStringList{QStringLiteral("connect")},
                                  QStringLiteral("Session override, tcp://host:port."), QStringLiteral("url"));
    QCommandLineOption mockOpt(QStringLiteral("mock"), QStringLiteral("Talk to a built-in mock RT-21 on 127.0.0.1."));
    QCommandLineOption shotsOpt(QStringLiteral("screenshots"), QStringLiteral("Write UI screenshots and exit."),
                                QStringLiteral("dir"));
    QCommandLineOption dprOpt(QStringLiteral("dpr"), QStringLiteral("Device pixel ratio for the sharp screenshot."),
                              QStringLiteral("scale"), QStringLiteral("2"));
    QCommandLineOption logOpt(QStringLiteral("log"), QStringLiteral("Record raw rotor TX/RX for this session."));
    parser.addOption(configOpt);
    parser.addOption(connectOpt);
    parser.addOption(mockOpt);
    parser.addOption(shotsOpt);
    parser.addOption(dprOpt);
    parser.addOption(logOpt);
    parser.process(app);

    if (parser.isSet(shotsOpt))
        return runScreenshots(parser.value(shotsOpt), parser.value(dprOpt).toDouble());

    if (parser.isSet(configOpt))
        UserFiles::setRoot(parser.value(configOpt));
    AppConfig config = UserFiles::load();
    if (parser.isSet(logOpt))
        config.trafficLog = true;
    TrafficLog::instance().setEnabled(config.trafficLog);
    MockRt21Server mock;
    if (parser.isSet(mockOpt)) {
        mock.sim()->setHeading(52);
        mock.sim()->setSpeed(30);
        if (!mock.listenTcp(17655))
            return 1;
        if (!config.rotors.isEmpty()) {
            config.rotors[0].transport = RotorEndpoint::Transport::Tcp;
            config.rotors[0].host = QStringLiteral("127.0.0.1");
            config.rotors[0].tcpPort = int(mock.port());
        }
    }
    if (parser.isSet(connectOpt)) {
        const QString url = parser.value(connectOpt);
        const QString rest = url.startsWith(QStringLiteral("tcp://")) ? url.mid(6) : url;
        const int colon = rest.lastIndexOf(':');
        if (colon > 0 && !config.rotors.isEmpty()) {
            config.rotors[config.active].transport = RotorEndpoint::Transport::Tcp;
            config.rotors[config.active].host = rest.left(colon);
            config.rotors[config.active].tcpPort = rest.mid(colon + 1).toInt();
        }
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() { TrafficLog::instance().flush(); });
    MainWindow window(config, nullptr);
    window.show();
    return app.exec();
}
