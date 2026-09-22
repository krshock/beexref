#pragma once

#include <QtGlobal>

namespace util {

// Whether a process with this id exists. Used by the stale-file sweeps
// (session cache, board temp copies) to tell a crashed instance's files
// from a live one's. Always true on Windows, where the age limit
// handles staleness instead.
bool isProcessAlive(qint64 pid);

} // namespace util
