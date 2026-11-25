/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Find an RT-21. USB and COM ports are checked first, and the scan stops at
 * the first port that answers. A TCP scan of the local subnet runs only when
 * those find nothing. A saved IP connection that is not up is replaced by
 * that hit. Any other hit stays a button until it is clicked.
 */

#pragma once

#include "AppConfig.h"
#include "Rt21Protocol.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

struct FoundRotor {
    RotorEndpoint endpoint;
    double heading = 0;
    QString label;
    QString firmware;
};

// Chip family from a USB serial adapter's manufacturer and description.
// Empty when it is not a known USB UART.
QString usbChipName(const QString &manufacturer, const QString &description);

// "RT-21 on COM6 (USB, 4800), heading 132°" or "RT-21 at 192.168.1.20:6555 (network), heading 1°".
QString foundRotorLabel(const QString &place, const QString &kind, int baud, double heading);

// True for VPN, virtual, and tunnel adapters that should not be swept.
bool isVirtualAdapterName(const QString &name);

// Ports to try on each host. Factory numeric defaults are not published; see the protocol note.
// `extra` is a saved or custom port. Order is stable and duplicates are dropped.
QList<int> networkCandidatePorts(const QList<int> &extra = {});

// RT-21 RS-232 is 4800, then 9600. Nothing else is probed.
QList<int> serialProbeBauds();

// Saved TCP is down, and Find confirmed a different endpoint. The live TCP
// session is left alone.
bool findReplacesSavedEndpoint(const RotorEndpoint &saved, bool linkUp, const RotorEndpoint &found);

// A TCP hit keeps the port that answered. A serial hit leaves the saved port as it was.
int tcpPortAfterFind(int savedPort, const RotorEndpoint &found);

// Find stores 4800 or 9600 when that is the baud that answered. Any other
// probe baud (19200) leaves the saved baud alone.
int baudKeptAfterFind(int savedBaud, int foundBaud);

// Qt reports a dropped socket as "Unknown error". Say what happened.
QString plainLinkFailure(const QString &message);

// A close with no real fault should not be logged as error=Unknown error.
QString closeNote(const QString &reason, const QString &errorString);

// The R11 reply, when it names an RT-21. Empty for a DCU-1 or any other device.
QString rt21Firmware(const QByteArray &frame);

struct SerialPortChoice {
    QString port;
    QString label;
};

// COM1, COM2, … COM10, COM110. A saved port that is not present is appended as "COM6 (missing)".
QList<SerialPortChoice> arrangeSerialPorts(QList<SerialPortChoice> present, const QString &saved);

class Rt21Finder
{
public:
    // One harmless BI1; read. Empty if the port is busy, missing, or not an RT-21.
    static std::optional<FoundRotor> probeSerial(const QString &portName, const QString &kind, int baud, int timeoutMs);

    static std::optional<FoundRotor> probeTcp(const QString &host, int port, int timeoutMs);

// Present serial ports, including Edgeport COM1–COM8. Ports that will not open are skipped.
// Stops at the first port that answers. Call on the thread that runs the event loop.
// Production Find uses FindScan instead. `onlyPorts`, when set, is probed in that order.
static QList<FoundRotor> scanSerialPorts(int timeoutMs = 650,
                                         const std::function<void(const FoundRotor &)> &onFound = {},
                                         const std::function<bool()> &cancelled = {},
                                         const QStringList &onlyPorts = {});

    // IPv4 hosts on local /24s of physical adapters, excluding this machine.
    static QStringList localSubnetHosts();

    // Probes host × port with `connectMs` handshakes, 32 at a time, and stops
    // at the first BI1; plus R11; version. Gives up launching new probes after
    // about 14 s. No AT command is sent.
    static QList<FoundRotor> scanHosts(const QStringList &hosts, const QList<int> &ports, int connectMs = 300,
                                       const std::function<void(const FoundRotor &)> &onFound = {},
                                       const std::function<bool()> &cancelled = {});
};

// Port name and the USB/serial kind shown on the result button.
// QSerialPortInfo is destroyed before this returns.
QList<std::pair<QString, QString>> serialFindCandidates();

// The whole Find probe runs on the GUI thread. One QSerialPort at a time:
// open, write BI1;, readyRead or a timeout, close, then the next port and baud.
// A network sweep uses QTcpSocket on this same thread. Hits are delivered by
// value. There is no worker thread and no shared container.
class FindScan : public QObject
{
    Q_OBJECT
public:
    struct Request {
        QList<std::pair<QString, QString>> serial;
        bool doSerial = true;
        bool doNetwork = false;
        QStringList hosts;
        QList<int> ports;
        int serialTimeoutMs = 650;
        int connectMs = 300;
        QList<int> bauds;
    };

    explicit FindScan(QObject *parent = nullptr);
    void start(const Request &request);
    void cancel();
    bool running() const { return running_; }

signals:
    void hitFound(const FoundRotor &found);
    void note(const QString &text);
    void settled();

private:
    struct FindNetProbe;

    void openNext();
    void onReady();
    void onTimeout();
    void afterPortClosed();
    void beginNetwork();
    void launchNetBatch();
    void settleProbe(FindNetProbe *probe, const QByteArray &result);
    void netBatchDone();
    void deliver(const FoundRotor &hit);
    void finish();
    void destroyPort();
    void destroyProbes();
    bool owns(FindNetProbe *probe) const;

    Request request;
    struct Step {
        QString name;
        QString kind;
        int baud = 4800;
    };
    QList<Step> steps;
    int stepIndex = 0;
    bool running_ = false;
    bool cancelled_ = false;
    bool gotHit = false;
    bool awaitingFirmware = false;
    FoundRotor pendingHit;
    class QSerialPort *port = nullptr;
    class QTimer *timer = nullptr;
    QByteArray rx;
    Rt21::Framer framer;
    QList<FindNetProbe *> probes;
    QList<std::pair<QString, int>> netTargets;
    int netIndex = 0;
    bool netActive = false;
    QElapsedTimer netBudget;
};
