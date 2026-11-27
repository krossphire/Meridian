/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Bearing.h"
#include "core/Compass.h"
#include "core/Coordinates.h"
#include "core/Gridsquare.h"
#include "core/Projection.h"
#include "core/ZipCodes.h"
#include "map/Dxcc.h"
#include "map/GeoMap.h"
#include "core/Solar.h"
#include "rotor/AppConfig.h"
#include "rotor/Reconnect.h"
#include "rotor/Rt21Protocol.h"

#include <QSettings>
#include <QTemporaryDir>

#include <QDateTime>
#include <QFile>
#include <QTest>

#include <cmath>

class TestCore : public QObject
{
    Q_OBJECT
private slots:
    void gridCenters();
    void gridFromPoint();
    void gridRejects();
    void coordinates_data();
    void coordinates();
    void zipCentroids();
    void bearingDirections();
    void compassWrap();
    void gridBearing();
    void preciseHomeBearing();
    void homesChangeTheBearing();
    void azimuthClamp();
    void projectionRoundTrip();
    void countryHit();
    void dxccHover();
    void homeCoordinatesRoundTrip();
    void targetLineSettingRoundTrip();
    void framing();
    void strictSemicolonFrames();
    void commands();
    void headingParse();
    void hardwareRx();
    void solarPosition();
    void reconnectTiming();
};

void TestCore::gridCenters()
{
    double lat = 0;
    double lon = 0;
    QVERIFY(Gridsquare::toLatLon(QStringLiteral("EM26"), lat, lon));
    QCOMPARE(lat, 36.5);
    QCOMPARE(lon, -95.0);
    QCOMPARE(Gridsquare::normalize(QStringLiteral(" em26 ")), QStringLiteral("EM26"));

    QVERIFY(Gridsquare::toLatLon(QStringLiteral("EM26ul"), lat, lon));
    QVERIFY(lat > 36.0 && lat < 37.0);
    QVERIFY(lon > -96.0 && lon < -94.0);

    QVERIFY(Gridsquare::toLatLon(QStringLiteral("EM26UL18"), lat, lon));
    QVERIFY(std::abs(lat - 36.5) < 1.0);
}

void TestCore::gridFromPoint()
{
    QCOMPARE(Gridsquare::fromLatLon(36.48, -94.27), QStringLiteral("EM26UL"));
    QCOMPARE(Gridsquare::fromLatLon(91, 0), QString());
}

void TestCore::gridRejects()
{
    double lat = 0;
    double lon = 0;
    QVERIFY(!Gridsquare::toLatLon(QStringLiteral("EM2"), lat, lon));
    QVERIFY(!Gridsquare::toLatLon(QStringLiteral("SS00"), lat, lon));
    QVERIFY(!Gridsquare::toLatLon(QStringLiteral("EM26ZZ"), lat, lon));
    QVERIFY(!Gridsquare::isValid(QStringLiteral("AA")));
}

void TestCore::coordinates_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<double>("lat");
    QTest::addColumn<double>("lon");
    const double lat = 36.47;
    const double lon = -94.23;
    QTest::newRow("decimal comma") << QStringLiteral("36.47, -94.23") << lat << lon;
    QTest::newRow("decimal space") << QStringLiteral("36.47 -94.23") << lat << lon;
    QTest::newRow("signed") << QStringLiteral("+36.47, -94.23") << lat << lon;
    QTest::newRow("suffix") << QStringLiteral("36.47N 94.23W") << lat << lon;
    QTest::newRow("suffix spaces") << QStringLiteral("36.47 N, 94.23 W") << lat << lon;
    QTest::newRow("prefix") << QStringLiteral("N36.47 W94.23") << lat << lon;
    QTest::newRow("prefix spaced") << QStringLiteral("N 36.47 W 94.23") << lat << lon;
    QTest::newRow("swapped hemi") << QStringLiteral("W94.23 N36.47") << lat << lon;
    QTest::newRow("dms symbols") << QStringLiteral("36°28'12\"N 94°13'48\"W") << lat << lon;
    QTest::newRow("dms loose") << QStringLiteral("36 28 12 N 94 13 48 W") << lat << lon;
    QTest::newRow("dms letters") << QStringLiteral("36d28m12s N 94d13m48s W") << lat << lon;
    QTest::newRow("ddm") << QStringLiteral("36°28.2'N 94°13.8'W") << lat << lon;
    QTest::newRow("ddm loose") << QStringLiteral("36 28.2 N 94 13.8 W") << lat << lon;
    QTest::newRow("south east") << QStringLiteral("36.47S 94.23E") << -lat << 94.23;
}

