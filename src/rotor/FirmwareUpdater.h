/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 1 firmware update. The bootloader transport is a stub: flash()
 * refuses and writes nothing. The wire protocol waits on a capture.
 */

#pragma once

#include "rotor/IntelHex.h"

#include <QString>

namespace Rt21 {

class FirmwareUpdater
{
public:
    virtual ~FirmwareUpdater() = default;

    // False means the image was not sent. bytesSent() stays 0 for the stub.
    virtual bool flash(const FirmwareImage &image, QString *status) = 0;
    virtual int bytesSent() const = 0;
};

class RefusingFirmwareUpdater : public FirmwareUpdater
{
public:
    bool flash(const FirmwareImage &image, QString *status) override;
    int bytesSent() const override { return 0; }
};

} // namespace Rt21
