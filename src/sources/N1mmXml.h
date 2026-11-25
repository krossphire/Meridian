/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * N1MM Logger+ UDP. LookupInfo and ContactInfo share one field list.
 * https://n1mmwp.hamdocs.com/appendices/external-udp-broadcasts/
 */

#pragma once

#include <QString>

struct N1mmPacket {
    enum class Kind { None, Lookup, Contact };
    Kind kind = Kind::None;
    QString call;
    QString grid;
};

N1mmPacket parseN1mmPacket(const QByteArray &xml);
