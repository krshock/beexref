#include "memory.h"

#include <QFile>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#elif defined(Q_OS_MACOS)
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

} // namespace util
