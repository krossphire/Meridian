/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GlobeWidget.h"

#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

class TestGlobe : public QObject
{
    Q_OBJECT
private slots:
    void clickEastAndEdit();
};

void TestGlobe::clickEastAndEdit()
{
    QFile lowFile(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/map110.bin"));
    QFile highFile(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/map50.bin"));
    QVERIFY(lowFile.open(QIODevice::ReadOnly));
    QVERIFY(highFile.open(QIODevice::ReadOnly));
    GeoLayer low;
    GeoLayer high;
    QVERIFY(loadGeoLayer(lowFile.readAll(), low));
    QVERIFY(loadGeoLayer(highFile.readAll(), high));

    GlobeWidget globe;
    globe.resize(600, 600);
    globe.setLayers(&low, &high);
    globe.setHome(36.5, -95.0);
    globe.setHeading(10);
    globe.show();
    QVERIFY(QTest::qWaitFor([&]() { return globe.mapReady(); }, 20000));

    QSignalSpy bearings(&globe, &GlobeWidget::bearingRequested);
    const QPoint east(globe.width() / 2 + 120, globe.height() / 2);
    QTest::mouseClick(&globe, Qt::LeftButton, Qt::NoModifier, east);
    QCOMPARE(bearings.size(), 1);
    QVERIFY(std::abs(bearings.at(0).at(0).toDouble() - 90.0) < 1.0);

    QSignalSpy edited(&globe, &GlobeWidget::headingEdited);
    globe.openHeadingEditor();
    auto *edit = globe.findChild<QLineEdit *>();
    QVERIFY(edit);
    edit->setText(QStringLiteral("12.34"));
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(edited.size(), 1);
    QCOMPARE(edited.at(0).at(0).toDouble(), 12.3);

    globe.openHeadingEditor();
    edit->setText(QStringLiteral("200"));
    QTest::keyClick(edit, Qt::Key_Escape);
    QCOMPARE(edited.size(), 1);
}

QTEST_MAIN(TestGlobe)
#include "tst_globe.moc"
