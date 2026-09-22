#include "format.h"

namespace util {

QString formatSize(qint64 bytes)
{
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    for (int unit = 0; unit < 5; ++unit) {
        if (value < 1024.0 || unit == 4) {
            if (unit == 0)
                return QStringLiteral("%1 B").arg(static_cast<qint64>(value));
            return QStringLiteral("%1 %2")
                .arg(value, 0, 'f', 1)
                .arg(QLatin1String(units[unit]));
        }
        value /= 1024.0;
    }
    return {};
}

} // namespace util
