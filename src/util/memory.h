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

// Returns freed heap memory to the OS. Large transient decode buffers
// leave the heap at a high-water mark even after they are freed; on
// glibc this calls malloc_trim(0). No-op elsewhere. Returns whether
// anything was released.
bool releaseFreeMemory();

// Tunes the allocator for image workloads, before any threads exist.
//
// Multi-megabyte decode buffers would otherwise land in glibc's
// per-thread arenas, where they fragment and stay resident after being
// freed: the process grew by ~50 MB over a zoom/pan session. With a low
// mmap threshold those buffers are mmap'd and returned on free, the low
// trim threshold returns the heap top, and the arena cap bounds virtual
// growth. No-op on other platforms.
void configureAllocator();

} // namespace util
