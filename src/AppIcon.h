/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

class QIcon;
class QPainter;
class QRectF;

// Dark disc, pale ring, amber needle. This is the window icon, and the
// Windows .ico is a raster of the same drawing.
void paintMeridianMark(QPainter &painter, const QRectF &bounds);
QIcon meridianIcon();
