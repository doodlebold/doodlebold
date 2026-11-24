#include "platform/SystemUsage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

namespace doodlebold::platform::usage {

#ifdef Q_OS_LINUX
namespace {
std::uint64_t statusField(const QByteArray& status, const char* name) {
    const qsizetype at = status.indexOf(QByteArray(name) + ':');
    if (at < 0)
        return 0;
    const qsizetype end = status.indexOf('\n', at);
    const QByteArray line = status.mid(at, end < 0 ? -1 : end - at);
    const QList<QByteArray> parts = line.simplified().split(' ');
    return parts.size() >= 2 ? parts[1].toULongLong() * 1024u : 0;
}
}
#endif

std::optional<Memory> memory() {
    Memory m;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return std::nullopt;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return std::nullopt;
    m.privateBytes = pmc.PrivateUsage;
    m.residentBytes = pmc.WorkingSetSize;
    m.totalBytes = ms.ullTotalPhys;
#elif defined(Q_OS_MACOS)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return std::nullopt;
    std::uint64_t total = 0;
    size_t size = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &size, nullptr, 0) != 0)
        return std::nullopt;
    m.privateBytes = info.phys_footprint;
    m.residentBytes = info.resident_size;
    m.totalBytes = total;
#else
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray status = f.readAll();
    m.privateBytes = statusField(status, "RssAnon");
    m.residentBytes = statusField(status, "VmRSS");
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page <= 0)
        return std::nullopt;
    m.totalBytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page);
#endif
    if (m.totalBytes == 0)
        return std::nullopt;
    return m;
}

std::optional<double> driveFullness(const QString& path) {
    // The folder may not exist yet (a first start): the drive is the one of its nearest existing parent.
    QString existing = QDir(path).absolutePath();
    while (!QFileInfo::exists(existing)) {
        const QString parent = QFileInfo(existing).absolutePath();
        if (parent == existing)
            break;
        existing = parent;
    }
    const QStorageInfo drive(existing);
    if (!drive.isValid() || drive.bytesTotal() <= 0)
        return std::nullopt;
    return 1.0 - static_cast<double>(drive.bytesAvailable()) / static_cast<double>(drive.bytesTotal());
}

int percent(std::uint64_t part, std::uint64_t whole) {
    if (whole == 0 || part == 0)
        return 0;
    return std::max(1, static_cast<int>(std::lround(100.0 * static_cast<double>(part) / static_cast<double>(whole))));
}

}
