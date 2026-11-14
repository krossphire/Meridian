/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Azimuthal equidistant globe. Coastlines and borders are Natural Earth
 * vectors projected once per home location (off the UI thread) and stroked
 * at device resolution. Heading, beam, and hover stay live on top of that cache.
 */

#pragma once

#include "map/GeoMap.h"

#include <QImage>
#include <QWidget>

#include <memory>
#include <optional>

class QLineEdit;
class QComboBox;
class QThread;
class QTimer;

class GlobeWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Link { Off, Connecting, On, Moving, Failed };

    explicit GlobeWidget(QWidget *parent = nullptr);
    ~GlobeWidget() override;

    void setLayers(const GeoLayer *low, const GeoLayer *high);
    void setHome(double lat, double lon);
    void setHeading(std::optional<double> azimuth);
    void setTarget(std::optional<double> azimuth);
    void setLink(Link state, const QString &detail);
    void setRotors(const QStringList &names, int active);

    void openHeadingEditor();
    bool mapReady() const;
    void setForcedDevicePixelRatio(qreal dpr);
    QImage renderSharp() const;

    double homeLat() const { return homeLat_; }
    double homeLon() const { return homeLon_; }

signals:
    void bearingRequested(double azimuth);
    void headingEdited(double azimuth);
    void stopRequested();
    void settingsRequested();
    void rotorSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int detailLevel() const;
    qreal activeDpr() const;
    qreal headingPixels() const;
    qreal beamWidth() const;
    QRectF disk() const;
    QRectF headingBounds() const;
    QFont headingFont() const;
    void requestMap(bool immediate);
    void publishMail();
    void placeChrome();
    void paintScene(QPainter &painter) const;
    void hoverAt(QPointF pos);
    void commitHeading();
    void cancelHeading();

    const GeoLayer *low = nullptr;
    const GeoLayer *high = nullptr;
    double homeLat_ = 36.5;
    double homeLon_ = -95.0;
    bool haveHome = false;
    std::optional<double> heading;
    std::optional<double> target;
    Link link = Link::Off;
    QString linkDetail;
    int hoverIndex = -1;
    QString hoverName;
    QPointF hoverPos;
    qreal forcedDpr = 0;

    QImage cache;
    QSize cacheSize;
    qreal cacheDpr = 0;
    int cacheLod = -1;
    quint64 cacheGen = 0;
    quint64 requestGen = 0;
    std::shared_ptr<const ProjectedLayer> lowProjected;
    std::shared_ptr<const ProjectedLayer> highProjected;

    QThread *thread = nullptr;
    class GlobeMapWorker *worker = nullptr;
    QTimer *debounce = nullptr;

    QWidget *gear = nullptr;
    QWidget *dot = nullptr;
    QLineEdit *editor = nullptr;
    QComboBox *rotors = nullptr;
    bool editorBusy = false;
};
