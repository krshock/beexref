#pragma once

#include <QtGlobal>

namespace util {

// Whether a process with this id exists. Used by the stale-file sweeps
// (session cache, board temp copies) to tell a crashed instance's files
// from a live one's. An id this process cannot query -- another user's
// process -- counts as alive, so nothing live is ever swept.
bool isProcessAlive(qint64 pid);

} // namespace util
