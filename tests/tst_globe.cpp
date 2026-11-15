/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GlobeWidget.h"
#include "core/Gridsquare.h"
#include "core/Projection.h"
#include "core/Solar.h"
#include "map/DayNight.h"
#include "map/GeoMap.h"

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
    void terminatorIsCurved();
    void subsolarProjectsInside();
    void rimSegmentsStayShort();
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

double lineResidual(const QImage &disc)
{
    QVector<QPointF> band;
    for (int y = 0; y < disc.height(); ++y) {
        for (int x = 0; x < disc.width(); ++x) {
            const int alpha = qAlpha(disc.pixel(x, y));
            if (alpha > 6 && alpha < 32)
                band.append(QPointF(x, y));
        }
    }
    if (band.size() < 40)
        return 1000.0;
    double mx = 0;
    double my = 0;
    for (const QPointF &p : band) {
        mx += p.x();
        my += p.y();
    }
    mx /= band.size();
    my /= band.size();
    double xx = 0;
    double xy = 0;
    double yy = 0;
    for (const QPointF &p : band) {
        const double dx = p.x() - mx;
        const double dy = p.y() - my;
        xx += dx * dx;
        xy += dx * dy;
        yy += dy * dy;
    }
    const double trace = xx + yy;
    const double det = xx * yy - xy * xy;
    const double gap = std::sqrt(std::max(0.0, trace * trace / 4.0 - det));
    const double small = trace / 2.0 - gap;
    return std::sqrt(std::max(0.0, small / band.size()));
}

void TestGlobe::terminatorIsCurved()
{
    const QList<QPair<QString, QDateTime>> cases = {
        {QStringLiteral("FI15"), QDateTime(QDate(2026, 11, 15), QTime(5, 39), Qt::UTC)},
        {QStringLiteral("EM26"), QDateTime(QDate(2024, 6, 21), QTime(18, 0), Qt::UTC)},
        {QStringLiteral("EM26"), QDateTime(QDate(2024, 3, 20), QTime(12, 0), Qt::UTC)},
        {QStringLiteral("JO62"), QDateTime(QDate(2024, 6, 21), QTime(6, 0), Qt::UTC)},
        {QStringLiteral("QF56"), QDateTime(QDate(2024, 12, 21), QTime(0, 0), Qt::UTC)},
        {QStringLiteral("PM14"), QDateTime(QDate(2026, 11, 15), QTime(5, 43), Qt::UTC)},
    };
    for (const auto &item : cases) {
        double lat = 0;
        double lon = 0;
        QVERIFY(Gridsquare::toLatLon(item.first, lat, lon));
        const SolarPosition sun = solarAt(lat, lon, item.second);
        const double minutes = item.second.time().hour() * 60.0 + item.second.time().minute();
        const QImage disc = renderDayNightDisc(384, lat, lon, sun, minutes);
        const double residual = lineResidual(disc);
        QVERIFY2(residual > 14.0, qPrintable(item.first + QStringLiteral(" residual ") + QString::number(residual)));
        const QString dir = qEnvironmentVariable("MERIDIAN_SHOT_DIR");
        if (!dir.isEmpty())
            disc.save(dir + QLatin1Char('/') + QStringLiteral("sun-") + item.first.toLower() + QLatin1Char('-')
                      + item.second.toString(QStringLiteral("MMdd-HHmm")) + QStringLiteral(".png"));
    }
    const SolarPosition polar = solarAt(-77.8, 166.7, QDateTime(QDate(2024, 6, 21), QTime(0, 0), Qt::UTC));
    const QImage polarDisc = renderDayNightDisc(384, -77.8, 166.7, polar, 0);
    QVERIFY2(lineResidual(polarDisc) > 14.0, qPrintable(QString::number(lineResidual(polarDisc))));
    const QString dir = qEnvironmentVariable("MERIDIAN_SHOT_DIR");
    if (!dir.isEmpty())
        polarDisc.save(dir + QStringLiteral("/sun-kc4-0621-0000.png"));

    double koreaLat = 0;
    double koreaLon = 0;
    QVERIFY(Gridsquare::toLatLon(QStringLiteral("PM14"), koreaLat, koreaLon));
    const QDateTime koreaWhen(QDate(2026, 11, 15), QTime(5, 43), Qt::UTC);
    const SolarPosition koreaSun = solarAt(koreaLat, koreaLon, koreaWhen);
    QVERIFY2(koreaSun.elevation > 20.0, qPrintable(QString::number(koreaSun.elevation)));
    const QImage korea = renderDayNightDisc(384, koreaLat, koreaLon, koreaSun, 5 * 60.0 + 43.0);
    const int mid = korea.width() / 2;
    QVERIFY2(qAlpha(korea.pixel(mid, mid)) > 30, qPrintable(QString::number(qAlpha(korea.pixel(mid, mid)))));
    bool nightNearRim = false;
    for (int y = 0; y < korea.height() && !nightNearRim; ++y) {
        for (int x = 0; x < korea.width(); ++x) {
            const double ux = (x + 0.5) / korea.width() * 2.0 - 1.0;
            const double uy = 1.0 - (y + 0.5) / korea.height() * 2.0;
            const double rho = std::hypot(ux, uy);
            if (rho > 0.82 && rho < 0.98 && qAlpha(korea.pixel(x, y)) < 8) {
                nightNearRim = true;
                break;
            }
        }
    }
    QVERIFY(nightNearRim);
}

void TestGlobe::subsolarProjectsInside()
{
    const QDateTime when(QDate(2026, 11, 15), QTime(5, 39), Qt::UTC);
    double lat = 0;
    double lon = 0;
    QVERIFY(Gridsquare::toLatLon(QStringLiteral("FI15"), lat, lon));
    const SolarPosition sun = solarAt(lat, lon, when);
    QVERIFY(sun.subsolarLatitude < -10.0 && sun.subsolarLatitude > -25.0);
    // 05:39 UTC is morning at Greenwich, so the sun is over the eastern hemisphere.
    QVERIFY2(sun.subsolarLongitude > 60.0 && sun.subsolarLongitude < 130.0,
             qPrintable(QString::number(sun.subsolarLongitude)));
    const UnitPoint spot = projectAzimuthal(lat, lon, sun.subsolarLatitude, sun.subsolarLongitude);
    QVERIFY(spot.rho < 0.999);
    QVERIFY2(spot.x > 0.0, qPrintable(QString::number(spot.x)));
}

void TestGlobe::rimSegmentsStayShort()
{
    QFile file(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/map110.bin"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    GeoLayer layer;
    QVERIFY(loadGeoLayer(file.readAll(), layer));
    const ProjectedLayer projected = projectLayer(layer, -33.9, 151.2);
    int checked = 0;
    for (const QPainterPath &path : projected.land) {
        QPointF prev;
        bool have = false;
        for (int i = 0; i < path.elementCount(); ++i) {
            const auto el = path.elementAt(i);
            const QPointF at(el.x, el.y);
            if (el.isLineTo() && have) {
                QVERIFY(std::hypot(at.x() - prev.x(), at.y() - prev.y()) < 0.5);
                ++checked;
            }
            prev = at;
            have = !el.isMoveTo() || true;
            if (el.isMoveTo())
                have = true;
        }
    }
    QVERIFY(checked > 100);
}

QTEST_MAIN(TestGlobe)
#include "tst_globe.moc"
