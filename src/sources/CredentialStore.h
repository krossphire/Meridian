/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * QRZ password. Windows Credential Manager, macOS Keychain, or libsecret.
 * Never meridian.ini.
 */

#pragma once

#include <QString>

class CredentialStore
{
public:
    static bool store(const QString &username, const QString &password);
    static QString load(const QString &username);
    static bool forget(const QString &username);

    // Tests keep the secret in process memory and do not touch the OS store.
    static void useMemoryForTests(bool on);
};
