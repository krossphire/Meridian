/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ControllerColumn.h"

#include "rotor/Rt21Driver.h"
#include "rotor/Rt21Setup.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {

class TravelBar : public QWidget
{
public:
    explicit TravelBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(36);
    }
    void setArc(int ccw, int cw, int center)
    {
        ccwLimit = ccw;
        cwLimit = cw;
        offset = center;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bar(8, 14, width() - 16, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(36, 48, 58));
        painter.drawRoundedRect(bar, 4, 4);
        auto xOf = [&](int degrees) {
            const double t = std::clamp(degrees, 0, 360) / 360.0;
            return bar.left() + t * bar.width();
        };
        const int start = std::clamp(offset - ccwLimit, 0, 360);
        const int end = std::clamp(offset + cwLimit, 0, 360);
        painter.setBrush(QColor(61, 90, 76));
        painter.drawRoundedRect(QRectF(xOf(start), bar.top(), std::max(4.0, xOf(end) - xOf(start)), bar.height()), 4, 4);
        painter.setPen(QColor(142, 160, 153));
        painter.drawText(QRectF(0, 0, width(), 12), Qt::AlignLeft, QStringLiteral("CCW %1").arg(ccwLimit));
        painter.drawText(QRectF(0, 0, width(), 12), Qt::AlignHCenter, QStringLiteral("offset %1").arg(offset));
        painter.drawText(QRectF(0, 0, width(), 12), Qt::AlignRight, QStringLiteral("CW %1").arg(cwLimit));
    }

private:
    int ccwLimit = 0;
    int cwLimit = 0;
    int offset = 0;
};

QGroupBox *makeGroup(const QString &title, QVBoxLayout *column)
{
    auto *box = new QGroupBox(title);
    column->addWidget(box);
    return box;
}

} // namespace

