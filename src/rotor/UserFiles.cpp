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
#include <QTemporaryDir>

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
    if (!QFileInfo::exists(settingsPath()))
        return AppConfig::defaults();
    QSettings file(settingsPath(), QSettings::IniFormat);
    return AppConfig::load(file);
}

bool UserFiles::save(const AppConfig &config)
{
    prepare();
    const QString dest = settingsPath();
    QTemporaryDir scratch;
    if (!scratch.isValid())
        return false;
    const QString tmp = scratch.filePath(QStringLiteral("meridian.ini"));
    {
        QSettings out(tmp, QSettings::IniFormat);
        config.save(out);
        out.sync();
        if (out.status() != QSettings::NoError)
            return false;
    }
    QFile written(tmp);
    if (!written.open(QIODevice::ReadOnly))
        return false;
    const QByteArray body = written.readAll();
    written.close();
    if (body.isEmpty())
        return false;
    // Keep the previous ini as meridian.ini.bak. Copy it; do not rename it
    // out of the way. On Windows a rename cannot replace the file that is
    // already there, and that used to drop the new settings.
    if (QFileInfo::exists(dest)) {
        const QString bak = dest + QStringLiteral(".bak");
        QFile::remove(bak);
        QFile::copy(dest, bak);
    }
    QSaveFile headed(dest);
    if (!headed.open(QIODevice::WriteOnly))
        return false;
    const QByteArray header = QByteArrayLiteral(
        "; Meridian settings. Copy this file to back it up or move it to another computer.\n"
        "; No passwords are stored here.\n");
    if (headed.write(header) != header.size() || headed.write(body) != body.size())
        return false;
    return headed.commit();
}