void TestCore::coordinates()
{
    QFETCH(QString, text);
    QFETCH(double, lat);
    QFETCH(double, lon);
    QString error;
    const auto loc = resolveLocation(text, nullptr, &error);
    QVERIFY2(loc.has_value(), qPrintable(error));
    QCOMPARE(loc->source, ResolvedLocation::Source::Coordinates);
    QVERIFY(std::abs(loc->latitude - lat) < 1e-6);
    QVERIFY(std::abs(loc->longitude - lon) < 1e-6);
}

void TestCore::zipCentroids()
{
    QFile file(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/zcta.bin"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    ZipDatabase zips;
    QVERIFY(zips.load(file.readAll()));
    QVERIFY(zips.size() > 30000);
    double lat = 0;
    double lon = 0;
    QVERIFY(zips.lookup(QStringLiteral("72714"), lat, lon));
    QVERIFY(std::abs(lat - 36.46735) < 1e-4);
    QVERIFY(std::abs(lon - (-94.222151)) < 1e-4);
    QVERIFY(zips.lookup(QStringLiteral("90210-1234"), lat, lon));
    QVERIFY(std::abs(lat - 34.100517) < 1e-4);
    QVERIFY(!zips.lookup(QStringLiteral("00000"), lat, lon));

    QString error;
    const auto loc = resolveLocation(QStringLiteral("72715"), &zips, &error);
    QVERIFY(loc.has_value());
    QCOMPARE(loc->source, ResolvedLocation::Source::Zip);
    QVERIFY(loc->summary().contains(QStringLiteral("EM26")));

    const auto grid = resolveLocation(QStringLiteral("EM26"), &zips, &error);
    QVERIFY(grid.has_value());
    QCOMPARE(grid->latitude, 36.5);
    QCOMPARE(grid->longitude, -95.0);
    QVERIFY(grid->summary().contains(QStringLiteral("36.5000°N")));
}

void TestCore::bearingDirections()
{
    const QPointF center(100, 100);
    const auto north = bearingFromClick(QPointF(100, 50), center, 80);
    const auto east = bearingFromClick(QPointF(160, 100), center, 80);
    const auto south = bearingFromClick(QPointF(100, 170), center, 80);
    const auto west = bearingFromClick(QPointF(30, 100), center, 80);
    QVERIFY(north && std::abs(north->azimuth - 0.0) < 0.01);
    QVERIFY(east && std::abs(east->azimuth - 90.0) < 0.01);
    QVERIFY(south && std::abs(south->azimuth - 180.0) < 0.01);
    QVERIFY(west && std::abs(west->azimuth - 270.0) < 0.01);
    QVERIFY(!bearingFromClick(QPointF(100, 100), center, 80));
    QVERIFY(!bearingFromClick(QPointF(200, 100), center, 80));
}

void TestCore::compassWrap()
{
    QCOMPARE(nearestCompassIndex(0.0), 0);
    QCOMPARE(QString::fromLatin1(compassLabel(0)), QStringLiteral("N"));
    QCOMPARE(nearestCompassIndex(359.0), 0);
    QCOMPARE(nearestCompassIndex(10.0), 0);
    QCOMPARE(nearestCompassIndex(22.4), 0);
    QCOMPARE(nearestCompassIndex(22.5), 1);
    QCOMPARE(QString::fromLatin1(compassLabel(1)), QStringLiteral("NE"));
    QCOMPARE(nearestCompassIndex(45.0), 1);
    QCOMPARE(nearestCompassIndex(180.0), 4);
    QCOMPARE(QString::fromLatin1(compassLabel(4)), QStringLiteral("S"));
    QCOMPARE(nearestCompassIndex(337.4), 7);
    QCOMPARE(QString::fromLatin1(compassLabel(7)), QStringLiteral("NW"));
    QCOMPARE(nearestCompassIndex(337.5), 0);
    QCOMPARE(nearestCompassIndex(-10.0), 0);
}

void TestCore::gridBearing()
{
    double bearing = -1;
    QVERIFY(Gridsquare::bearingTo(QStringLiteral("EM26"), QStringLiteral("KG54"), bearing));
    QCOMPARE(QString::number(bearing, 'f', 1), QStringLiteral("92.4"));
    QVERIFY(!Gridsquare::bearingTo(QStringLiteral("EM26"), QStringLiteral("nope"), bearing));

    // A 6-character square is centered on its subsquare, not on a corner.
    double lat = 0;
    double lon = 0;
    QVERIFY(Gridsquare::toLatLon(QStringLiteral("EM26ul"), lat, lon));
    QCOMPARE(lat, 36.47916666666667);
    QCOMPARE(lon, -94.29166666666667);
    QVERIFY(Gridsquare::bearingTo(QStringLiteral("EM26ul"), QStringLiteral("EM26vm"), bearing));
    QCOMPARE(QString::number(bearing, 'f', 1), QStringLiteral("58.1"));
    QVERIFY(Gridsquare::bearingTo(QStringLiteral("EM26ul"), QStringLiteral("EM26uk"), bearing));
    QCOMPARE(QString::number(bearing, 'f', 1), QStringLiteral("180.0"));
    QVERIFY(Gridsquare::bearingTo(QStringLiteral("EM26ul"), QStringLiteral("KG54"), bearing));
    QCOMPARE(QString::number(bearing, 'f', 1), QStringLiteral("92.8"));
    double same = -1;
    QVERIFY(Gridsquare::bearingTo(36.5, -95.0, 36.5, -95.0, same));
    QCOMPARE(same, 0.0);
}

void TestCore::preciseHomeBearing()
{
    const double targetLat = 36.0;
    const double targetLon = -94.25;
    QString error;
    const auto decimal = resolveLocation(QStringLiteral("36.25, -94.25"), nullptr, &error);
    QVERIFY2(decimal.has_value(), qPrintable(error));
    QCOMPARE(decimal->source, ResolvedLocation::Source::Coordinates);
    QVERIFY(std::abs(decimal->latitude - 36.25) < 1e-9);
    QVERIFY(std::abs(decimal->longitude - (-94.25)) < 1e-9);
    QVERIFY(!decimal->grid.isEmpty());
    double centerLat = 0;
    double centerLon = 0;
    QVERIFY(Gridsquare::toLatLon(decimal->grid, centerLat, centerLon));
    QVERIFY(std::abs(centerLat - decimal->latitude) > 0.01 || std::abs(centerLon - decimal->longitude) > 0.01);

    double rawBearing = 0;
    double centerBearing = 0;
    QVERIFY(Gridsquare::bearingTo(decimal->latitude, decimal->longitude, targetLat, targetLon, rawBearing));
    QVERIFY(Gridsquare::bearingTo(centerLat, centerLon, targetLat, targetLon, centerBearing));
    QCOMPARE(QString::number(rawBearing, 'f', 1), QStringLiteral("180.0"));
    QCOMPARE(QString::number(centerBearing, 'f', 1), QStringLiteral("187.1"));

    const auto minutes = resolveLocation(QStringLiteral("36 15 N 94 15 W"), nullptr, &error);
    QVERIFY2(minutes.has_value(), qPrintable(error));
    QVERIFY(std::abs(minutes->latitude - decimal->latitude) < 1e-9);
    QVERIFY(std::abs(minutes->longitude - decimal->longitude) < 1e-9);
    double fromMinutes = 0;
    QVERIFY(Gridsquare::bearingTo(minutes->latitude, minutes->longitude, targetLat, targetLon, fromMinutes));
    QCOMPARE(QString::number(fromMinutes, 'f', 1), QStringLiteral("180.0"));

    const auto degrees = resolveHome(QString(), QStringLiteral("36.25°"), QStringLiteral("94°15'W"), nullptr, &error);
    QVERIFY2(degrees.has_value(), qPrintable(error));
    QVERIFY(std::abs(degrees->latitude - 36.25) < 1e-9);
    QVERIFY(std::abs(degrees->longitude - (-94.25)) < 1e-9);

    QTemporaryDir bareDir;
    QVERIFY(bareDir.isValid());
    QSettings bare(bareDir.filePath(QStringLiteral("meridian.ini")), QSettings::IniFormat);
    bare.clear();
    bare.setValue(QStringLiteral("home/text"), QStringLiteral("FN31"));
    bare.sync();
    const AppConfig kept = AppConfig::load(bare);
    QCOMPARE(kept.homeText, QStringLiteral("FN31"));
    bare.clear();

    const auto overridden = resolveHome(QStringLiteral("EM26"), QStringLiteral("36.25"), QStringLiteral("-94.25"),
                                        nullptr, &error);
    QVERIFY2(overridden.has_value(), qPrintable(error));
    QVERIFY(std::abs(overridden->latitude - 36.25) < 1e-9);
    QVERIFY(std::abs(overridden->longitude - (-94.25)) < 1e-9);
    double fromFields = 0;
    QVERIFY(Gridsquare::bearingTo(overridden->latitude, overridden->longitude, targetLat, targetLon, fromFields));
    QCOMPARE(QString::number(fromFields, 'f', 1), QStringLiteral("180.0"));

    double fourLat = 0;
    double fourLon = 0;
    QVERIFY(Gridsquare::toLatLon(QStringLiteral("EM26"), fourLat, fourLon));
    QCOMPARE(fourLat, 36.5);
    QCOMPARE(fourLon, -95.0);
    const double stationLat = 36.0724337;
    const double stationLon = -94.0893386;
    double fromFour = 0;
    double fromSix = 0;
    double fromCorner = 0;
    QVERIFY(Gridsquare::bearingTo(fourLat, fourLon, stationLat, stationLon, fromFour));
    QVERIFY(Gridsquare::toLatLon(QStringLiteral("EM26vl"), centerLat, centerLon));
    QVERIFY(Gridsquare::bearingTo(centerLat, centerLon, stationLat, stationLon, fromSix));
    QVERIFY(Gridsquare::bearingTo(36.0, -96.0, stationLat, stationLon, fromCorner));
    QCOMPARE(QString::number(fromFour, 'f', 1), QStringLiteral("119.9"));
    QCOMPARE(QString::number(fromSix, 'f', 1), QStringLiteral("166.7"));
    QVERIFY(QString::number(fromCorner, 'f', 1) != QString::number(fromFour, 'f', 1));

    double wide = 0;
    double tight = 0;
    QVERIFY(Gridsquare::bearingTo(QStringLiteral("EM26"), QStringLiteral("PM95"), wide));
    QVERIFY(Gridsquare::bearingTo(QStringLiteral("EM26ul"), QStringLiteral("PM95"), tight));
    QVERIFY(std::abs(wide - tight) < 3.0);
    QVERIFY(std::abs(wide - tight) > 0.1);
}

void TestCore::homesChangeTheBearing()
{
    const QString target = QStringLiteral("KG54");
    const struct {
        const char *home;
        const char *bearing;
    } rows[] = {
        {"JO01", "152.8"},
        {"PM95", "257.6"},
        {"FN31", "101.5"},
        {"QF56", "232.1"},
    };
    QString previous;
    for (const auto &row : rows) {
        double bearing = 0;
        QVERIFY(Gridsquare::bearingTo(QString::fromLatin1(row.home), target, bearing));
        const QString shown = QString::number(bearing, 'f', 1);
        QCOMPARE(shown, QString::fromLatin1(row.bearing));
        QVERIFY(shown != previous);
        previous = shown;
    }
}

void TestCore::azimuthClamp()
{
    QCOMPARE(clampRt21Azimuth(0), 0.0);
    QCOMPARE(clampRt21Azimuth(360), 0.0);
    QCOMPARE(clampRt21Azimuth(359.94), 359.9);
    QCOMPARE(clampRt21Azimuth(359.96), 0.0);
    QCOMPARE(clampRt21Azimuth(-10), 350.0);
    QCOMPARE(clampRt21Azimuth(720.4), 0.4);
    QCOMPARE(clampRt21Azimuth(123.44), 123.4);
    QCOMPARE(clampRt21Azimuth(123.46), 123.5);
}

void TestCore::projectionRoundTrip()
{
    const UnitPoint origin = projectAzimuthal(36.5, -95.0, 36.5, -95.0);
    QVERIFY(std::abs(origin.x) < 1e-9);
    QVERIFY(std::abs(origin.y) < 1e-9);

    const UnitPoint north = projectAzimuthal(0, 0, 20, 0);
    QVERIFY(north.y > 0.1);
    QVERIFY(std::abs(north.x) < 1e-6);

    const UnitPoint east = projectAzimuthal(0, 0, 0, 20);
    QVERIFY(east.x > 0.1);
    QVERIFY(std::abs(east.y) < 1e-6);

    double lat = 0;
    double lon = 0;
    QVERIFY(unprojectAzimuthal(36.5, -95.0, north.x, north.y, lat, lon));
    const UnitPoint back = projectAzimuthal(36.5, -95.0, 48.0, -80.0);
    QVERIFY(unprojectAzimuthal(36.5, -95.0, back.x, back.y, lat, lon));
    QVERIFY(std::abs(lat - 48.0) < 1e-6);
    QVERIFY(std::abs(lon - (-80.0)) < 1e-6);
    QVERIFY(!unprojectAzimuthal(0, 0, 1.2, 0, lat, lon));
}

void TestCore::countryHit()
{
    QFile file(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/map50.bin"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    GeoLayer layer;
    QVERIFY(loadGeoLayer(file.readAll(), layer));
    const int home = countryAt(layer, 36.5, -95.0);
    QVERIFY(home >= 0);
    QCOMPARE(layer.countries.at(home).name, QStringLiteral("United States of America"));
    QCOMPARE(countryAt(layer, 0.0, -40.0), -1);
    const int paris = countryAt(layer, 48.85, 2.35);
    QVERIFY(paris >= 0);
    QCOMPARE(layer.countries.at(paris).name, QStringLiteral("France"));
    QCOMPARE(shortCountryName(QStringLiteral("United States of America")), QStringLiteral("United States"));
}

void TestCore::dxccHover()
{
    QFile outlines(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/dxcc.bin"));
    QFile names(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/dxcc-names.tsv"));
    QFile prefixes(QStringLiteral(MERIDIAN_DATA_DIR) + QStringLiteral("/dxcc-prefixes.tsv"));
    QVERIFY(outlines.open(QIODevice::ReadOnly));
    QVERIFY(names.open(QIODevice::ReadOnly));
    QVERIFY(prefixes.open(QIODevice::ReadOnly));
    QVERIFY(loadDxcc(outlines.readAll(), names.readAll(), prefixes.readAll()));
    const DxccLabel zealand = dxccForCountry(QStringLiteral("New Zealand"));
    QCOMPARE(zealand.name, QStringLiteral("New Zealand"));
    QCOMPARE(zealand.prefix, QStringLiteral("ZL"));
    const DxccLabel alaska = dxccAt(64.0, -153.0);
    QCOMPARE(alaska.name, QStringLiteral("Alaska"));
    QCOMPARE(alaska.prefix, QStringLiteral("KL7"));
    const DxccLabel hawaii = dxccAt(21.3, -157.8);
    QCOMPARE(hawaii.prefix, QStringLiteral("KH6"));
    const DxccLabel azores = dxccAt(37.8, -25.5);
    QCOMPARE(azores.prefix, QStringLiteral("CU"));
    const DxccLabel sardinia = dxccAt(40.1, 9.0);
    QCOMPARE(sardinia.prefix, QStringLiteral("IS0"));
    QVERIFY(!dxccEntities().isEmpty());

    const auto hawaiiCall = dxccForCallsign(QStringLiteral("KH6ABC"));
    const auto mixed = dxccForCallsign(QStringLiteral("K1ABC/KH6"));
    QVERIFY(hawaiiCall && mixed);
    QCOMPARE(mixed->prefix, QStringLiteral("KH6"));
    QCOMPARE(mixed->havePoint, hawaiiCall->havePoint);
    QCOMPARE(mixed->latitude, hawaiiCall->latitude);
    QCOMPARE(mixed->longitude, hawaiiCall->longitude);

    const auto k5po = dxccForCallsign(QStringLiteral("K5PO"));
    QVERIFY(k5po);
    QCOMPARE(k5po->country, QStringLiteral("United States of America"));
    const auto kh6 = dxccForCallsign(QStringLiteral("KH6"));
    QVERIFY(kh6);
    QCOMPARE(kh6->country, QStringLiteral("Hawaii"));
    const auto kl7 = dxccForCallsign(QStringLiteral("KL7RA"));
    QVERIFY(kl7);
    QCOMPARE(kl7->country, QStringLiteral("Alaska"));
    const auto kp4 = dxccForCallsign(QStringLiteral("KP4"));
    QVERIFY(kp4);
    QCOMPARE(kp4->country, QStringLiteral("Puerto Rico"));
    const auto falklands = dxccForCallsign(QStringLiteral("VP8ABC"));
    QVERIFY(falklands);
    QCOMPARE(falklands->country, QStringLiteral("Falkland Islands"));
    const auto georgia = dxccForCallsign(QStringLiteral("VP8ABC/G"));
    QVERIFY(georgia);
    QCOMPARE(georgia->country, QStringLiteral("South Georgia and the Islands"));
    const auto south = dxccForCallsign(QStringLiteral("VP0G"));
    QVERIFY(south);
    QCOMPARE(south->country, QStringLiteral("South Georgia and the Islands"));
    const auto exact = dxccForCallsign(QStringLiteral("K6KO"));
    QVERIFY(exact);
    QCOMPARE(exact->country, QStringLiteral("Hawaii"));
    QCOMPARE(exact->prefix, QStringLiteral("K6KO"));
}

void TestCore::homeCoordinatesRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings ini(dir.filePath(QStringLiteral("meridian.ini")), QSettings::IniFormat);
    ini.clear();
    AppConfig config = AppConfig::defaults();
    config.homeText = QStringLiteral("EM26");
    config.homeLatitude = QStringLiteral("36.25");
    config.homeLongitude = QStringLiteral("-94.25");
    config.save(ini);
    ini.sync();
    const AppConfig loaded = AppConfig::load(ini);
    QCOMPARE(loaded.homeText, QStringLiteral("EM26"));
    QCOMPARE(loaded.homeLatitude, QStringLiteral("36.25"));
    QCOMPARE(loaded.homeLongitude, QStringLiteral("-94.25"));
    QString error;
    const auto home = resolveHome(loaded.homeText, loaded.homeLatitude, loaded.homeLongitude, nullptr, &error);
    QVERIFY2(home.has_value(), qPrintable(error));
    QVERIFY(std::abs(home->latitude - 36.25) < 1e-9);
    QVERIFY(std::abs(home->longitude - (-94.25)) < 1e-9);
}

void TestCore::targetLineSettingRoundTrip()
{
    QCOMPARE(AppConfig::defaults().showTargetBearingLine, true);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings ini(dir.filePath(QStringLiteral("meridian.ini")), QSettings::IniFormat);
    AppConfig config = AppConfig::defaults();
    config.showTargetBearingLine = false;
    config.save(ini);
    ini.sync();
    const AppConfig hidden = AppConfig::load(ini);
    QCOMPARE(hidden.showTargetBearingLine, false);
    QFile file(dir.filePath(QStringLiteral("meridian.ini")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(file.readAll());
    QVERIFY(body.contains(QStringLiteral("showTargetBearingLine=false")));
    config.showTargetBearingLine = true;
    config.save(ini);
    ini.sync();
    QCOMPARE(AppConfig::load(ini).showTargetBearingLine, true);
}

void TestCore::framing()
{
    Rt21::Framer framer;
    framer.append("12");
    QVERIFY(framer.take().isEmpty());
    framer.append("3.4; 45");
    QList<QByteArray> frames = framer.take();
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0), QByteArray("123.4"));
    framer.append(".0;\r\nBI1;");
    frames = framer.take();
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0), QByteArray(" 45.0"));
    QCOMPARE(frames.at(1), QByteArray("BI1"));
}

void TestCore::strictSemicolonFrames()
{
    Rt21::Framer headed;
    headed.append(";131.9;");
    const QList<QByteArray> one = headed.take();
    QCOMPARE(one.size(), 1);
    QCOMPARE(one.at(0), QByteArray("131.9"));
    const auto heading = Rt21::parseHeadingFrame(one.at(0));
    QVERIFY(heading);
    QVERIFY(std::abs(*heading - 131.9) < 1e-9);

    Rt21::Framer split;
    split.append("OK");
    split.append("<uart2>;");
    const QList<QByteArray> prompt = split.take();
    QCOMPARE(prompt.size(), 2);
    QCOMPARE(prompt.at(0), QByteArray("OK"));
    QCOMPARE(prompt.at(1), QByteArray("<uart2>"));

    Rt21::Framer capped;
    capped.append(QByteArray(300, 'A'));
    capped.append(";");
    QVERIFY(capped.take().isEmpty());
    capped.append("131.9;");
    const QList<QByteArray> after = capped.take();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.at(0), QByteArray("131.9"));
}

