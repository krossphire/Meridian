/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GlobeWidget.h"

#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

class TestGlobe : public QObject
{
    Q_OBJECT
private slots:
    void clickEastAndEdit();
    void sunStaysInsideDisc();
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

void TestGlobe::sunStaysInsideDisc()
{
    GlobeWidget globe;
    globe.resize(420, 420);
    globe.setHome(36.4, -94.2);
    globe.setHeading(1.2);
    globe.setShowSun(true);
    globe.setSolarPreview(QDateTime(QDate(2024, 6, 21), QTime(18, 0), Qt::UTC));
    globe.show();
    QTest::qWait(50);
    const QImage shot = globe.grab().toImage();
    const double side = std::max(2.0, std::min(shot.width(), shot.height()) - 2.0);
    const double radius = side / 2.0;
    const double cx = shot.width() / 2.0;
    const double cy = shot.height() / 2.0;
    for (int y = 0; y < shot.height(); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            const double distance = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            if (distance <= radius + 1.5)
                continue;
            if (y > shot.height() - 32)
                continue;
            if (x > shot.width() - 36 && y > shot.height() - 36)
                continue;
            const QColor pixel = shot.pixelColor(x, y);
            QVERIFY2(pixel.red() < 40 && pixel.green() < 40 && pixel.blue() < 48,
                     qPrintable(QStringLiteral("%1,%2 rgb %3 %4 %5").arg(x).arg(y).arg(pixel.red()).arg(pixel.green()).arg(pixel.blue())));
        }
    }
}

QTEST_MAIN(TestGlobe)
#include "tst_globe.moc"
