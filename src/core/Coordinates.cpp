/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Coordinates.h"

#include "Gridsquare.h"

#include <QRegularExpression>
#include <QVector>

#include <cmath>

namespace {

struct Comp {
    QVector<double> nums;
    QChar hemi;
};

QString normalizeAngleText(QString text)
{
    text = text.trimmed();
    const QList<QChar> symbols = {
        QChar(0x00B0), QChar(0x00BA), QChar(0x2032), QChar(0x2033),
        QChar(0x2018), QChar(0x2019), QChar(0x201C), QChar(0x201D),
        QLatin1Char('\''), QLatin1Char('"')};
    for (const QChar symbol : symbols)
        text.replace(symbol, QLatin1Char(' '));
    text.replace(QLatin1Char(','), QLatin1Char(' '));
    text.replace(QRegularExpression(QStringLiteral("(?<=\\d)[dD](?=\\s*\\d)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?<=\\d)[mM](?=\\s*\\d)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?<=\\d)[sS](?=\\s*(?:[NSEWnsew]|$))")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

enum class TokKind { Number, Hemi };

struct Tok {
    TokKind kind = TokKind::Number;
    double number = 0;
    QChar hemi;
};

bool tokenize(const QString &text, QList<Tok> &out)
{
    int i = 0;
    while (i < text.size()) {
        if (text.at(i).isSpace()) {
            ++i;
            continue;
        }
        const QChar upper = text.at(i).toUpper();
        if (upper == QLatin1Char('N') || upper == QLatin1Char('S') || upper == QLatin1Char('E')
            || upper == QLatin1Char('W')) {
            Tok tok;
            tok.kind = TokKind::Hemi;
            tok.hemi = upper;
            out.append(tok);
            ++i;
            continue;
        }
        const int start = i;
        if (text.at(i) == QLatin1Char('+') || text.at(i) == QLatin1Char('-'))
            ++i;
        int digits = 0;
        bool dot = false;
        while (i < text.size()) {
            const QChar c = text.at(i);
            if (c.isDigit()) {
                ++digits;
                ++i;
            } else if (c == QLatin1Char('.') && !dot) {
                dot = true;
                ++i;
            } else {
                break;
            }
        }
        if (digits == 0)
            return false;
        bool ok = false;
        const double value = text.mid(start, i - start).toDouble(&ok);
        if (!ok)
            return false;
        Tok tok;
        tok.kind = TokKind::Number;
        tok.number = value;
        out.append(tok);
    }
    return !out.isEmpty();
}

QVector<Comp> componentsFrom(const QList<Tok> &toks)
{
    QVector<Comp> comps;
    Comp cur;
    const auto flush = [&]() {
        if (!cur.nums.isEmpty()) {
            comps.append(cur);
            cur = {};
        }
    };
    for (const Tok &tok : toks) {
        if (tok.kind == TokKind::Hemi) {
            if (cur.hemi.isNull() && cur.nums.isEmpty()) {
                cur.hemi = tok.hemi;
            } else if (cur.hemi.isNull()) {
                cur.hemi = tok.hemi;
                flush();
            } else {
                flush();
                cur.hemi = tok.hemi;
            }
        } else {
            cur.nums.append(tok.number);
        }
    }
    flush();
    return comps;
}

bool componentDegrees(const Comp &comp, double &out)
{
    if (comp.nums.isEmpty() || comp.nums.size() > 3)
        return false;
    double degrees = comp.nums.at(0);
    double sign = 1.0;
    if (degrees < 0.0) {
        sign = -1.0;
        degrees = -degrees;
    }
    if (comp.hemi == QLatin1Char('S') || comp.hemi == QLatin1Char('W')) {
        if (sign < 0.0)
            return false;
        sign = -1.0;
    } else if (comp.hemi == QLatin1Char('N') || comp.hemi == QLatin1Char('E')) {
        if (sign < 0.0)
            return false;
    }
    if (comp.nums.size() > 1 && degrees != std::floor(degrees))
        return false;
    double minutes = 0;
    double seconds = 0;
    if (comp.nums.size() >= 2)
        minutes = comp.nums.at(1);
    if (comp.nums.size() == 3)
        seconds = comp.nums.at(2);
    if (minutes < 0.0 || seconds < 0.0 || minutes >= 60.0 || seconds >= 60.0)
        return false;
    out = sign * (degrees + minutes / 60.0 + seconds / 3600.0);
    return true;
}

bool hemiIsLat(QChar h)
{
    return h == QLatin1Char('N') || h == QLatin1Char('S');
}

bool hemiIsLon(QChar h)
{
    return h == QLatin1Char('E') || h == QLatin1Char('W');
}

} // namespace

QString formatLatLon(double lat, double lon)
{
    return QStringLiteral("%1°%2, %3°%4")
        .arg(std::abs(lat), 0, 'f', 4)
        .arg(lat >= 0 ? QStringLiteral("N") : QStringLiteral("S"))
        .arg(std::abs(lon), 0, 'f', 4)
        .arg(lon >= 0 ? QStringLiteral("E") : QStringLiteral("W"));
}

QString ResolvedLocation::summary() const
{
    const QString coords = formatLatLon(latitude, longitude);
    if (source == Source::Coordinates)
        return coords;
    const QString shown = entered.isEmpty() ? grid : entered;
    return coords + QStringLiteral("  ·  ") + shown;
}

QString ResolvedLocation::detail() const
{
    switch (source) {
    case Source::Grid:
        return QStringLiteral("Center of the Maidenhead square");
    case Source::Zip:
        return QStringLiteral("US Census 2020 ZCTA centroid");
    case Source::Coordinates:
        if (grid.isEmpty())
            return QStringLiteral("Bearings use these coordinates.");
        return QStringLiteral("Bearings use these coordinates. %1 is the 6-character square for display only.")
            .arg(grid);
    }
    return {};
}

std::optional<ResolvedLocation> resolveLocation(const QString &textIn, const ZipDatabase *zips, QString *error)
{
    const auto fail = [&](const QString &message) -> std::optional<ResolvedLocation> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    const QString text = textIn.trimmed();
    if (text.isEmpty())
        return fail(QStringLiteral("Enter a grid, ZIP code, or coordinates."));

    if (const QString grid = Gridsquare::normalize(text); !grid.isEmpty()) {
        ResolvedLocation loc;
        if (!Gridsquare::toLatLon(grid, loc.latitude, loc.longitude))
            return fail(QStringLiteral("That grid square isn't valid."));
        loc.entered = grid;
        loc.grid = Gridsquare::fromLatLon(loc.latitude, loc.longitude);
        loc.source = ResolvedLocation::Source::Grid;
        return loc;
    }

    static const QRegularExpression zipRe(QStringLiteral("^\\d{5}(?:-\\d{4})?$"));
    if (zipRe.match(text).hasMatch()) {
        if (!zips)
            return fail(QStringLiteral("The ZIP code table isn't loaded."));
        double lat = 0;
        double lon = 0;
        if (!zips->lookup(text, lat, lon)) {
            return fail(QStringLiteral(
                "No centroid for ZIP %1. The table is the 2020 Census ZCTA gazetteer, "
                "which leaves out some PO-box-only codes.")
                            .arg(text.left(5)));
        }
        ResolvedLocation loc;
        loc.latitude = lat;
        loc.longitude = lon;
        loc.grid = Gridsquare::fromLatLon(lat, lon);
        loc.source = ResolvedLocation::Source::Zip;
        return loc;
    }

    const QString normalized = normalizeAngleText(text);
    QList<Tok> toks;
    if (!tokenize(normalized, toks))
        return fail(QStringLiteral("Couldn't parse that location."));

    QVector<Comp> comps = componentsFrom(toks);
    if (comps.size() == 1 && comps.at(0).hemi.isNull() && comps.at(0).nums.size() == 2) {
        Comp lon = comps.at(0);
        lon.nums = {comps.at(0).nums.at(1)};
        comps[0].nums = {comps.at(0).nums.at(0)};
        comps.append(lon);
    }
    if (comps.size() != 2)
        return fail(QStringLiteral("Enter latitude and longitude, a grid square, or a ZIP code."));

    double a = 0;
    double b = 0;
    if (!componentDegrees(comps.at(0), a) || !componentDegrees(comps.at(1), b))
        return fail(QStringLiteral("Couldn't parse that coordinate. Minutes and seconds must be under 60."));

    bool firstIsLat = true;
    const QChar h0 = comps.at(0).hemi;
    const QChar h1 = comps.at(1).hemi;
    if (!h0.isNull() && !h1.isNull()) {
        if (hemiIsLat(h0) && hemiIsLon(h1))
            firstIsLat = true;
        else if (hemiIsLon(h0) && hemiIsLat(h1))
            firstIsLat = false;
        else
            return fail(QStringLiteral("Use N/S on the latitude and E/W on the longitude."));
    } else if (!h0.isNull() && !hemiIsLat(h0)) {
        return fail(QStringLiteral("Put latitude first, or mark it with N/S."));
    } else if (!h1.isNull() && !hemiIsLon(h1)) {
        return fail(QStringLiteral("Longitude needs an E/W marker when it isn't a signed number."));
    }

    const double lat = firstIsLat ? a : b;
    const double lon = firstIsLat ? b : a;
    if (std::abs(lat) > 90.0 || std::abs(lon) > 180.0)
        return fail(QStringLiteral("Latitude must be within 90° and longitude within 180°."));

    ResolvedLocation loc;
    loc.latitude = lat;
    loc.longitude = lon;
    // Display label only. Callers must not turn this grid back into a center.
    loc.grid = Gridsquare::fromLatLon(lat, lon);
    loc.source = ResolvedLocation::Source::Coordinates;
    return loc;
}

bool parseAxis(const QString &textIn, bool latitude, double &out, QString *error)
{
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    QString text = textIn.trimmed().toUpper();
    if (text.isEmpty())
        return fail(latitude ? QStringLiteral("Enter a latitude.") : QStringLiteral("Enter a longitude."));
    text.replace(QChar(0x00B0), QLatin1Char(' '));
    text.replace(QChar(0x2032), QLatin1Char(' '));
    text.replace(QChar(0x2033), QLatin1Char(' '));
    text.replace(QLatin1Char('\''), QLatin1Char(' '));
    text.replace(QLatin1Char('"'), QLatin1Char(' '));
    const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QChar hemi;
    QStringList numbers;
    const auto takeHemi = [&](QChar mark) {
        if (!hemi.isNull())
            return fail(QStringLiteral("Use one hemisphere marker."));
        hemi = mark;
        return true;
    };
    for (QString part : parts) {
        if (part == QLatin1String("N") || part == QLatin1String("S") || part == QLatin1String("E")
            || part == QLatin1String("W")) {
            if (!takeHemi(part.at(0)))
                return false;
            continue;
        }
        if (part.size() > 1
            && (part.back() == QLatin1Char('N') || part.back() == QLatin1Char('S') || part.back() == QLatin1Char('E')
                || part.back() == QLatin1Char('W'))) {
            if (!takeHemi(part.back()))
                return false;
            part.chop(1);
        } else if (part.size() > 1
                   && (part.front() == QLatin1Char('N') || part.front() == QLatin1Char('S')
                       || part.front() == QLatin1Char('E') || part.front() == QLatin1Char('W'))) {
            if (!takeHemi(part.front()))
                return false;
            part = part.mid(1);
        }
        numbers.append(part);
    }
    if (numbers.isEmpty() || numbers.size() > 3)
        return fail(QStringLiteral("Couldn't parse that coordinate."));
    if (latitude && !hemi.isNull() && hemi != QLatin1Char('N') && hemi != QLatin1Char('S'))
        return fail(QStringLiteral("Latitude takes N or S."));
    if (!latitude && !hemi.isNull() && hemi != QLatin1Char('E') && hemi != QLatin1Char('W'))
        return fail(QStringLiteral("Longitude takes E or W."));
    bool negative = false;
    QString first = numbers.at(0);
    if (first.startsWith(QLatin1Char('+')) || first.startsWith(QLatin1Char('-'))) {
        negative = first.startsWith(QLatin1Char('-'));
        first = first.mid(1);
    }
    bool ok = false;
    const double degrees = first.toDouble(&ok);
    if (!ok || first.isEmpty() || degrees < 0.0)
        return fail(QStringLiteral("Couldn't parse that coordinate."));
    if (numbers.size() > 1 && degrees != std::floor(degrees))
        return fail(QStringLiteral("Couldn't parse that coordinate. Minutes and seconds must be under 60."));
    double minutes = 0;
    double seconds = 0;
    if (numbers.size() >= 2) {
        minutes = numbers.at(1).toDouble(&ok);
        if (!ok)
            return fail(QStringLiteral("Couldn't parse that coordinate."));
    }
    if (numbers.size() == 3) {
        seconds = numbers.at(2).toDouble(&ok);
        if (!ok)
            return fail(QStringLiteral("Couldn't parse that coordinate."));
    }
    if (minutes < 0.0 || seconds < 0.0 || minutes >= 60.0 || seconds >= 60.0)
        return fail(QStringLiteral("Couldn't parse that coordinate. Minutes and seconds must be under 60."));
    if (!hemi.isNull() && negative)
        return fail(QStringLiteral("Use a sign or a hemisphere, not both."));
    const double magnitude = degrees + minutes / 60.0 + seconds / 3600.0;
    const double value = !hemi.isNull() ? ((hemi == QLatin1Char('S') || hemi == QLatin1Char('W')) ? -magnitude : magnitude)
                                         : (negative ? -magnitude : magnitude);
    if (latitude && std::abs(value) > 90.0)
        return fail(QStringLiteral("Latitude must be within 90°."));
    if (!latitude && std::abs(value) > 180.0)
        return fail(QStringLiteral("Longitude must be within 180°."));
    out = value;
    return true;
}

std::optional<ResolvedLocation> resolveHome(const QString &place, const QString &latitude, const QString &longitude,
                                            const ZipDatabase *zips, QString *error)
{
    const auto fail = [&](const QString &message) -> std::optional<ResolvedLocation> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const QString latText = latitude.trimmed();
    const QString lonText = longitude.trimmed();
    if (latText.isEmpty() != lonText.isEmpty())
        return fail(QStringLiteral("Enter both latitude and longitude, or leave both blank."));
    if (!latText.isEmpty()) {
        double lat = 0;
        double lon = 0;
        if (!parseAxis(latText, true, lat, error) || !parseAxis(lonText, false, lon, error))
            return std::nullopt;
        ResolvedLocation loc;
        loc.latitude = lat;
        loc.longitude = lon;
        loc.grid = Gridsquare::fromLatLon(lat, lon);
        loc.source = ResolvedLocation::Source::Coordinates;
        return loc;
    }
    if (place.trimmed().isEmpty())
        return fail(QStringLiteral("Set your grid in Settings"));
    return resolveLocation(place, zips, error);
}