void TestCore::commands()
{
    // Byte-exact point-and-shoot. The CR before ';' is the execute byte.
    QCOMPARE(Rt21::gotoImmediate(123.4), QByteArray("AP1123.4\r;"));
    QCOMPARE(Rt21::gotoImmediate(5), QByteArray("AP1005.0\r;"));
    QCOMPARE(Rt21::gotoImmediate(360), QByteArray("AP1000.0\r;"));
    QCOMPARE(Rt21::gotoImmediate(10).count('\r'), 1);
    QCOMPARE(Rt21::preset(12), QByteArray("AP1012.0;"));
    QVERIFY(!Rt21::preset(12).contains('\r'));
    const QList<QByteArray> built = {
        Rt21::stopCommand(),
        Rt21::queryTenths(),
        Rt21::queryInteger(),
        Rt21::queryStatus(),
        Rt21::queryVersion(),
        Rt21::queryPot(),
        Rt21::queryReset(),
        Rt21::preset(10),
        Rt21::executeMove(),
        Rt21::readEe(QLatin1Char('A')),
        Rt21::writeEe(QLatin1Char('A'), 1),
        Rt21::registerWriteCommand(QLatin1Char('O'), 10),
        Rt21::registerReadCommand(QLatin1Char('O')),
        Rt21::brightnessWriteCommand(10),
        Rt21::brightnessReadCommand(),
        Rt21::calibrateEndpoint(true),
        Rt21::calibrateEndpoint(false),
        Rt21::setPulseHeading(1.0),
    };
    for (const QByteArray &cmd : built) {
        QVERIFY(!cmd.contains('\r'));
        QVERIFY(!cmd.contains('\n'));
        QVERIFY(cmd.endsWith(';'));
    }
    QCOMPARE(Rt21::stopCommand(), QByteArray(";"));
    QCOMPARE(Rt21::queryTenths(), QByteArray("BI1;"));
    QCOMPARE(Rt21::queryStatus(), QByteArray("R21;"));
    QCOMPARE(Rt21::readEe(QLatin1Char('d')), QByteArray("RD1;"));
    QCOMPARE(Rt21::writeEe(QLatin1Char('D'), 180), QByteArray("WD1180;"));
    QCOMPARE(Rt21::calibrateEndpoint(false), QByteArray("C01;"));
    QCOMPARE(Rt21::calibrateEndpoint(true), QByteArray("C11;"));
    QCOMPARE(Rt21::setPulseHeading(36.5), QByteArray("AW1036.5;"));
}

