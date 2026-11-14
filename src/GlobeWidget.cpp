/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GlobeWidget.h"

#include "core/Bearing.h"
#include "core/Projection.h"
#include "core/Solar.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QEnterEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QThread>
#include <QTimer>
#include <QToolTip>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace {

const QColor kWindow(11, 15, 18);
const QColor kOcean(16, 22, 27);
const QColor kLand(36, 49, 54);
const QColor kHover(62, 96, 98);
const QColor kCoast(214, 222, 214, 225);
const QColor kBorder(168, 186, 178, 150);
const QColor kBeam(255, 191, 0);
const QColor kInk(244, 246, 244);

struct Published {
    QMutex mutex;
    QImage image;
    QImage lines;
    QSize size;
    qreal dpr = 1;
    int lod = -1;
    quint64 gen = 0;
    std::shared_ptr<ProjectedLayer> low;
    std::shared_ptr<ProjectedLayer> high;
    double lat = 999;
    double lon = 999;
};

class GearButton : public QAbstractButton
{
public:
    explicit GearButton(QWidget *parent)
        : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Settings"));
        setFocusPolicy(Qt::NoFocus);
        renderer.load(QStringLiteral(":/icons/gear.svg"));
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        update();
        QAbstractButton::enterEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        update();
        QAbstractButton::leaveEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setOpacity(underMouse() ? 1.0 : 0.48);
        const qreal inset = width() * 0.22;
        renderer.render(&painter, QRectF(inset, inset, width() - 2 * inset, height() - 2 * inset));
    }

private:
    QSvgRenderer renderer;
};