ControllerColumn::ControllerColumn(QWidget *parent)
    : QWidget(parent)
{
    column = new QVBoxLayout(this);
    column->setSpacing(14);
    auto *actions = new QHBoxLayout;
    auto *read = new QPushButton(QStringLiteral("Read from RT-21"));
    read->setObjectName(QStringLiteral("readFromRotor"));
    auto *backup = new QPushButton(QStringLiteral("Save backup"));
    backup->setObjectName(QStringLiteral("saveBackup"));
    actions->addWidget(read);
    actions->addWidget(backup);
    actions->addStretch();
    column->addLayout(actions);
    connect(read, &QPushButton::clicked, this, &ControllerColumn::readRequested);
    connect(backup, &QPushButton::clicked, this, &ControllerColumn::backupRequested);
    auto *note = new QLabel(QStringLiteral(
        "Only display brightness can be written, and only from its Write button. Everything else stays read-only."));
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("hint"));
    column->addWidget(note);

    version = new QLabel(QStringLiteral("Not read yet."));
    version->setObjectName(QStringLiteral("versionLabel"));
    version->setWordWrap(true);
    column->addWidget(version);
    displayCallout = new QLabel;
    displayCallout->setObjectName(QStringLiteral("displayCallout"));
    displayCallout->setWordWrap(true);
    column->addWidget(displayCallout);

    auto addChoice = [&](QGroupBox *box, int which, const QStringList &names) {
        auto *lay = new QVBoxLayout(box);
        choices[which] = new QButtonGroup(box);
        for (int i = 0; i < names.size(); ++i) {
            auto *button = new QRadioButton(names.at(i));
            button->setEnabled(false);
            choices[which]->addButton(button, i);
            lay->addWidget(button);
        }
    };

    addChoice(makeGroup(QStringLiteral("Rotor type"), column), 0,
              {QStringLiteral("Pot"), QStringLiteral("Counter"), QStringLiteral("Ham-x"), QStringLiteral("T2X"),
               QStringLiteral("TIC-PST"), QStringLiteral("Orion"), QStringLiteral("Spid"),
               QStringLiteral("Yaesu G-800/1000"), QStringLiteral("Yaesu AC360"), QStringLiteral("Yaesu AC450"),
               QStringLiteral("Yaesu G-2800"), QStringLiteral("DX Engineering"), QStringLiteral("Sub-Lunar")});
    addChoice(makeGroup(QStringLiteral("Security"), column), 1, {QStringLiteral("User"), QStringLiteral("Admin")});
    addChoice(makeGroup(QStringLiteral("Mode"), column), 2,
              {QStringLiteral("Normal"), QStringLiteral("Master/Slave"), QStringLiteral("Master/CR"),
               QStringLiteral("Slave/CR"), QStringLiteral("Offset"), QStringLiteral("Debug")});

    auto *rotor = makeGroup(QStringLiteral("Rotor"), column);
    auto *rotorLay = new QVBoxLayout(rotor);
    heading = new QLabel(QStringLiteral("Heading —"));
    linkLabel = new QLabel(QStringLiteral("Port —"));
    linkLabel->setWordWrap(true);
    auto *motionNote = new QLabel(QStringLiteral(
        "Point-and-shoot, CCW, CW, and Cancel stay off here. Turn the antenna from the map. "
        "Stop is Esc or Space."));
    motionNote->setWordWrap(true);
    rotorLay->addWidget(heading);
    rotorLay->addWidget(linkLabel);
    rotorLay->addWidget(motionNote);

    auto *limitBox = makeGroup(QStringLiteral("Limits and offset"), column);
    auto *limitLay = new QVBoxLayout(limitBox);
    limits = new TravelBar;
    auto *limitText = new QLabel(QStringLiteral("CCW —    CW —    offset —"));
    limitText->setObjectName(QStringLiteral("limitText"));
    values['H' - 'A'] = limitText;
    limitLay->addWidget(limits);
    limitLay->addWidget(limitText);

    auto valueGroup = [&](const QString &title, const QList<QPair<QChar, QString>> &rows) {
        auto *box = makeGroup(title, column);
        auto *form = new QFormLayout(box);
        for (const auto &row : rows) {
            auto *label = new QLabel(QStringLiteral("—"));
            label->setWordWrap(true);
            if (row.first >= QLatin1Char('A') && row.first <= QLatin1Char('Z'))
                values[row.first.unicode() - QLatin1Char('A').unicode()] = label;
            else
                magnetic = label;
            form->addRow(row.second, label);
        }
        return box;
    };

    valueGroup(QStringLiteral("Motor"),
               {{QLatin1Char('G'), QStringLiteral("Max speed")},
                {QLatin1Char('F'), QStringLiteral("Min speed")},
                {QLatin1Char('N'), QStringLiteral("Ramp")},
                {QLatin1Char('P'), QStringLiteral("Motor timeout")},
                {QLatin1Char('E'), QStringLiteral("Rev / brake delay")}});
    auto *motorNote = new QLabel(QStringLiteral(
        "Max, ramp, timeout, and brake delay matched the utility on this station. "
        "Min speed did not: the utility showed 1 while register F read 125. The raw register is what is shown."));
    motorNote->setWordWrap(true);
    column->addWidget(motorNote);

    auto *analogBox = makeGroup(QStringLiteral("Analog calibration"), column);
    auto *analogLay = new QVBoxLayout(analogBox);
    analog = new QLabel(QStringLiteral("A/D —"));
    analog->setWordWrap(true);
    analogLay->addWidget(analog);
    ccwCal = new QLabel(QStringLiteral("CCW —"));
    cwCal = new QLabel(QStringLiteral("CW —"));
    analogLay->addWidget(ccwCal);
    analogLay->addWidget(cwCal);
    auto *calNote = new QLabel(QStringLiteral(
        "Set, Quick Cal, and Reset Cal are off. Terminal voltage is computed by the utility, not stored in its own register. "
        "At A/D 0440 the utility showed 3.58 V, which is not a 5 V full-scale reading."));
    calNote->setWordWrap(true);
    analogLay->addWidget(calNote);

    pulseGroup = valueGroup(QStringLiteral("Digital calibration"), {{QLatin1Char('K'), QStringLiteral("Divide ratio")}});
    if (auto *form = qobject_cast<QFormLayout *>(pulseGroup->layout())) {
        auto *pulseNote = new QLabel(QStringLiteral("Greyed unless the rotor type is Counter."));
        pulseNote->setWordWrap(true);
        form->addRow(QString(), pulseNote);
    }

    auto *advanced = makeGroup(QStringLiteral("Advanced"), column);
    auto *advancedLay = new QVBoxLayout(advanced);
    const QStringList bitNames = {
        QStringLiteral("Freeze display"), QStringLiteral("Fast brake"), QStringLiteral("Auto go-back"),
        QStringLiteral("Elevation"), QStringLiteral("Auto dim"), QStringLiteral("Analog out"),
        QStringLiteral("EME"), QStringLiteral("Point-and-shoot shortest route"), QStringLiteral("Port 9600"),
        QStringLiteral("Use encoder"), QStringLiteral("Demo"), QStringLiteral("Allow cal to the CCW limit"),
    };
    for (int i = 0; i < 12; ++i) {
        bits[i] = new QCheckBox(bitNames.at(i));
        bits[i]->setEnabled(false);
        advancedLay->addWidget(bits[i]);
    }
    auto *sensor = new QCheckBox(QStringLiteral("Use serial sensor"));
    sensor->setEnabled(false);
    sensor->setToolTip(QStringLiteral("The utility writes this with the same advanced field. Its bit was not separate in the program."));
    advancedLay->addWidget(sensor);

    valueGroup(QStringLiteral("Travel extras"),
               {{QLatin1Char('V'), QStringLiteral("Counter-rotate delay")},
                {QLatin1Char('U'), QStringLiteral("Over-travel")},
                {QLatin1Char('X'), QStringLiteral("Limit switch")},
                {QLatin1Char('L'), QStringLiteral("Cal range")},
                {QLatin1Char('['), QStringLiteral("Magnetic variation")}});

    auto *display = makeGroup(QStringLiteral("Display"), column);
    auto *displayForm = new QFormLayout(display);
    auto *brightRow = new QWidget;
    auto *brightLay = new QHBoxLayout(brightRow);
    brightLay->setContentsMargins(0, 0, 0, 0);
    auto *brightNow = new QLabel(QStringLiteral("—"));
    brightNow->setWordWrap(true);
    values[QLatin1Char('O').unicode() - QLatin1Char('A').unicode()] = brightNow;
    brightnessSpin = new QSpinBox;
    brightnessSpin->setObjectName(QStringLiteral("brightnessSpin"));
    brightnessSpin->setRange(0, 15);
    brightnessSpin->setValue(8);
    auto *writeBrightness = new QPushButton(QStringLiteral("Write"));
    writeBrightness->setObjectName(QStringLiteral("writeBrightness"));
    brightLay->addWidget(brightNow);
    brightLay->addWidget(brightnessSpin);
    brightLay->addWidget(writeBrightness);
    brightLay->addStretch();
    displayForm->addRow(QStringLiteral("Brightness"), brightRow);
    auto *oled = new QLabel(QStringLiteral(
        "Has no visible effect on OLED RT-21 units (LCD units only, unverified)."));
    oled->setObjectName(QStringLiteral("brightnessNote"));
    oled->setWordWrap(true);
    displayForm->addRow(QString(), oled);
    auto addReadRow = [&](QChar id, const QString &name) {
        auto *label = new QLabel(QStringLiteral("—"));
        label->setWordWrap(true);
        if (id >= QLatin1Char('A') && id <= QLatin1Char('Z'))
            values[id.unicode() - QLatin1Char('A').unicode()] = label;
        displayForm->addRow(name, label);
    };
    addReadRow(QLatin1Char('Y'), QStringLiteral("Knob timer"));
    saverCombo = new QComboBox;
    saverCombo->setObjectName(QStringLiteral("screenSaver"));
    saverCombo->setEnabled(false);
    saverCombo->addItem(QStringLiteral("Off"), 0);
    for (int minutes : {1, 2, 5, 10, 20, 30, 40, 50, 60})
        saverCombo->addItem(QStringLiteral("%1 min").arg(minutes), minutes);
    displayForm->addRow(QStringLiteral("Screen saver"), saverCombo);
    auto *saverNote = new QLabel(QStringLiteral(
        "Not written. On 4.13.2 it blanks the panel and serial keeps answering. Polls do not wake it. "
        "On 4.14.x the same setting stops serial until a power cycle."));
    saverNote->setWordWrap(true);
    displayForm->addRow(QString(), saverNote);
    addReadRow(QLatin1Char('R'), QStringLiteral("Alt 1"));
    addReadRow(QLatin1Char('S'), QStringLiteral("Alt 2"));
    connect(writeBrightness, &QPushButton::clicked, this, [this]() {
        if (brightnessSpin)
            emit brightnessWriteRequested(brightnessSpin->value());
    });

    column->addStretch();
}

