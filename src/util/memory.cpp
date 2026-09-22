#include "memory.h"

#include <QFile>

#if defined(Q_OS_LINUX)
#include <malloc.h>
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <malloc/malloc.h>
#include <mach/mach.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

namespace util {

qint64 processRssBytes()
{
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/self/statm"));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QList<QByteArray> fields = file.readAll().simplified().split(' ');
    if (fields.size() < 2)
        return 0;
    bool ok = false;
    const qint64 pages = fields.at(1).toLongLong(&ok);
    if (!ok)
        return 0;
    return pages * static_cast<qint64>(sysconf(_SC_PAGESIZE));
#elif defined(Q_OS_MACOS)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count)
        != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<qint64>(info.phys_footprint);
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return 0;
    return static_cast<qint64>(counters.WorkingSetSize);
#else
    return 0;
#endif
}

bool releaseFreeMemory()
{
#if defined(Q_OS_LINUX) && defined(__GLIBC__)
    return malloc_trim(0) != 0;
#elif defined(Q_OS_MACOS)
    malloc_zone_pressure_relief(nullptr, 0);
    return true;
#else
    return false;
#endif
}

void configureAllocator()
{
#if defined(Q_OS_LINUX) && defined(__GLIBC__)
    // Buffers of 1 MB and up are mmap'd, so freeing them returns the
    // pages immediately instead of parking them in an arena.
    mallopt(M_MMAP_THRESHOLD, 1024 * 1024);
    // Trim the heap top once 128 KB are free.
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);
    // Two arenas (main + decode worker) bound the virtual growth.
    mallopt(M_ARENA_MAX, 2);
#endif
}

} // namespace util
