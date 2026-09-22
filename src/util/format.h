#pragma once

#include <QString>
#include <QtGlobal>

namespace util {

// Human-readable size, matching the reference's format_size: 1024
// base, whole bytes below 1 KB, one decimal above.
QString formatSize(qint64 bytes);

} // namespace util
