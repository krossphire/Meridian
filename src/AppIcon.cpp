/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "AppIcon.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>

void paintMeridianMark(QPainter &painter, const QRectF &bounds)
{
    const qreal scale = bounds.width() / 128.0;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(16, 22, 27));
    painter.setPen(QPen(QColor(214, 222, 214), 4 * scale));
    painter.drawEllipse(QRectF(bounds.left() + 8 * scale, bounds.top() + 8 * scale, 112 * scale, 112 * scale));
    painter.setPen(QPen(QColor(255, 191, 0), 6 * scale, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(bounds.left() + 64 * scale, bounds.top() + 64 * scale),
                     QPointF(bounds.left() + 96 * scale, bounds.top() + 34 * scale));
    painter.setBrush(QColor(255, 191, 0));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(bounds.left() + 64 * scale, bounds.top() + 64 * scale), 5 * scale, 5 * scale);
}

QIcon meridianIcon()
{
    QPixmap pixmap(128, 128);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paintMeridianMark(painter, QRectF(0, 0, 128, 128));
    return QIcon(pixmap);
}
