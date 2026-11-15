/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Day and night as a disc image. Each pixel is inverse-projected from the
 * azimuthal map, then shaded by solar elevation. The terminator is whatever
 * curve that produces, including near the poles and the antipode.
 */

#pragma once

#include "core/Solar.h"

#include <QImage>

// Square image of the unit disc. Pixels outside the disc are transparent.
// North is up. `pixels` is the width and height, typically 256–512.
QImage renderDayNightDisc(int pixels, double homeLat, double homeLon, const SolarPosition &sun, double utcMinutes);
