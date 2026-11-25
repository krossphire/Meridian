/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "StatusTone.h"
#include "rotor/AppConfig.h"
#include "rotor/Rt21Finder.h"
#include "rotor/UsbHint.h"

#include <QDialog>
#include <QList>
#include <QMap>
#include <QSet>

class QCloseEvent;
class ControllerColumn;
class SetupPanel;

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class Rt21Driver;
class ZipDatabase;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(const AppConfig &config, const ZipDatabase *zips, Rt21Driver *driver, QWidget *parent = nullptr);

    void setConfig(const AppConfig &config);
    void setDriver(Rt21Driver *driver);
    void setNetworkConnection(bool on);
    AppConfig config() const { return draft; }
    void showPage(int index);
    void presentFound(const FoundRotor &found);
    void showUsbFacts(const QList<UsbDeviceFact> &devices);

signals:
    void configSaved(const AppConfig &config);
    // The probe has closed its port. A live serial link is reopened after this.
    void findProbeSettled();

private:
    struct FieldRow {
        QChar id;
        bool writable = false;
        QSpinBox *spin = nullptr;
        QLabel *raw = nullptr;
        int original = 0;
        bool numeric = false;
    };

    void buildStation();
    void buildSources();
    void buildRotors();
    void buildController();
    bool commitDraft(bool interactive);
    void refreshLocation();
    void loadRotor(int index);
    void storeRotor(int index);
    void refreshPorts();
    void showSerialPort(const QString &port);
    void refreshUsbHints();
    void renderUsbHints(const QList<UsbHint> &hints);
    void startFind(bool network);
    void finishFind(const QList<FoundRotor> &found, bool network);
    void applyNetworkConnection();
    void noteTcpEndpoint();
    void onFindSettled();
    void applyFound(const FoundRotor &found);
    void styleFindButton();
    void setStatus(const QString &text);
    void exportSetup();
    void refreshDisplayCallout();
    QString backupConfig(const QString &path) const;

    AppConfig draft;
    const ZipDatabase *zips = nullptr;
    Rt21Driver *driver = nullptr;
    int rotorIndex = -1;

    class QTabWidget *tabs = nullptr;
    class QTabWidget *setupTabs = nullptr;
    SetupPanel *setupPanel = nullptr;
    ControllerColumn *setupColumn = nullptr;
    QLabel *statusLabel = nullptr;
    class QTimer *statusFadeTimer = nullptr;
    QString statusPlain;
    StatusTone statusToneNow = StatusTone::Neutral;
    QMap<QString, QString> eeRaw;
    QLabel *screenSaver = nullptr;
    QLabel *multiplier = nullptr;
    QLabel *magnetic = nullptr;
    QLineEdit *homeEdit = nullptr;
    QLabel *summary = nullptr;
    QLabel *detail = nullptr;
    class QCheckBox *trafficBox = nullptr;
    class QCheckBox *sunBox = nullptr;
    class QCheckBox *compassBox = nullptr;
    class QCheckBox *beamBox = nullptr;
    QLineEdit *qrzUserEdit = nullptr;
    QLineEdit *qrzPassEdit = nullptr;
    bool qrzPassStored = false;
    QLineEdit *wsjtPortEdit = nullptr;
    QLineEdit *wsjtGroupEdit = nullptr;
    QLineEdit *n1mmPortEdit = nullptr;
    class QSpinBox *idleSpin = nullptr;
    QListWidget *rotorList = nullptr;
    QLineEdit *nameEdit = nullptr;
    QComboBox *transport = nullptr;
    QComboBox *networkConnectionBox = nullptr;
    QStackedWidget *transportPages = nullptr;
    QLineEdit *hostEdit = nullptr;
    QLineEdit *customHostEdit = nullptr;
    QLineEdit *customPortEdit = nullptr;
    QSpinBox *portSpin = nullptr;
    QComboBox *portCombo = nullptr;
    QComboBox *baudCombo = nullptr;
    class QPushButton *findButton = nullptr;
    class QPushButton *networkButton = nullptr;
    QLabel *findNote = nullptr;
    QLabel *usbHint = nullptr;
    bool usbBaselineReady = false;
    bool usbFactsLocked = false;
    QSet<QString> usbSeen;
    class QVBoxLayout *findResults = nullptr;
    FindScan *findScan = nullptr;
    bool findStopRequested = false;
    QStringList foundKeys;
    QLabel *versionLabel = nullptr;
    QLabel *displayCallout = nullptr;
    QString versionReport;
    QString potReport;
    QString resetReport;
    QVector<FieldRow> fields;
    class QCheckBox *bits[12] = {};
    bool fillingBits = false;
    bool driverWired = false;
    bool committing = false;
    bool haveSerialHold = false;
    QString serialHold;
    bool continueToNetwork = false;
    bool resumeSerial = false;
    bool findRestoreWait = false;
    bool fillingForm = false;
    bool findSavedHit = false;
    bool havePendingApply = false;
    FoundRotor pendingApply;
    RotorEndpoint resumeEndpoint;

protected:
    void closeEvent(QCloseEvent *event) override;
};
