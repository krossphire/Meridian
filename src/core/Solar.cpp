/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Solar.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kDeg = M_PI / 180.0;

double wrap360(double degrees)
{
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped < 0.0)
        wrapped += 360.0;
    return wrapped;
}

double wrapMinutes(double minutes)
{
    double wrapped = std::fmod(minutes, 1440.0);
    if (wrapped < 0.0)
        wrapped += 1440.0;
    return wrapped;
}

double julianDay(const QDateTime &utc)
{
    const QDateTime when = utc.toUTC();
    int year = when.date().year();
    int month = when.date().month();
    const int day = when.date().day();
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    const int century = year / 100;
    const int gregorian = 2 - century + century / 4;
    const double midnight = std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day
                            + gregorian - 1524.5;
    const QTime clock = when.time();
    const double fraction = (clock.hour() + clock.minute() / 60.0 + (clock.second() + clock.msec() / 1000.0) / 3600.0)
                            / 24.0;
    return midnight + fraction;
}

double meanLongitude(double century)
{
    return wrap360(280.46646 + century * (36000.76983 + century * 0.0003032));
}

double meanAnomaly(double century)
{
    return wrap360(357.52911 + century * (35999.05029 - 0.0001537 * century));
}

double eccentricity(double century)
{
    return 0.016708634 - century * (0.000042037 + 0.0000001267 * century);
}

double equationOfCenter(double century)
{
    const double anomaly = meanAnomaly(century) * kDeg;
    return std::sin(anomaly) * (1.914602 - century * (0.004817 + 0.000014 * century))
           + std::sin(2.0 * anomaly) * (0.019993 - 0.000101 * century) + std::sin(3.0 * anomaly) * 0.000289;
}

double obliquity(double century)
{
    const double seconds = 21.448 - century * (46.8150 + century * (0.00059 - century * 0.001813));
    const double mean = 23.0 + (26.0 + seconds / 60.0) / 60.0;
    const double omega = 125.04 - 1934.136 * century;
    return mean + 0.00256 * std::cos(omega * kDeg);
}

double declination(double century)
{
    const double apparent = meanLongitude(century) + equationOfCenter(century) - 0.00569
                            - 0.00478 * std::sin((125.04 - 1934.136 * century) * kDeg);
    const double sine = std::sin(obliquity(century) * kDeg) * std::sin(apparent * kDeg);
    return std::asin(std::clamp(sine, -1.0, 1.0)) / kDeg;
}

double equationOfTime(double century)
{
    const double obl = obliquity(century);
    const double geom = meanLongitude(century);
    const double ecc = eccentricity(century);
    const double anomaly = meanAnomaly(century);
    const double y = std::pow(std::tan(obl * kDeg / 2.0), 2.0);
    const double sin2l = std::sin(2.0 * geom * kDeg);
    const double sinm = std::sin(anomaly * kDeg);
    const double cos2l = std::cos(2.0 * geom * kDeg);
    const double sin4l = std::sin(4.0 * geom * kDeg);
    const double sin2m = std::sin(2.0 * anomaly * kDeg);
    const double minutes = y * sin2l - 2.0 * ecc * sinm + 4.0 * ecc * y * sinm * cos2l - 0.5 * y * y * sin4l
                           - 1.25 * ecc * ecc * sin2m;
    return (minutes / kDeg) * 4.0;
}

double refractedElevation(double latitude, double longitude, double decl, double eqMinutes, double utcMinutes)
{
    const double solarTime = wrapMinutes(utcMinutes + eqMinutes + 4.0 * longitude);
    double hourAngle = solarTime / 4.0 - 180.0;
    if (hourAngle < -180.0)
        hourAngle += 360.0;
    const double lat = latitude * kDeg;
    const double dec = decl * kDeg;
    const double ha = hourAngle * kDeg;
    const double cosine = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
    const double zenith = std::acos(std::clamp(cosine, -1.0, 1.0)) / kDeg;
    const double geometric = 90.0 - zenith;

    double correction = 0.0;
    if (geometric <= 85.0) {
        const double te = std::tan(geometric * kDeg);
        if (geometric > 5.0)
            correction = 58.1 / te - 0.07 / (te * te * te) + 0.000086 / (te * te * te * te * te);
        else if (geometric > -0.575)
            correction = 1735.0 + geometric * (-518.2 + geometric * (103.4 + geometric * (-12.79 + geometric * 0.711)));
        else
            correction = -20.774 / te;
        correction /= 3600.0;
    }
    return geometric + correction;
}

double azimuthOf(double latitude, double longitude, double decl, double eqMinutes, double utcMinutes)
{
    const double solarTime = wrapMinutes(utcMinutes + eqMinutes + 4.0 * longitude);
    double hourAngle = solarTime / 4.0 - 180.0;
    if (hourAngle < -180.0)
        hourAngle += 360.0;
    const double lat = latitude * kDeg;
    const double dec = decl * kDeg;
    const double ha = hourAngle * kDeg;
    const double cosine = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
    const double zenith = std::acos(std::clamp(cosine, -1.0, 1.0)) / kDeg;
    const double denom = std::cos(lat) * std::sin(zenith * kDeg);
    double azimuth = 0.0;
    if (std::abs(denom) > 0.001) {
        double cosAz = (std::sin(lat) * std::cos(zenith * kDeg) - std::sin(dec)) / denom;
        azimuth = 180.0 - std::acos(std::clamp(cosAz, -1.0, 1.0)) / kDeg;
        if (hourAngle > 0.0)
            azimuth = -azimuth;
    } else {
        azimuth = latitude > 0.0 ? 180.0 : 0.0;
    }
    return wrap360(azimuth);
}

double utcMinutesOf(const QDateTime &utc)
{
    const QTime clock = utc.toUTC().time();
    return clock.hour() * 60.0 + clock.minute() + (clock.second() + clock.msec() / 1000.0) / 60.0;
}

} // namespace

SolarPosition solarAt(double latitudeDeg, double longitudeDeg, const QDateTime &utc)
{
    const double century = (julianDay(utc) - 2451545.0) / 36525.0;
    const double minutes = utcMinutesOf(utc);
    SolarPosition sun;
    sun.declination = declination(century);
    sun.equationOfTime = equationOfTime(century);
    sun.elevation = refractedElevation(latitudeDeg, longitudeDeg, sun.declination, sun.equationOfTime, minutes);
    sun.azimuth = azimuthOf(latitudeDeg, longitudeDeg, sun.declination, sun.equationOfTime, minutes);
    sun.subsolarLatitude = sun.declination;
    double subLon = (720.0 - minutes - sun.equationOfTime) / 4.0;
    while (subLon > 180.0)
        subLon -= 360.0;
    while (subLon < -180.0)
        subLon += 360.0;
    sun.subsolarLongitude = subLon;
    return sun;
}

double solarElevation(double latitudeDeg, double longitudeDeg, const SolarPosition &sun, double utcMinutes)
{
    return refractedElevation(latitudeDeg, longitudeDeg, sun.declination, sun.equationOfTime, utcMinutes);
}
