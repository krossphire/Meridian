/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "FirmwareUpdater.h"

namespace Rt21 {

bool RefusingFirmwareUpdater::flash(const FirmwareImage &image, QString *status)
{
    Q_UNUSED(image);
    if (status)
        *status = QStringLiteral("Bootloader protocol not yet captured");
    return false;
}

} // namespace Rt21
