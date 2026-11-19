/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "UserFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString appDataRoot()
{
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("Meridian"));
}

QString &rootStorage()
{
    static QString path;
    return path;
}

bool &readyFlag()
{
    static bool ready = false;
    return ready;
}

void copyNativeSettings(QSettings &native, QSettings &dest)
{
    const QStringList keys = native.allKeys();
    for (const QString &key : keys) {
        if (key.contains(QStringLiteral("password"), Qt::CaseInsensitive)
            || key.contains(QStringLiteral("passphrase"), Qt::CaseInsensitive))
            continue;
        dest.setValue(key, native.value(key));
    }
}

void migrateNative(const QString &dest)
{
    if (QFileInfo::exists(dest))
        return;
    QSettings native(QSettings::NativeFormat, QSettings::UserScope, QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
    if (native.allKeys().isEmpty())
        return;
    const QString tmp = dest + QStringLiteral(".tmp");
    {
        QSettings out(tmp, QSettings::IniFormat);
        copyNativeSettings(native, out);
        out.sync();
    }
    QFile::remove(dest);
    QFile::rename(tmp, dest);
}

void prepare()
{
    if (readyFlag())
        return;
    const QString exe = QCoreApplication::applicationDirPath();
    const bool exeWritable = UserFiles::directoryWritable(exe);
    rootStorage() = UserFiles::chooseRoot(exe, appDataRoot(), exeWritable);
    readyFlag() = true;
    QDir().mkpath(rootStorage());
    QDir().mkpath(UserFiles::logDirectory());
    QDir().mkpath(UserFiles::backupDirectory());
    migrateNative(UserFiles::settingsPath());
}

} // namespace

QString UserFiles::chooseRoot(const QString &exeDirectory, const QString &appDataDirectory, bool exeWritable)
{
    if (exeWritable && !exeDirectory.isEmpty())
        return exeDirectory;
    return appDataDirectory;
}

bool UserFiles::directoryWritable(const QString &directory)
{
    if (directory.isEmpty())
        return false;
    QDir().mkpath(directory);
    const QString probePath = QDir(directory).filePath(QStringLiteral(".meridian-write-test"));
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    const bool wrote = probe.write("ok") == 2;
    probe.close();
    probe.remove();
    return wrote;
}

void UserFiles::setRoot(const QString &directory)
{
    rootStorage() = directory;
    readyFlag() = true;
    QDir().mkpath(directory);
    QDir().mkpath(logDirectory());
    QDir().mkpath(backupDirectory());
}

QString UserFiles::root()
{
    prepare();
    return rootStorage();
}

bool UserFiles::besideExecutable()
{
    return QDir::cleanPath(root()) == QDir::cleanPath(QCoreApplication::applicationDirPath());
}

QString UserFiles::settingsPath()
{
    return QDir(root()).filePath(QStringLiteral("meridian.ini"));
}

QString UserFiles::logDirectory()
{
    return QDir(root()).filePath(QStringLiteral("logs"));
}

QString UserFiles::backupDirectory()
{
    return QDir(root()).filePath(QStringLiteral("backups"));
}

AppConfig UserFiles::load()
{
    prepare();
    if (!QFileInfo::exists(settingsPath())) {
        const AppConfig config = AppConfig::defaults();
        save(config);
        return config;
    }
    QSettings file(settingsPath(), QSettings::IniFormat);
    AppConfig config = AppConfig::load(file);
    if (config.migrateModulePort())
        save(config);
    return config;
}

void UserFiles::save(const AppConfig &config)
{
    prepare();
    const QString dest = settingsPath();
    const QString tmp = dest + QStringLiteral(".tmp");
    QFile::remove(tmp);
    {
        QSettings out(tmp, QSettings::IniFormat);
        config.save(out);
        out.sync();
    }
    QFile written(tmp);
    if (written.open(QIODevice::ReadOnly)) {
        const QByteArray body = written.readAll();
        written.close();
        QSaveFile headed(tmp);
        if (headed.open(QIODevice::WriteOnly)) {
            headed.write("; Meridian settings. Copy this file to back it up or move it to another computer.\n");
            headed.write("; No passwords are stored here.\n");
            headed.write(body);
            headed.commit();
        }
    }
    const QString bak = dest + QStringLiteral(".bak");
    QFile::remove(bak);
    if (QFileInfo::exists(dest))
        QFile::rename(dest, bak);
    if (!QFile::rename(tmp, dest) && QFileInfo::exists(bak))
        QFile::rename(bak, dest);
}
