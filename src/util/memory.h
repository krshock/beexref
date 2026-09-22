#pragma once

#include <QtGlobal>

namespace util {

// Current process resident memory in bytes, or 0 when unavailable.
// There is no portable API for this; each platform has its own call:
//   Linux    /proc/self/statm (RSS pages)
//   macOS    task_info(TASK_VM_INFO).phys_footprint, the value
//            Activity Monitor shows as "Memory"
//   Windows  GetProcessMemoryInfo().WorkingSetSize
// Linux RSS counts shared library pages in full, so the value is not
// directly comparable across platforms, which is fine for diagnostics.
qint64 processRssBytes();

} // namespace util