class StatusDot : public QWidget
{
public:
    explicit StatusDot(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    void setDot(const QColor &next, const QString &tip)
    {
        color = next;
        setToolTip(tip);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal s = qMin(width(), height()) - 2.0;
        painter.drawEllipse(QRectF((width() - s) / 2.0, (height() - s) / 2.0, s, s));
    }

private:
    QColor color{120, 128, 126};
};

class HeadingEdit : public QLineEdit
{
public:
    explicit HeadingEdit(QWidget *parent)
        : QLineEdit(parent)
    {
        setAlignment(Qt::AlignCenter);
        setFrame(false);
        setObjectName(QStringLiteral("headingEdit"));
        setStyleSheet(QStringLiteral(
            "QLineEdit#headingEdit { background: transparent; color: #f4f6f4; border: none;"
            " border-bottom: 2px solid rgba(255,191,0,200); border-radius: 0; padding: 0 2px 1px 2px;"
            " selection-background-color: rgba(255,191,0,70); selection-color: #f7f4ea; }"));
    }
    std::function<void()> commitHook;
    std::function<void()> cancelHook;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            if (cancelHook)
                cancelHook();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (commitHook)
                commitHook();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override
    {
        QLineEdit::focusOutEvent(event);
        if (commitHook)
            commitHook();
    }
};

QPen cosmeticPen(const QColor &color, qreal dip, qreal dpr)
{
    QPen pen(color);
    pen.setCosmetic(true);
    pen.setWidthF(std::max(1.0, dip * dpr));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

struct MapFrames {
    QImage base;
    QImage lines;
};

MapFrames renderProjected(const ProjectedLayer &layer, QSize logical, qreal dpr, int lod)
{
    MapFrames frames;
    frames.base = QImage(qRound(logical.width() * dpr), qRound(logical.height() * dpr), QImage::Format_ARGB32_Premultiplied);
    frames.base.setDevicePixelRatio(dpr);
    frames.base.fill(kWindow);
    frames.lines = QImage(frames.base.size(), QImage::Format_ARGB32_Premultiplied);
    frames.lines.setDevicePixelRatio(dpr);
    frames.lines.fill(Qt::transparent);
    if (logical.isEmpty())
        return frames;

    const qreal side = std::max(2.0, qMin(logical.width(), logical.height()) - 2.0);
    const qreal radius = side / 2.0;
    const qreal cx = logical.width() / 2.0;
    const qreal cy = logical.height() / 2.0;
    QPainterPath disk;
    disk.addEllipse(QRectF(cx - radius, cy - radius, side, side));
    QTransform transform;
    transform.translate(cx, cy);
    transform.scale(radius, -radius);

    QPainter base(&frames.base);
    base.setRenderHint(QPainter::Antialiasing, true);
    base.setClipPath(disk);
    base.fillPath(disk, kOcean);
    base.save();
    base.setTransform(transform);
    base.setPen(Qt::NoPen);
    base.setBrush(kLand);
    for (const QPainterPath &land : layer.land)
        base.drawPath(land);
    base.restore();

    QPainter lines(&frames.lines);
    lines.setRenderHint(QPainter::Antialiasing, true);
    lines.setClipPath(disk);
    lines.save();
    lines.setTransform(transform);
    lines.setBrush(Qt::NoBrush);
    if (lod >= 1) {
        lines.setPen(cosmeticPen(kBorder, 0.75, dpr));
        for (const ProjectedCountry &country : layer.countries)
            lines.drawPath(country.path);
    }
    lines.setPen(cosmeticPen(kCoast, lod == 0 ? 1.2 : 1.15, dpr));
    for (const QPainterPath &land : layer.land)
        lines.drawPath(land);
    lines.restore();
    lines.setClipping(false);
    lines.setBrush(Qt::NoBrush);
    lines.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    lines.drawEllipse(QRectF(cx - radius, cy - radius, side, side));
    return frames;
}

void drawCountryLabels(QPainter &painter, const ProjectedLayer &layer, const QRectF &globe)
{
    const qreal radius = globe.width() / 2.0;
    const QPointF center = globe.center();
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(qRound(std::clamp(radius * 0.036, 11.0, 15.0)));
    font.setWeight(QFont::Medium);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    QVector<int> order(layer.countries.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return layer.countries.at(a).population > layer.countries.at(b).population;
    });

    QVector<QRectF> used;
    int drawn = 0;
    for (int index : order) {
        if (drawn >= 14)
            break;
        const ProjectedCountry &country = layer.countries.at(index);
        if (!country.labelOk || country.population < 15000000)
            continue;
        const QPointF at(center.x() + country.label.x() * radius, center.y() - country.label.y() * radius);
        const QString name = shortCountryName(country.name);
        const int textWidth = metrics.horizontalAdvance(name);
        QRectF box(at.x() - textWidth / 2.0, at.y() - metrics.height() / 2.0, textWidth, metrics.height());
        if (box.top() < globe.top() + globe.height() * 0.16)
            continue;
        bool crowded = false;
        for (const QRectF &taken : used) {
            if (taken.adjusted(-10, -6, 10, 6).intersects(box)) {
                crowded = true;
                break;
            }
        }
        if (crowded)
            continue;
        used.append(box);
        ++drawn;
        painter.setPen(QColor(0, 0, 0, 160));
        painter.drawText(box.translated(0, 1), Qt::AlignCenter, name);
        painter.setPen(QColor(220, 228, 222, 220));
        painter.drawText(box, Qt::AlignCenter, name);
    }
}

} // namespace