void TestCore::headingParse()
{
    const auto a = Rt21::parseHeading("123.4");
    const auto b = Rt21::parseHeading(" 45.2");
    const auto c = Rt21::parseHeading("045;");
    QVERIFY(a && std::abs(*a - 123.4) < 1e-9);
    QVERIFY(b && std::abs(*b - 45.2) < 1e-9);
    QVERIFY(!c);
    const auto whole = Rt21::parseHeading("045");
    QVERIFY(whole && std::abs(*whole - 45.0) < 1e-9);
    QVERIFY(!Rt21::parseHeading("BI1"));
    QVERIFY(!Rt21::parseHeading("400"));

    const QByteArray status = QByteArray("\x01" "01>123.4");
    const auto parsed = Rt21::parseStatusFrame(status);
    QVERIFY(parsed);
    QVERIFY(parsed->haveStatus);
    QCOMPARE(parsed->status, 1);
    QVERIFY(std::abs(parsed->azimuth - 123.4) < 1e-9);

    const auto pot = Rt21::parseIntegerPayload(QByteArray("\x01" "0512"));
    QVERIFY(pot && *pot == 512);
    QVERIFY(!Rt21::parseIntegerPayload("Pot"));
    QCOMPARE(Rt21::payloadAfterSoh(QByteArray("\x01RT-21 Version 4.13")), QStringLiteral("RT-21 Version 4.13"));

    // Andrew's RT-21, firmware 4.14.g1, 15 Nov 2026, over 192.168.1.106:6555.
    auto echo = [](const char *mark, const char *value) {
        QByteArray frame(mark);
        frame.append(char(0x01));
        frame.append(value);
        return frame;
    };
    QCOMPARE(Rt21::payloadAfterSoh(echo("A", "5")), QStringLiteral("5"));
    QCOMPARE(Rt21::parseIntegerPayload(echo("A", "5")).value_or(-1), 5);
    QCOMPARE(Rt21::parseIntegerPayload(echo("B", "705")).value_or(-1), 705);
    QCOMPARE(Rt21::parseIntegerPayload(echo("D", "174")).value_or(-1), 174);
    QCOMPARE(Rt21::parseIntegerPayload(echo("F", "125")).value_or(-1), 125);
    QCOMPARE(Rt21::parseIntegerPayload(echo("J", "10")).value_or(-1), 10);
    QCOMPARE(Rt21::parseIntegerPayload(echo("K", "3960")).value_or(-1), 3960);
    QCOMPARE(Rt21::parseIntegerPayload(echo("R", "361")).value_or(-1), 361);
    QCOMPARE(Rt21::parseIntegerPayload(echo("S", "361")).value_or(-1), 361);
    QCOMPARE(Rt21::parseIntegerPayload(echo("T", "2576")).value_or(-1), 2576);
    QCOMPARE(Rt21::payloadAfterSoh(echo("1", "RT-21 4.14.g1")), QStringLiteral("RT-21 4.14.g1"));
    QCOMPARE(Rt21::payloadAfterSoh(echo("0", "0334")), QStringLiteral("0334"));
    QCOMPARE(Rt21::parseIntegerPayload(echo("0", "0334")).value_or(-1), 334);
}

