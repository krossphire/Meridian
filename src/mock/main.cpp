/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Rt21Simulator.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("meridian-mock"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(MERIDIAN_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Mock Green Heron RT-21 for Meridian."));
    parser.addHelpOption();
    QCommandLineOption portOpt(QStringList{QStringLiteral("tcp")}, QStringLiteral("TCP port (0 for none)."),
                               QStringLiteral("port"), QStringLiteral("6555"));
    QCommandLineOption ptyOpt(QStringLiteral("pty"), QStringLiteral("Also serve the protocol on a pseudo-terminal."));
    QCommandLineOption headingOpt(QStringLiteral("heading"), QStringLiteral("Starting azimuth."), QStringLiteral("deg"),
                                  QStringLiteral("0"));
    QCommandLineOption speedOpt(QStringLiteral("speed"), QStringLiteral("Degrees per second."), QStringLiteral("dps"),
                                QStringLiteral("6"));
    QCommandLineOption frozenOpt(QStringLiteral("frozen"), QStringLiteral("Hold the heading still."));
    parser.addOption(portOpt);
    parser.addOption(ptyOpt);
    parser.addOption(headingOpt);
    parser.addOption(speedOpt);
    parser.addOption(frozenOpt);
    parser.process(app);

    MockRt21Server server;
    server.sim()->setHeading(parser.value(headingOpt).toDouble());
    server.sim()->setSpeed(parser.value(speedOpt).toDouble());
    server.sim()->setFrozen(parser.isSet(frozenOpt));

    QTextStream out(stdout);
    const int port = parser.value(portOpt).toInt();
    if (port >= 0) {
        if (!server.listenTcp(quint16(port))) {
            QTextStream(stderr) << "Could not listen on port " << port << "\n";
            return 1;
        }
        out << "tcp 127.0.0.1:" << server.port() << "\n";
    }
    if (parser.isSet(ptyOpt)) {
        const QString pty = server.listenPty();
        if (pty.isEmpty()) {
            QTextStream(stderr) << "Could not allocate a pty\n";
            return 1;
        }
        out << "pty " << pty << "\n";
    }
    out.flush();
    return app.exec();
}