class GlobeMapWorker : public QObject
{
    Q_OBJECT
public:
    explicit GlobeMapWorker(std::shared_ptr<Published> box)
        : box(std::move(box))
    {}

public slots:
    void build(quint64 gen, const GeoLayer *low, const GeoLayer *high, double lat, double lon, QSize size, qreal dpr,
               int lod)
    {
        if (!low || !high || size.isEmpty() || dpr <= 0)
            return;
        if (!projected || std::abs(projectedLat - lat) > 1e-7 || std::abs(projectedLon - lon) > 1e-7) {
            projected = std::make_shared<std::pair<ProjectedLayer, ProjectedLayer>>();
            projected->first = projectLayer(*low, lat, lon);
            projected->second = projectLayer(*high, lat, lon);
            projectedLat = lat;
            projectedLon = lon;
        }
        const ProjectedLayer &layer = lod == 0 ? projected->first : projected->second;
        MapFrames frames = renderProjected(layer, size, dpr, lod);
        {
            QMutexLocker lock(&box->mutex);
            box->image = std::move(frames.base);
            box->lines = std::move(frames.lines);
            box->size = size;
            box->dpr = dpr;
            box->lod = lod;
            box->gen = gen;
            if (!box->low || std::abs(box->lat - lat) > 1e-7 || std::abs(box->lon - lon) > 1e-7) {
                box->low = std::make_shared<ProjectedLayer>(projected->first);
                box->high = std::make_shared<ProjectedLayer>(projected->second);
                box->lat = lat;
                box->lon = lon;
            }
        }
        emit ready();
    }

signals:
    void ready();

private:
    std::shared_ptr<Published> box;
    std::shared_ptr<std::pair<ProjectedLayer, ProjectedLayer>> projected;
    double projectedLat = 999;
    double projectedLon = 999;
};

GlobeWidget::GlobeWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(160, 160);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    auto *gearButton = new GearButton(this);
    gear = gearButton;
    connect(gearButton, &QAbstractButton::clicked, this, [this]() { emit settingsRequested(); });
    dot = new StatusDot(this);
    rotors = new QComboBox(this);
    rotors->setObjectName(QStringLiteral("rotorPick"));
    rotors->setFocusPolicy(Qt::NoFocus);
    rotors->setCursor(Qt::PointingHandCursor);
    rotors->setStyleSheet(QStringLiteral(
        "QComboBox#rotorPick { background: rgba(12,16,20,150); color: #c9d4cf; border: none;"
        " border-radius: 8px; padding: 2px 8px; font-size: 11px; }"
        "QComboBox#rotorPick::drop-down { border: none; width: 14px; }"
        "QComboBox#rotorPick QAbstractItemView { background: #161c21; color: #e7eeea;"
        " selection-background-color: #2c4038; }"));
    connect(rotors, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
            emit rotorSelected(index);
    });

    auto *edit = new HeadingEdit(this);
    editor = edit;
    editor->hide();
    edit->commitHook = [this]() { commitHeading(); };
    edit->cancelHook = [this]() { cancelHeading(); };

    debounce = new QTimer(this);
    debounce->setSingleShot(true);
    connect(debounce, &QTimer::timeout, this, [this]() { requestMap(true); });

    auto box = std::make_shared<Published>();
    // The worker and the widget share the box. Keep it alive via the worker
    // and a copy captured in the ready handler through the worker's box.
    thread = new QThread(this);
    worker = new GlobeMapWorker(box);
    worker->moveToThread(thread);
    connect(worker, &GlobeMapWorker::ready, this, [this, box]() {
        QImage image;
        QImage lines;
        QSize size;
        qreal dpr = 1;
        int lod = -1;
        quint64 gen = 0;
        std::shared_ptr<ProjectedLayer> lowMap;
        std::shared_ptr<ProjectedLayer> highMap;
        {
            QMutexLocker lock(&box->mutex);
            image = box->image;
            lines = box->lines;
            size = box->size;
            dpr = box->dpr;
            lod = box->lod;
            gen = box->gen;
            lowMap = box->low;
            highMap = box->high;
        }
        Published snap;
        snap.image = image;
        snap.lines = lines;
        snap.size = size;
        snap.dpr = dpr;
        snap.lod = lod;
        snap.gen = gen;
        snap.low = lowMap;
        snap.high = highMap;
        if (snap.gen != requestGen)
            return;
        cache = snap.image;
        cacheLines = snap.lines;
        cacheSize = snap.size;
        cacheDpr = snap.dpr;
        cacheLod = snap.lod;
        cacheGen = snap.gen;
        if (snap.low)
            lowProjected = snap.low;
        if (snap.high)
            highProjected = snap.high;
        update();
    });
    thread->start();
    placeChrome();

    sunTimer = new QTimer(this);
    sunTimer->setInterval(60000);
    connect(sunTimer, &QTimer::timeout, this, [this]() {
        sunDirty = true;
        update();
    });
    sunTimer->start();
}

