/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#if defined(MERIDIAN_HAVE_LIBSECRET)
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")
#endif

#include "CredentialStore.h"

#include <QHash>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#endif

namespace {

bool memoryOnly = false;
QHash<QString, QString> memory;

const QString kService = QStringLiteral("Meridian QRZ");

#if defined(MERIDIAN_HAVE_LIBSECRET)
const SecretSchema *qrzSchema()
{
    static const SecretSchema schema = {
        "net.k5po.Meridian",
        SECRET_SCHEMA_NONE,
        {{"user", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
    return &schema;
}
#endif

} // namespace

void CredentialStore::useMemoryForTests(bool on)
{
    memoryOnly = on;
    if (!on)
        memory.clear();
}

bool CredentialStore::store(const QString &username, const QString &password)
{
    const QString user = username.trimmed();
    if (user.isEmpty() || password.isEmpty())
        return false;
    if (memoryOnly) {
        memory.insert(user, password);
        return true;
    }
#if defined(Q_OS_WIN)
    const std::wstring target = (kService + QLatin1Char('/') + user).toStdWString();
    const QByteArray blob = password.toUtf8();
    const std::wstring account = user.toStdWString();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = DWORD(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(account.c_str());
    return CredWriteW(&cred, 0) == TRUE;
#elif defined(Q_OS_MACOS)
    const QByteArray service = kService.toUtf8();
    const QByteArray account = user.toUtf8();
    const QByteArray secret = password.toUtf8();
    UInt32 length = 0;
    void *data = nullptr;
    SecKeychainItemRef item = nullptr;
    const OSStatus found = SecKeychainFindGenericPassword(nullptr, UInt32(service.size()), service.constData(),
                                                          UInt32(account.size()), account.constData(), &length, &data,
                                                          &item);
    if (found == errSecSuccess && item) {
        const OSStatus updated = SecKeychainItemModifyAttributesAndData(item, nullptr, UInt32(secret.size()),
                                                                        secret.constData());
        SecKeychainItemFreeContent(nullptr, data);
        CFRelease(item);
        return updated == errSecSuccess;
    }
    const OSStatus added = SecKeychainAddGenericPassword(nullptr, UInt32(service.size()), service.constData(),
                                                         UInt32(account.size()), account.constData(),
                                                         UInt32(secret.size()), secret.constData(), nullptr);
    return added == errSecSuccess;
#elif defined(MERIDIAN_HAVE_LIBSECRET)
    GError *error = nullptr;
    const QByteArray secret = password.toUtf8();
    const QByteArray account = user.toUtf8();
    const gboolean ok = secret_password_store_sync(qrzSchema(), SECRET_COLLECTION_DEFAULT, "Meridian QRZ",
                                                   secret.constData(), nullptr, &error, "user", account.constData(),
                                                   nullptr);
    if (error)
        g_error_free(error);
    return ok == TRUE;
#else
    Q_UNUSED(password);
    return false;
#endif
}

QString CredentialStore::load(const QString &username)
{
    const QString user = username.trimmed();
    if (user.isEmpty())
        return {};
    if (memoryOnly)
        return memory.value(user);
#if defined(Q_OS_WIN)
    const std::wstring target = (kService + QLatin1Char('/') + user).toStdWString();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred || !cred->CredentialBlob)
        return {};
    const QString secret = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob),
                                             int(cred->CredentialBlobSize));
    CredFree(cred);
    return secret;
#elif defined(Q_OS_MACOS)
    const QByteArray service = kService.toUtf8();
    const QByteArray account = user.toUtf8();
    UInt32 length = 0;
    void *data = nullptr;
    const OSStatus found = SecKeychainFindGenericPassword(nullptr, UInt32(service.size()), service.constData(),
                                                          UInt32(account.size()), account.constData(), &length, &data,
                                                          nullptr);
    if (found != errSecSuccess || !data)
        return {};
    const QString secret = QString::fromUtf8(static_cast<const char *>(data), int(length));
    SecKeychainItemFreeContent(nullptr, data);
    return secret;
#elif defined(MERIDIAN_HAVE_LIBSECRET)
    GError *error = nullptr;
    const QByteArray account = user.toUtf8();
    gchar *secret = secret_password_lookup_sync(qrzSchema(), nullptr, &error, "user", account.constData(), nullptr);
    if (error)
        g_error_free(error);
    if (!secret)
        return {};
    const QString value = QString::fromUtf8(secret);
    secret_password_free(secret);
    return value;
#else
    return {};
#endif
}

bool CredentialStore::forget(const QString &username)
{
    const QString user = username.trimmed();
    if (user.isEmpty())
        return false;
    if (memoryOnly) {
        memory.remove(user);
        return true;
    }
#if defined(Q_OS_WIN)
    const std::wstring target = (kService + QLatin1Char('/') + user).toStdWString();
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE;
#elif defined(Q_OS_MACOS)
    const QByteArray service = kService.toUtf8();
    const QByteArray account = user.toUtf8();
    SecKeychainItemRef item = nullptr;
    const OSStatus found = SecKeychainFindGenericPassword(nullptr, UInt32(service.size()), service.constData(),
                                                          UInt32(account.size()), account.constData(), nullptr, nullptr,
                                                          &item);
    if (found != errSecSuccess || !item)
        return false;
    const OSStatus deleted = SecKeychainItemDelete(item);
    CFRelease(item);
    return deleted == errSecSuccess;
#elif defined(MERIDIAN_HAVE_LIBSECRET)
    GError *error = nullptr;
    const QByteArray account = user.toUtf8();
    const gboolean ok = secret_password_clear_sync(qrzSchema(), nullptr, &error, "user", account.constData(), nullptr);
    if (error)
        g_error_free(error);
    return ok == TRUE;
#else
    return false;
#endif
}
