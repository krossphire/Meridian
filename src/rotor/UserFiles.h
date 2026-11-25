/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything Meridian writes sits beside the program: meridian.ini, logs/,
 * and backups/. AppData is only the fallback when that folder is read-only.
 */

#pragma once

#include "AppConfig.h"

#include <QString>

class QSettings;

class UserFiles
{
public:
    // Exe folder when it can be written, otherwise the per-user Meridian folder.
    static QString root();
    static bool besideExecutable();

    static QString settingsPath();
    static QString logDirectory();
    static QString backupDirectory();

    // Tests and the screenshot run point this at a temporary folder.
    static void setRoot(const QString &directory);

    static AppConfig load();
    // False when meridian.ini could not be written. Callers show that; this stays quiet.
    static bool save(const AppConfig &config);

    // True when `directory` accepts a new file.
    static bool directoryWritable(const QString &directory);
    static QString chooseRoot(const QString &exeDirectory, const QString &appDataDirectory, bool exeWritable);
};
