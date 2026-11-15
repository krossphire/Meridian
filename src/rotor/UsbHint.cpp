/*
 * Meridian — rotator controller for the Green Heron RT-21.
 * Copyright (C) 2026 Andrew, K5PO
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "UsbHint.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVector>

namespace {

constexpr int kRecentSeconds = 15 * 60;
constexpr int kProblemFailedPostStart = 43;

const char *kCp210xDriver = "https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers";
const char *kFtdiDriver = "https://ftdichip.com/drivers/vcp-drivers/";
const char *kGreenHeronDrivers = "https://www.greenheronengineering.com/rotator-controllers-software/";

bool isCp210xPid(quint16 pid)
{
    switch (pid) {
    case 0xEA60: // CP2101/2/3/4/9, the single-port chip Green Heron used
    case 0xEA61:
    case 0xEA63:
    case 0xEA70: // CP2105
    case 0xEA71: // CP2108
        return true;
    default:
        return false;
    }
}

bool isFtdiPid(quint16 pid)
{
    switch (pid) {
    case 0x6001: // FT232
    case 0x6010: // FT2232
    case 0x6011: // FT4232
    case 0x6014: // FT232H
    case 0x6015: // FT-X
        return true;
    default:
        return false;
    }
}

QString chipHintText(UartFamily family)
{
    if (family == UartFamily::Cp210x) {
        return QStringLiteral(
            "Possible RT-21. A Silicon Labs CP210x was found (units shipped from November 2022) "
            "but it has no COM port. Install the Silicon Labs CP210x driver, then replug.");
    }
    return QStringLiteral(
        "Possible RT-21. An FTDI chip was found (older units) but it has no COM port. "
        "Serial numbers 2000 and up use the FTDI driver from Windows Update. "
        "Serial numbers under 2000 need Green Heron's INF. Then replug.");
}

QString chipHintHtml(UartFamily family)
{
    if (family == UartFamily::Cp210x) {
        return QStringLiteral(
                   "Possible RT-21. A Silicon Labs CP210x was found (units shipped from November 2022) "
                   "but it has no COM port. Install the <a href=\"%1\">Silicon Labs CP210x driver</a>, then replug.")
            .arg(QLatin1String(kCp210xDriver));
    }
    return QStringLiteral(
               "Possible RT-21. An FTDI chip was found (older units) but it has no COM port. "
               "Serial numbers 2000 and up use the <a href=\"%1\">FTDI driver</a> from Windows Update. "
               "Serial numbers under 2000 need <a href=\"%2\">Green Heron's INF</a>. Then replug.")
        .arg(QLatin1String(kFtdiDriver), QLatin1String(kGreenHeronDrivers));
}

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include <devpropdef.h>

struct ListedDevice {
    QString instanceId;
    QString hardwareId;
    QString name;
    quint16 vid = 0;
    quint16 pid = 0;
    int problemCode = -1;
    bool portClass = false;
    QDateTime lastArrival;
};

quint16 hexField(const QString &text, const QString &key)
{
    const int at = text.indexOf(key, 0, Qt::CaseInsensitive);
    if (at < 0)
        return 0;
    bool ok = false;
    const quint16 value = text.mid(at + key.size(), 4).toUShort(&ok, 16);
    return ok ? value : 0;
}

QString deviceText(HDEVINFO info, SP_DEVINFO_DATA *data, DWORD property)
{
    DWORD size = 0;
    SetupDiGetDeviceRegistryPropertyW(info, data, property, nullptr, nullptr, 0, &size);
    if (size < sizeof(wchar_t))
        return {};
    QVector<wchar_t> buffer(int(size / sizeof(wchar_t)) + 1);
    if (!SetupDiGetDeviceRegistryPropertyW(info, data, property, nullptr, reinterpret_cast<PBYTE>(buffer.data()), size,
                                           nullptr))
        return {};
    return QString::fromWCharArray(buffer.data());
}

QList<UsbDeviceFact> enumerateWindows()
{
    const DEVPROPKEY arrivalKey = {
        {0x83da6326, 0x97a6, 0x4088, {0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29}}, 102};
    HDEVINFO info = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (info == INVALID_HANDLE_VALUE)
        return {};
    QList<ListedDevice> listed;
    SP_DEVINFO_DATA data;
    data.cbSize = sizeof(data);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(info, index, &data); ++index) {
        ListedDevice row;
        WCHAR instance[512];
        if (SetupDiGetDeviceInstanceIdW(info, &data, instance, 512, nullptr))
            row.instanceId = QString::fromWCharArray(instance);
        row.hardwareId = deviceText(info, &data, SPDRP_HARDWAREID);
        row.name = deviceText(info, &data, SPDRP_FRIENDLYNAME);
        if (row.name.isEmpty())
            row.name = deviceText(info, &data, SPDRP_DEVICEDESC);
        const QString idBlob = row.instanceId + QLatin1Char(' ') + row.hardwareId;
        row.vid = hexField(idBlob, QStringLiteral("VID_"));
        row.pid = hexField(idBlob, QStringLiteral("PID_"));
        const QString classGuid = deviceText(info, &data, SPDRP_CLASSGUID);
        row.portClass = classGuid.compare(QStringLiteral("{4d36e978-e325-11ce-bfc1-08002be10318}"), Qt::CaseInsensitive) == 0
                        || row.name.contains(QRegularExpression(QStringLiteral("\\(COM\\d+\\)")));
        ULONG status = 0;
        ULONG problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0) == CR_SUCCESS && (status & DN_HAS_PROBLEM))
            row.problemCode = int(problem);
        FILETIME arrived{};
        DEVPROPTYPE type = 0;
        ULONG got = sizeof(arrived);
        if (CM_Get_DevNode_PropertyW(data.DevInst, &arrivalKey, &type, reinterpret_cast<PBYTE>(&arrived), &got, 0) == CR_SUCCESS
            && got == sizeof(arrived)) {
            ULARGE_INTEGER ticks;
            ticks.LowPart = arrived.dwLowDateTime;
            ticks.HighPart = arrived.dwHighDateTime;
            const qint64 ms = qint64(ticks.QuadPart / 10000ULL) - 11644473600000LL;
            row.lastArrival = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
        }
        listed.append(row);
    }
    SetupDiDestroyDeviceInfoList(info);

    auto knownUart = [](quint16 vid, quint16 pid) {
        return (vid == 0x10C4 && isCp210xPid(pid)) || (vid == 0x0403 && isFtdiPid(pid));
    };
    QSet<quint32> familiesWithPorts;
    for (const ListedDevice &row : listed) {
        if (row.portClass && knownUart(row.vid, row.pid))
            familiesWithPorts.insert((quint32(row.vid) << 16) | row.pid);
    }
    QList<UsbDeviceFact> facts;
    for (const ListedDevice &row : listed) {
        const bool failedLook = row.problemCode == kProblemFailedPostStart || row.vid == 0
                                || row.name.contains(QStringLiteral("descriptor"), Qt::CaseInsensitive);
        if (!knownUart(row.vid, row.pid) && !failedLook)
            continue;
        UsbDeviceFact fact;
        fact.vid = row.vid;
        fact.pid = row.pid;
        fact.name = row.name;
        fact.instanceId = row.instanceId;
        fact.problemCode = row.problemCode;
        fact.lastArrival = row.lastArrival;
        fact.hasSerialPort = familiesWithPorts.contains((quint32(row.vid) << 16) | row.pid);
        facts.append(fact);
    }
    return facts;
}
#endif

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/serial/IOSerialKeys.h>

QString cfToString(CFTypeRef value)
{
    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return {};
    char buffer[512];
    if (!CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer), kCFStringEncodingUTF8))
        return {};
    return QString::fromUtf8(buffer);
}

quint16 cfToHex(CFTypeRef value)
{
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID())
        return 0;
    int number = 0;
    CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &number);
    return quint16(number);
}

void readUsbService(const char *className, QList<UsbDeviceFact> *out)
{
    CFMutableDictionaryRef match = IOServiceMatching(className);
    if (!match)
        return;
    io_iterator_t iterator = 0;
#if defined(MAC_OS_X_VERSION_12_0) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_12_0
    const mach_port_t mainPort = kIOMainPortDefault;
#else
    const mach_port_t mainPort = kIOMasterPortDefault;
#endif
    if (IOServiceGetMatchingServices(mainPort, match, &iterator) != KERN_SUCCESS)
        return;
    io_service_t device = 0;
    while ((device = IOIteratorNext(iterator))) {
        UsbDeviceFact fact;
        CFTypeRef vid = IORegistryEntryCreateCFProperty(device, CFSTR(kUSBVendorID), kCFAllocatorDefault, 0);
        CFTypeRef pid = IORegistryEntryCreateCFProperty(device, CFSTR(kUSBProductID), kCFAllocatorDefault, 0);
        CFTypeRef product = IORegistryEntryCreateCFProperty(device, CFSTR(kUSBProductString), kCFAllocatorDefault, 0);
        fact.vid = cfToHex(vid);
        fact.pid = cfToHex(pid);
        fact.name = cfToString(product);
        uint64_t entryId = 0;
        IORegistryEntryGetRegistryEntryID(device, &entryId);
        fact.instanceId = QString::number(quint64(entryId));
        io_iterator_t children = 0;
        if (IORegistryEntryGetChildIterator(device, kIOServicePlane, &children) == KERN_SUCCESS) {
            io_service_t child = 0;
            while ((child = IOIteratorNext(children))) {
                CFTypeRef callout = IORegistryEntrySearchCFProperty(child, kIOServicePlane, CFSTR(kIOCalloutDeviceKey),
                                                                    kCFAllocatorDefault, kIORegistryIterateRecursively);
                if (callout) {
                    fact.hasSerialPort = true;
                    CFRelease(callout);
                }
                IOObjectRelease(child);
            }
            IOObjectRelease(children);
        }
        if (vid)
            CFRelease(vid);
        if (pid)
            CFRelease(pid);
        if (product)
            CFRelease(product);
        const bool known = (fact.vid == 0x10C4 && isCp210xPid(fact.pid)) || (fact.vid == 0x0403 && isFtdiPid(fact.pid));
        const bool failed = fact.vid == 0 || fact.name.contains(QStringLiteral("descriptor"), Qt::CaseInsensitive);
        if (known || failed)
            out->append(fact);
        IOObjectRelease(device);
    }
    IOObjectRelease(iterator);
}

QList<UsbDeviceFact> enumerateMac()
{
    QList<UsbDeviceFact> facts;
    readUsbService("IOUSBHostDevice", &facts);
    if (facts.isEmpty())
        readUsbService("IOUSBDevice", &facts);
    return facts;
}
#endif

} // namespace

UartFamily uartFamily(quint16 vid, quint16 pid)
{
    if (vid == 0x10C4 && isCp210xPid(pid))
        return UartFamily::Cp210x;
    if (vid == 0x0403 && isFtdiPid(pid))
        return UartFamily::Ftdi;
    return UartFamily::None;
}

bool isDescriptorFailure(const UsbDeviceFact &device)
{
    const QString name = device.name.toLower();
    if (name.contains(QStringLiteral("device descriptor request failed")))
        return true;
    if (device.vid == 0x0000 && device.pid == 0x0002)
        return true;
    if (device.problemCode == kProblemFailedPostStart
        && (device.vid == 0 || name.contains(QStringLiteral("unknown usb device"))))
        return true;
    return false;
}

bool failedStartIsRecent(const UsbDeviceFact &device, const QDateTime &now, bool baselineReady,
                         const QSet<QString> &seenAtStart)
{
    if (device.lastArrival.isValid()) {
        const qint64 age = device.lastArrival.secsTo(now);
        if (age >= 0 && age <= kRecentSeconds)
            return true;
    }
    return baselineReady && !device.instanceId.isEmpty() && !seenAtStart.contains(device.instanceId);
}

QList<UsbHint> classifyUsbDevices(const QList<UsbDeviceFact> &devices, const QDateTime &now, bool baselineReady,
                                  const QSet<QString> &seenAtStart)
{
    bool cp210x = false;
    bool ftdi = false;
    bool failed = false;
    for (const UsbDeviceFact &device : devices) {
        if (isDescriptorFailure(device)) {
            if (failedStartIsRecent(device, now, baselineReady, seenAtStart))
                failed = true;
            continue;
        }
        const UartFamily family = uartFamily(device.vid, device.pid);
        if (family == UartFamily::None || device.hasSerialPort)
            continue;
        if (family == UartFamily::Cp210x)
            cp210x = true;
        else
            ftdi = true;
    }
    QList<UsbHint> hints;
    if (cp210x) {
        UsbHint hint;
        hint.kind = UsbHint::Kind::MissingDriver;
        hint.family = UartFamily::Cp210x;
        hint.text = chipHintText(UartFamily::Cp210x);
        hint.html = chipHintHtml(UartFamily::Cp210x);
        hints.append(hint);
    }
    if (ftdi) {
        UsbHint hint;
        hint.kind = UsbHint::Kind::MissingDriver;
        hint.family = UartFamily::Ftdi;
        hint.text = chipHintText(UartFamily::Ftdi);
        hint.html = chipHintHtml(UartFamily::Ftdi);
        hints.append(hint);
    }
    if (failed) {
        UsbHint hint;
        hint.kind = UsbHint::Kind::FailedStart;
        hint.text = QStringLiteral(
            "A USB device failed to start. If that's your RT-21, try another data cable, a rear USB port, "
            "or power-cycle the controller.");
        hint.html = hint.text;
        hints.append(hint);
    }
    return hints;
}

QList<UsbDeviceFact> usbFactsFromSysfs(const QString &root)
{
    QList<UsbDeviceFact> facts;
    QDir dir(root);
    if (!dir.exists())
        return facts;
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : entries) {
        if (name.contains(QLatin1Char(':')))
            continue;
        const QString path = dir.filePath(name);
        QFile vendor(path + QStringLiteral("/idVendor"));
        QFile product(path + QStringLiteral("/idProduct"));
        if (!vendor.open(QIODevice::ReadOnly) || !product.open(QIODevice::ReadOnly))
            continue;
        bool vidOk = false;
        bool pidOk = false;
        UsbDeviceFact fact;
        fact.vid = QString::fromLatin1(vendor.readAll().trimmed()).toUShort(&vidOk, 16);
        fact.pid = QString::fromLatin1(product.readAll().trimmed()).toUShort(&pidOk, 16);
        if (!vidOk || !pidOk)
            continue;
        QFile label(path + QStringLiteral("/product"));
        if (label.open(QIODevice::ReadOnly))
            fact.name = QString::fromUtf8(label.readAll().trimmed());
        fact.instanceId = name;
        const QFileInfo info(path);
        fact.lastArrival = info.birthTime().isValid() ? info.birthTime() : info.lastModified();
        if (fact.lastArrival.timeSpec() != Qt::UTC)
            fact.lastArrival = fact.lastArrival.toUTC();
        const QDir node(path);
        const auto children = node.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &child : children) {
            if (child.startsWith(QStringLiteral("tty")) || node.exists(child + QStringLiteral("/tty")))
                fact.hasSerialPort = true;
            const QDir iface(node.filePath(child));
            const auto leaves = iface.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &leaf : leaves) {
                if (leaf.startsWith(QStringLiteral("tty")))
                    fact.hasSerialPort = true;
            }
        }
        if (uartFamily(fact.vid, fact.pid) != UartFamily::None || isDescriptorFailure(fact))
            facts.append(fact);
    }
    return facts;
}

QList<UsbDeviceFact> enumeratePresentUsbDevices()
{
#ifdef Q_OS_WIN
    return enumerateWindows();
#elif defined(Q_OS_MACOS)
    return enumerateMac();
#elif defined(Q_OS_LINUX)
    return usbFactsFromSysfs(QStringLiteral("/sys/bus/usb/devices"));
#else
    return {};
#endif
}