GlobeWidget::~GlobeWidget()
{
    thread->quit();
    thread->wait();
    delete worker;
}

void GlobeWidget::setLayers(const GeoLayer *lowLayer, const GeoLayer *highLayer)
{
    low = lowLayer;
    high = highLayer;
    requestMap(true);
}

void GlobeWidget::setHome(double lat, double lon)
{
    if (haveHome && std::abs(homeLat_ - lat) < 1e-8 && std::abs(homeLon_ - lon) < 1e-8)
        return;
    homeLat_ = lat;
    homeLon_ = lon;
    haveHome = true;
    hoverIndex = -1;
    sunDirty = true;
    requestMap(true);
}

void GlobeWidget::setHeading(std::optional<double> azimuth)
{
    heading = azimuth;
    update();
}

void GlobeWidget::setTarget(std::optional<double> azimuth)
{
    target = azimuth;
    update();
}

void GlobeWidget::setStatusLine(const QString &text)
{
    if (statusLine == text)
        return;
    statusLine = text;
    update();
}

void GlobeWidget::setHeadingStale(bool stale)
{
    if (headingStale == stale)
        return;
    headingStale = stale;
    update();
}

void GlobeWidget::setShowSun(bool show)
{
    if (showSun == show)
        return;
    showSun = show;
    sunDirty = true;
    update();
}

void GlobeWidget::setSolarPreview(const QDateTime &utc)
{
    solarPreview = utc;
    sunDirty = true;
    update();
}

void GlobeWidget::setLink(Link state, const QString &detail)
{
    link = state;
    linkDetail = detail;
    QColor color(125, 206, 160);
    if (state == Link::Failed || state == Link::Off)
        color = QColor(224, 108, 92);
    else if (state == Link::Connecting)
        color = QColor(224, 177, 92);
    else if (state == Link::Moving)
        color = kBeam;
    static_cast<StatusDot *>(dot)->setDot(color, detail);
    placeChrome();
    update();
}

void GlobeWidget::setRotors(const QStringList &names, int active)
{
    rotors->blockSignals(true);
    rotors->clear();
    rotors->addItems(names);
    if (active >= 0 && active < names.size())
        rotors->setCurrentIndex(active);
    rotors->blockSignals(false);
    placeChrome();
}

void GlobeWidget::openHeadingEditor()
{
    auto *edit = static_cast<HeadingEdit *>(editor);
    edit->setFont(headingFont());
    edit->setText(heading ? QString::number(*heading, 'f', 1) : QString());
    edit->setGeometry(headingBounds().toAlignedRect().adjusted(-8, -2, 8, 2));
    edit->show();
    edit->setFocus(Qt::OtherFocusReason);
    edit->selectAll();
    update();
}

bool GlobeWidget::mapReady() const
{
    return cacheGen == requestGen && !cache.isNull() && cacheSize == size()
           && std::abs(cacheDpr - activeDpr()) < 0.01 && cacheLod == detailLevel();
}

void GlobeWidget::setForcedDevicePixelRatio(qreal dpr)
{
    forcedDpr = dpr;
    requestMap(true);
}

QImage GlobeWidget::renderSharp()
{
    const qreal dpr = activeDpr();
    QImage image(qRound(width() * dpr), qRound(height() * dpr), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(kWindow);
    QPainter painter(&image);
    paintScene(painter);
    const auto children = findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children) {
        if (!child->isVisible())
            continue;
        child->render(&painter, child->pos());
    }
    return image;
}