void ControllerColumn::addTail(QWidget *ports, QWidget *network)
{
    if (ports)
        column->insertWidget(column->count() - 1, ports);
    if (network) {
        network->setObjectName(QStringLiteral("networkGroup"));
        column->insertWidget(column->count() - 1, network);
    }
}

void ControllerColumn::setLink(const RotorEndpoint &endpoint)
{
    if (!linkLabel)
        return;
    if (!endpoint.configured()) {
        linkLabel->setText(QStringLiteral("No port saved yet. Edgeport ports show up as COM1–COM8."));
        return;
    }
    if (endpoint.transport == RotorEndpoint::Transport::Serial)
        linkLabel->setText(QStringLiteral("%1 at %2 baud, 8N1.").arg(endpoint.serialPort).arg(endpoint.baud));
    else
        linkLabel->setText(QStringLiteral("%1 port %2.").arg(endpoint.host).arg(endpoint.tcpPort));
}

void ControllerColumn::setDriver(Rt21Driver *next)
{
    if (wired && driver)
        disconnect(driver, nullptr, this, nullptr);
    driver = next;
    wired = true;
    if (!driver)
        return;
    connect(driver, &Rt21Driver::headingChanged, this, [this](double azimuth) {
        heading->setText(QStringLiteral("Heading %1°").arg(azimuth, 0, 'f', 1));
    });
    connect(driver, &Rt21Driver::configField, this,
            [this](QChar id, const QString &raw, bool numeric, int value) { applyField(id, raw, numeric, value); });
    connect(driver, &Rt21Driver::configNote, this, [this](const QString &text) {
        if (text.startsWith(QStringLiteral("Pot A/D ")))
            analog->setText(text);
    });
    if (driver->heading())
        heading->setText(QStringLiteral("Heading %1°").arg(*driver->heading(), 0, 'f', 1));
}