void TestCore::hardwareRx()
{
    // Captured from a healthy RT-21 session, 14:06–14:12, one TCP connection.
    const QByteArray plain = QByteArray::fromHex("3133312E393B");
    QCOMPARE(plain, QByteArray("131.9;"));
    Rt21::Framer framer;
    framer.append(plain);
    QList<QByteArray> frames = framer.take();
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0), QByteArray("131.9"));
    const auto heading = Rt21::parseHeadingFrame(frames.at(0));
    // Serial BI1 replies are "37.7;" and sometimes "37.7\r" with no extra terminator.
    Rt21::Framer serialReply;
    serialReply.append(QByteArray("37.7;"));
    QCOMPARE(serialReply.take().value(0), QByteArray("37.7"));
    serialReply.append(QByteArray("37.7\r"));
    QCOMPARE(serialReply.take().value(0), QByteArray("37.7"));
    QVERIFY(heading);
    QVERIFY(std::abs(*heading - 131.9) < 1e-9);

    const QByteArray idle = QByteArray::fromHex("0130003E3133322E343B");
    framer.append(idle);
    frames = framer.take();
    QCOMPARE(frames.size(), 1);
    const auto stopped = Rt21::parseStatusFrame(frames.at(0));
    QVERIFY(stopped);
    QVERIFY(stopped->haveStatus);
    QCOMPARE(stopped->status, 0);
    QVERIFY(std::abs(stopped->azimuth - 132.4) < 1e-9);
    const auto stoppedHeading = Rt21::parseHeadingFrame(frames.at(0));
    QVERIFY(stoppedHeading);
    QVERIFY(std::abs(*stoppedHeading - 132.4) < 1e-9);

    const QByteArray turning = QByteArray::fromHex("0130013E3132322E363B");
    framer.append(turning);
    frames = framer.take();
    QCOMPARE(frames.size(), 1);
    const auto moving = Rt21::parseStatusFrame(frames.at(0));
    QVERIFY(moving);
    QVERIFY(moving->haveStatus);
    QCOMPARE(moving->status, 1);
    QVERIFY(std::abs(moving->azimuth - 122.6) < 1e-9);

    Rt21::Framer noisy;
    QByteArray r21frame;
    r21frame.append('\x01');
    r21frame.append('0');
    r21frame.append('\0');
    r21frame.append(">50.1;");
    noisy.append(r21frame);
    const QList<QByteArray> r21 = noisy.take();
    QCOMPARE(r21.size(), 1);
    const auto r21Heading = Rt21::parseHeadingFrame(r21.at(0));
    QVERIFY(r21Heading);
    QVERIFY(std::abs(*r21Heading - 50.1) < 1e-9);
    QCOMPARE(Rt21::payloadAfterSoh(QByteArray("1") + QByteArray(1, '\x01') + QByteArray("RT-21 4.13.2")),
             QStringLiteral("RT-21 4.13.2"));
    QByteArray stuffed;
    const QByteArray plainHeading("35.1;");
    for (char ch : plainHeading) {
        stuffed.append(ch);
        stuffed.append('\0');
    }
    noisy.append(stuffed);
    QCOMPARE(noisy.take().value(0), QByteArray("35.1"));
    QVERIFY(noisy.controlBytes() > 0);
}

