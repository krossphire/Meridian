/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Setup, in one tall column. The groups follow the Green Heron utility.
 * The wording, colors, and layout are Meridian's.
 */

#pragma once

#include "rotor/AppConfig.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QSpinBox;
class QVBoxLayout;
class Rt21Driver;

class ControllerColumn : public QWidget
{
    Q_OBJECT
public:
    explicit ControllerColumn(QWidget *parent = nullptr);

    void setDriver(Rt21Driver *driver);
    void setLink(const RotorEndpoint &endpoint);
    void addTail(QWidget *ports, QWidget *network);
    QLabel *versionLabel() const { return version; }
    QLabel *callout() const { return displayCallout; }

signals:
    void readRequested();
    void backupRequested();
    void brightnessWriteRequested(int value);

private:
    void applyField(QChar id, const QString &raw, bool numeric, int value);
    void selectCode(QChar id, int code);

    QVBoxLayout *column = nullptr;
    QLabel *version = nullptr;
    QLabel *displayCallout = nullptr;
    QSpinBox *brightnessSpin = nullptr;
    QLabel *heading = nullptr;
    QLabel *linkLabel = nullptr;
    QLabel *analog = nullptr;
    QLabel *ccwCal = nullptr;
    QLabel *cwCal = nullptr;
    QLabel *values[26] = {};
    QLabel *magnetic = nullptr;
    class QButtonGroup *choices[3] = {};
    QCheckBox *bits[12] = {};
    QWidget *pulseGroup = nullptr;
    QWidget *limits = nullptr;
    Rt21Driver *driver = nullptr;
    bool wired = false;
    int ccwLimit = 0;
    int cwLimit = 0;
    int offset = 0;
};
