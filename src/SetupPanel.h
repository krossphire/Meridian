/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Read-only Ports and Network pages. The guide suggests a port and an address.
 * It does not fill the connection fields, and it does not write to the rotor.
 */

#pragma once

#include "rotor/AppConfig.h"

#include <QMap>
#include <QObject>

class QLabel;
class QLineEdit;
class QStackedWidget;
class QWidget;
class Rt21Driver;

class SetupPanel : public QObject
{
    Q_OBJECT
public:
    explicit SetupPanel(QObject *parent = nullptr);

    QWidget *portsPage() const { return ports; }
    QWidget *networkPage() const { return network; }
    void setDriver(Rt21Driver *driver);
    void setEndpoint(const RotorEndpoint &endpoint);
    QMap<QString, QString> networkValues() const;
    void showNetwork();
    void showWizard();

signals:
    void statusMessage(const QString &text);
    void exportRequested();

private:
    void buildPorts();
    void buildNetwork();
    void showWizardStep(int step);
    void remember(const QString &key, const QString &value);
    void setStatus(const QString &text);

    QWidget *ports = nullptr;
    QWidget *network = nullptr;
    Rt21Driver *driver = nullptr;
    RotorEndpoint endpoint;
    bool wired = false;

    QLabel *controllerSpeed = nullptr;
    QLabel *savedLink = nullptr;
    QLabel *localPorts = nullptr;
    QLabel *moduleSerial = nullptr;
    QLabel *gatewayNote = nullptr;

    QMap<QString, QLineEdit *> values;
    QMap<QString, QString> rawNetwork;
    QStackedWidget *stack = nullptr;
    QLabel *wizardTitle = nullptr;
    QLabel *wizardBody = nullptr;
    int wizardStep = 0;
    int suggestedPort = 0;
    QString suggestedAddress;
};