void TestCore::solarPosition()
{
    auto closeTo = [](double value, double expected) {
        return std::abs(value - expected) <= 0.5;
    };
    const QDateTime denver = QDateTime(QDate(2003, 10, 17), QTime(19, 30, 30), Qt::UTC);
    const SolarPosition denverSun = solarAt(39.742476, -105.1786, denver);
    QVERIFY2(closeTo(denverSun.azimuth, 194.34024), qPrintable(QString::number(denverSun.azimuth, 'f', 3)));
    QVERIFY2(closeTo(denverSun.elevation, 39.888), qPrintable(QString::number(denverSun.elevation, 'f', 3)));

    const auto june = solarAt(36.4, -94.2, QDateTime(QDate(2024, 6, 21), QTime(18, 0), Qt::UTC));
    QVERIFY2(closeTo(june.azimuth, 161.351), qPrintable(QString::number(june.azimuth, 'f', 3)));
    QVERIFY2(closeTo(june.elevation, 76.422), qPrintable(QString::number(june.elevation, 'f', 3)));
    const auto december = solarAt(36.4, -94.2, QDateTime(QDate(2024, 12, 21), QTime(18, 0), Qt::UTC));
    QVERIFY2(closeTo(december.azimuth, 175.959), qPrintable(QString::number(december.azimuth, 'f', 3)));
    QVERIFY2(closeTo(december.elevation, 30.082), qPrintable(QString::number(december.elevation, 'f', 3)));
    const auto march = solarAt(36.4, -94.2, QDateTime(QDate(2024, 3, 20), QTime(14, 30), Qt::UTC));
    QVERIFY2(closeTo(march.azimuth, 109.816), qPrintable(QString::number(march.azimuth, 'f', 3)));
    QVERIFY2(closeTo(march.elevation, 25.016), qPrintable(QString::number(march.elevation, 'f', 3)));
}

void TestCore::reconnectTiming()
{
    QCOMPARE(reconnectDelayMs(0, 0, false), 20000);
    QCOMPARE(reconnectDelayMs(20000, 1, false), 30000);
    QCOMPARE(reconnectDelayMs(90000, 2, false), 60000);
    QCOMPARE(reconnectDelayMs(0, 0, true), 15000);
    QCOMPARE(reconnectDelayMs(90LL * 1000, 0, true), 15000);
    QCOMPARE(reconnectDelayMs(3LL * 60 * 1000, 0, true), 30000);
    QCOMPARE(reconnectDelayMs(3LL * 60 * 1000, 4, true), 5 * 60 * 1000);
    QCOMPARE(lastSeenPhrase(2), QStringLiteral("Last seen just now"));
    QCOMPARE(lastSeenPhrase(90), QStringLiteral("Last seen 1 min ago"));
    QCOMPARE(lastSeenPhrase(7200), QStringLiteral("Last seen 2 h ago"));
}

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