void ControllerColumn::selectCode(QChar id, int code)
{
    int which = -1;
    if (id == QLatin1Char('J'))
        which = 0;
    else if (id == QLatin1Char('Q'))
        which = 1;
    else if (id == QLatin1Char('M'))
        which = 2;
    if (which < 0 || !choices[which])
        return;
    if (auto *button = choices[which]->button(code))
        button->setChecked(true);
}

void ControllerColumn::applyField(QChar id, const QString &raw, bool numeric, int value)
{
    if (id == QLatin1Char('J')) {
        selectCode(id, value);
        if (pulseGroup)
            pulseGroup->setEnabled(value == 1);
    } else if (id == QLatin1Char('Q') || id == QLatin1Char('M')) {
        selectCode(id, numeric ? value : -1);
    }
    if (id == QLatin1Char('H'))
        ccwLimit = value;
    if (id == QLatin1Char('I'))
        cwLimit = value;
    if (id == QLatin1Char('D'))
        offset = value;
    if (limits && (id == QLatin1Char('H') || id == QLatin1Char('I') || id == QLatin1Char('D')))
        static_cast<TravelBar *>(limits)->setArc(ccwLimit, cwLimit, offset);

    QString shown = raw;
    if (numeric && id == QLatin1Char('W')) {
        shown = value == 0 ? QStringLiteral("Off") : QStringLiteral("%1 min").arg(value);
        if (saverCombo) {
            int index = saverCombo->findData(value);
            if (index < 0) {
                saverCombo->addItem(QStringLiteral("%1 min").arg(value), value);
                index = saverCombo->findData(value);
            }
            saverCombo->setCurrentIndex(index);
        }
    }
    else if (numeric && (id == QLatin1Char('R') || id == QLatin1Char('S')))
        shown = value == 361 ? QStringLiteral("off") : QStringLiteral("%1°").arg(value);
    else if (numeric && id == QLatin1Char('O')) {
        shown = QString::number(value);
        if (brightnessSpin)
            brightnessSpin->setValue(std::clamp(value, brightnessSpin->minimum(), brightnessSpin->maximum()));
    } else if (numeric && id == QLatin1Char('X'))
        shown = QStringLiteral("%1 (raw %2)").arg(value / 10.0, 0, 'f', 1).arg(value);
    else if (numeric && (id == QLatin1Char('P') || id == QLatin1Char('E') || id == QLatin1Char('V')))
        shown = QStringLiteral("%1 s").arg(value);
    else if (numeric && (id == QLatin1Char('H') || id == QLatin1Char('I') || id == QLatin1Char('D') || id == QLatin1Char('U')
                         || id == QLatin1Char('L')))
        shown = QStringLiteral("%1°").arg(value);
    else if (numeric && id == QLatin1Char('F') && value > 10)
        shown = QStringLiteral("%1 (raw; the utility's slider is 0–10)").arg(value);
    else if (id == QLatin1Char('['))
        shown = raw;

    if (id == QLatin1Char('H') || id == QLatin1Char('I') || id == QLatin1Char('D')) {
        if (QLabel *label = values['H' - 'A'])
            label->setText(QStringLiteral("CCW %1°    CW %2°    offset %3°").arg(ccwLimit).arg(cwLimit).arg(offset));
    }
    if (id == QLatin1Char('A') && ccwCal)
        ccwCal->setText(QStringLiteral("CCW %1").arg(shown));
    if (id == QLatin1Char('B') && cwCal)
        cwCal->setText(QStringLiteral("CW %1").arg(shown));

    if (id >= QLatin1Char('A') && id <= QLatin1Char('Z')) {
        const int index = id.unicode() - QLatin1Char('A').unicode();
        if (values[index] && id != QLatin1Char('H') && id != QLatin1Char('I') && id != QLatin1Char('D') && id != QLatin1Char('A')
            && id != QLatin1Char('B'))
            values[index]->setText(shown);
    } else if (id == QLatin1Char('[') && magnetic) {
        magnetic->setText(shown);
    }

    if (id == QLatin1Char('T') && numeric) {
        for (int bit = 0; bit < 12; ++bit)
            bits[bit]->setChecked(value & (1 << bit));
    }
}
