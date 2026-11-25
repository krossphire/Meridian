/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eight fixed compass headings, clockwise from north. The highlighted pill
 * is the one nearest the rotor, including the wrap from 359° back to north.
 */

#pragma once

inline constexpr int kCompassCount = 8;

const char *compassLabel(int index);

// 0 = N, 1 = NE, … 7 = NW. Headings that are not finite land on north.
int nearestCompassIndex(double headingDegrees);