int GlobeWidget::detailLevel() const
{
    const int side = std::min(width(), height());
    if (side < 280)
        return 0;
    if (side < 680)
        return 1;
    return 2;
}

qreal GlobeWidget::activeDpr() const
{
    return forcedDpr > 0 ? forcedDpr : devicePixelRatioF();
}

qreal GlobeWidget::headingPixels() const
{
    const int side = std::min(width(), height());
    return std::clamp(side * 0.105, 22.0, 80.0);
}

qreal GlobeWidget::beamWidth() const
{
    const int side = std::min(width(), height());
    if (side < 280)
        return 3.6;
    if (side < 520)
        return 2.5;
    return 2.1;
}

QRectF GlobeWidget::disk() const
{
    const qreal side = std::max(2.0, qMin(width(), height()) - 2.0);
    return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

QFont GlobeWidget::headingFont() const
{
    QFont font;
    const QStringList families = QFontDatabase::families();
    if (families.contains(QStringLiteral("Inter Medium")))
        font.setFamily(QStringLiteral("Inter Medium"));
    else if (families.contains(QStringLiteral("Inter"))) {
        font.setFamily(QStringLiteral("Inter"));
        font.setWeight(QFont::Medium);
    } else {
        font = this->font();
        font.setWeight(QFont::Medium);
    }
    font.setPixelSize(qRound(headingPixels()));
    return font;
}

QRectF GlobeWidget::headingBounds() const
{
    const QFont font = headingFont();
    const QFontMetrics metrics(font);
    const QString sample = QStringLiteral("359.9°");
    const qreal textWidth = metrics.horizontalAdvance(sample) + 8.0;
    const qreal textHeight = metrics.height();
    const QRectF globe = disk();
    const qreal top = globe.top() + std::max(4.0, globe.height() * 0.035);
    return QRectF(globe.center().x() - textWidth / 2.0, top, textWidth, textHeight);
}

void GlobeWidget::requestMap(bool immediate)
{
    if (!immediate) {
        debounce->start(40);
        return;
    }
    debounce->stop();
    if (!low || !high || !worker)
        return;
    ++requestGen;
    const quint64 gen = requestGen;
    const QSize widgetSize = size();
    const qreal dpr = activeDpr();
    const int lod = detailLevel();
    const double lat = homeLat_;
    const double lon = homeLon_;
    const GeoLayer *lowLayer = low;
    const GeoLayer *highLayer = high;
    QMetaObject::invokeMethod(worker,
                              [this, gen, lowLayer, highLayer, lat, lon, widgetSize, dpr, lod]() {
                                  worker->build(gen, lowLayer, highLayer, lat, lon, widgetSize, dpr, lod);
                              },
                              Qt::QueuedConnection);
}

void GlobeWidget::placeChrome()
{
    const QRectF globe = disk();
    const int side = std::min(width(), height());
    const bool tiny = side < 250;
    gear->setGeometry(int(globe.right()) - 40, int(globe.bottom()) - 40, 34, 34);
    gear->setVisible(!tiny || underMouse());
    dot->setGeometry(int(globe.left()) + 12, int(globe.bottom()) - 22, 10, 10);
    const bool showRotors = rotors->count() > 1 && side >= 260;
    rotors->setVisible(showRotors);
    rotors->setGeometry(dot->x() + 16, dot->y() - 6, std::min(160, int(globe.width() * 0.36)), 22);
    if (editor->isVisible()) {
        editor->setFont(headingFont());
        editor->setGeometry(headingBounds().toAlignedRect().adjusted(-8, -2, 8, 2));
    }
}

void GlobeWidget::ensureSun()
{
    if (!showSun || !haveHome) {
        sunOverlay = QImage();
        return;
    }
    const QDateTime utc = solarPreview.isValid() ? solarPreview : QDateTime::currentDateTimeUtc();
    const qint64 minute = utc.toSecsSinceEpoch() / 60;
    if (!sunDirty && minute == sunMinute && sunSize == size())
        return;
    sunDirty = false;
    sunMinute = minute;
    sunSize = size();
    const double utcMinutes = utc.time().hour() * 60.0 + utc.time().minute() + utc.time().second() / 60.0;
    sunNow = solarAt(homeLat_, homeLon_, utc);
    if (detailLevel() < 1) {
        sunOverlay = QImage();
        return;
    }

    // Night keeps the map as it is. Twilight is a faint band. Day is a little
    // more opaque than that band. Tunable, and soft enough to scale.
    constexpr double kNight = -6.0;
    constexpr double kDay = 6.0;
    constexpr int kTwilightAlpha = 28;
    constexpr int kDayAlpha = 46;

    const int sidePx = std::min(width(), height());
    const qreal dpr = activeDpr();
    const int limit = 720;
    const qreal drawScale = std::min(1.0, limit / std::max(1.0, sidePx * dpr));
    const int iw = std::max(1, qRound(width() * dpr * drawScale));
    const int ih = std::max(1, qRound(height() * dpr * drawScale));
    QImage overlay(iw, ih, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    const qreal side = std::max(2.0, qreal(std::min(width(), height())) - 2.0);
    const qreal radius = side / 2.0;
    const qreal cx = width() / 2.0;
    const qreal cy = height() / 2.0;
    const qreal sample = 1.0 / (dpr * drawScale);
    for (int y = 0; y < ih; ++y) {
        auto *row = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        const double ly = (y + 0.5) * sample;
        for (int x = 0; x < iw; ++x) {
            const double lx = (x + 0.5) * sample;
            const double ux = (lx - cx) / radius;
            const double uy = -(ly - cy) / radius;
            if (ux * ux + uy * uy > 1.0)
                continue;
            double lat = 0;
            double lon = 0;
            if (!unprojectAzimuthal(homeLat_, homeLon_, ux, uy, lat, lon))
                continue;
            const double elevation = solarElevation(lat, lon, sunNow, utcMinutes);
            if (elevation <= kNight)
                continue;
            int alpha = kDayAlpha;
            int red = 196;
            int green = 186;
            int blue = 156;
            if (elevation < 0.0) {
                const double t = (elevation - kNight) / (0.0 - kNight);
                alpha = int(kTwilightAlpha * t);
                red = 158;
                green = 166;
                blue = 170;
            } else if (elevation < kDay) {
                const double t = elevation / kDay;
                alpha = int(kTwilightAlpha + (kDayAlpha - kTwilightAlpha) * t);
                red = int(158 + (196 - 158) * t);
                green = int(166 + (186 - 166) * t);
                blue = int(170 + (156 - 170) * t);
            }
            if (alpha <= 0)
                continue;
            row[x] = qPremultiply(qRgba(red, green, blue, alpha));
        }
    }
    overlay.setDevicePixelRatio(dpr * drawScale);
    sunOverlay = overlay;
}

void GlobeWidget::paintScene(QPainter &painter)
{
    ensureSun();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(rect(), kWindow);

    const bool cacheMatches = !cache.isNull() && cacheSize == size() && std::abs(cacheDpr - activeDpr()) < 0.01
                              && cacheLod == detailLevel();
    if (!cache.isNull() && cacheMatches)
        painter.drawImage(QPointF(0, 0), cache);
    else if (!cache.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(rect(), cache);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    } else {
        painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
        painter.setBrush(kOcean);
        painter.drawEllipse(disk());
    }

    if (showSun && detailLevel() >= 1 && !sunOverlay.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(QPointF(0, 0), sunOverlay);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
    if (!cacheLines.isNull() && cacheMatches)
        painter.drawImage(QPointF(0, 0), cacheLines);
    else if (!cacheLines.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(rect(), cacheLines);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }

    const QRectF globe = disk();
    const QPointF center = globe.center();
    const qreal radius = globe.width() / 2.0;
    const int lod = detailLevel();

    if (lod >= 1 && hoverIndex >= 0) {
        const std::shared_ptr<const ProjectedLayer> layer = lod == 0 ? lowProjected : highProjected;
        if (layer) {
            const ProjectedCountry *country = nullptr;
            for (const ProjectedCountry &candidate : layer->countries) {
                if (candidate.index == hoverIndex) {
                    country = &candidate;
                    break;
                }
            }
            if (country) {
                painter.save();
                QPainterPath diskPath;
                diskPath.addEllipse(globe);
                painter.setClipPath(diskPath);
                QTransform transform;
                transform.translate(center.x(), center.y());
                transform.scale(radius, -radius);
                painter.setTransform(transform);
                painter.setPen(Qt::NoPen);
                painter.setBrush(kHover);
                painter.drawPath(country->path);
                painter.restore();

                if (!hoverName.isEmpty()) {
                    QFont font(QStringLiteral("Inter"));
                    font.setPixelSize(qRound(std::clamp(radius * 0.045, 11.0, 16.0)));
                    painter.setFont(font);
                    const QFontMetrics metrics(font);
                    const int textWidth = metrics.horizontalAdvance(hoverName);
                    QRectF chip(hoverPos.x() + 14, hoverPos.y() - metrics.height() - 10, textWidth + 16,
                                metrics.height() + 8);
                    if (chip.right() > width() - 8)
                        chip.moveLeft(hoverPos.x() - chip.width() - 14);
                    if (chip.top() < 8)
                        chip.moveTop(hoverPos.y() + 16);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(8, 12, 14, 185));
                    painter.drawRoundedRect(chip, 7, 7);
                    painter.setPen(kInk);
                    painter.drawText(chip, Qt::AlignCenter, hoverName);
                }
            }
        }
    }

    if (lod >= 2 && highProjected)
        drawCountryLabels(painter, *highProjected, globe);

    if (showSun && haveHome) {
        const UnitPoint spot = projectAzimuthal(homeLat_, homeLon_, sunNow.subsolarLatitude, sunNow.subsolarLongitude);
        const QPointF at(center.x() + spot.x * radius, center.y() - spot.y * radius);
        const qreal mark = std::clamp(radius * 0.028, 3.5, 7.0);
        const bool below = sunNow.elevation < 0.0;
        painter.setBrush(below ? Qt::NoBrush : QColor(232, 196, 120, 210));
        painter.setPen(QPen(QColor(232, 196, 120, below ? 90 : 220), below ? 1.2 : 1.0));
        painter.drawEllipse(at, mark, mark);
    }

    auto pointOn = [&](double azimuth, qreal rho) {
        const double radians = azimuth * M_PI / 180.0;
        return QPointF(center.x() + std::sin(radians) * radius * rho,
                       center.y() - std::cos(radians) * radius * rho);
    };

    if (target && (!heading || std::abs(std::remainder(*target - *heading, 360.0)) > 0.9)) {
        painter.setPen(QPen(QColor(255, 191, 0, 170), std::max(1.4, beamWidth() * 0.55), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(pointOn(*target, 0.90), pointOn(*target, 0.985));
    }

    if (heading) {
        if (lod > 0) {
            painter.setPen(QPen(QColor(255, 191, 0, 46), beamWidth() + 3.2, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(pointOn(*heading, 0.03), pointOn(*heading, 0.97));
        }
        painter.setPen(QPen(kBeam, beamWidth(), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(pointOn(*heading, 0.025), pointOn(*heading, 0.97));
        painter.setPen(Qt::NoPen);
        painter.setBrush(kBeam);
        painter.drawEllipse(center, std::max(2.4, beamWidth() * 0.85), std::max(2.4, beamWidth() * 0.85));
    }

    if (!editor->isVisible()) {
        const QString text = heading ? QString::number(*heading, 'f', 1) + QStringLiteral("°") : QStringLiteral("—");
        painter.setFont(headingFont());
        const QRectF box = headingBounds();
        const int inkAlpha = headingStale ? 110 : 255;
        painter.setPen(QColor(0, 0, 0, headingStale ? 70 : 150));
        painter.drawText(box.translated(0, 1.4), Qt::AlignCenter, text);
        painter.setPen(QColor(kInk.red(), kInk.green(), kInk.blue(), inkAlpha));
        painter.drawText(box, Qt::AlignCenter, text);
    }

    const QString lineText = !statusLine.isEmpty() ? statusLine : linkDetail;
    const bool showState = !statusLine.isEmpty()
                           || ((link == Link::Failed || link == Link::Off || link == Link::Connecting) && !linkDetail.isEmpty());
    if (showState && !lineText.isEmpty()) {
        QFont font(QStringLiteral("Inter"));
        font.setPixelSize(11);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QString line = metrics.elidedText(lineText, Qt::ElideRight, int(globe.width() * 0.72));
        const QRectF box(globe.left() + 18, globe.bottom() - 22, globe.width() * 0.72, 16);
        painter.setPen(QColor(210, 196, 184, 210));
        painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, line);
    }
}

void GlobeWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintScene(painter);
}

void GlobeWidget::resizeEvent(QResizeEvent *)
{
    sunDirty = true;
    placeChrome();
    requestMap(false);
}

void GlobeWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (!editor->isVisible() && headingBounds().adjusted(-8, -6, 8, 8).contains(event->position())) {
        openHeadingEditor();
        event->accept();
        return;
    }
    const QRectF globe = disk();
    if (const auto hit = bearingFromClick(event->position(), globe.center(), globe.width() / 2.0))
        emit bearingRequested(clampRt21Azimuth(hit->azimuth));
}

void GlobeWidget::mouseMoveEvent(QMouseEvent *event)
{
    hoverAt(event->position());
}

void GlobeWidget::hoverAt(QPointF pos)
{
    hoverPos = pos;
    if (detailLevel() < 1 || !high) {
        if (hoverIndex != -1) {
            hoverIndex = -1;
            hoverName.clear();
            update();
        }
        return;
    }
    const QRectF globe = disk();
    const qreal radius = globe.width() / 2.0;
    const double x = (pos.x() - globe.center().x()) / radius;
    const double y = (globe.center().y() - pos.y()) / radius;
    double lat = 0;
    double lon = 0;
    int found = -1;
    if (unprojectAzimuthal(homeLat_, homeLon_, x, y, lat, lon))
        found = countryAt(*high, lat, lon);
    if (found == hoverIndex)
        return;
    hoverIndex = found;
    hoverName = found >= 0 ? shortCountryName(high->countries.at(found).name) : QString();
    update();
}

void GlobeWidget::keyPressEvent(QKeyEvent *event)
{
    if (!editor->isVisible() && (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space)) {
        emit stopRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void GlobeWidget::enterEvent(QEnterEvent *event)
{
    placeChrome();
    QWidget::enterEvent(event);
}

void GlobeWidget::leaveEvent(QEvent *event)
{
    hoverIndex = -1;
    hoverName.clear();
    placeChrome();
    update();
    QWidget::leaveEvent(event);
}

void GlobeWidget::commitHeading()
{
    if (editorBusy || !editor->isVisible())
        return;
    editorBusy = true;
    const QString text = editor->text().trimmed();
    editor->hide();
    editorBusy = false;
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
        return;
    emit headingEdited(clampRt21Azimuth(value));
    update();
}

void GlobeWidget::cancelHeading()
{
    if (editorBusy)
        return;
    editorBusy = true;
    editor->hide();
    editor->clearFocus();
    editorBusy = false;
    update();
}

#include "GlobeWidget.moc"
