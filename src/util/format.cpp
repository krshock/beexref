#include "format.h"

#include <QUrl>

namespace util {
namespace {

// 255 bytes is the usual limit for one path component (ext4, XFS, ...).
constexpr int kMaxFilenameBytes = 255;

bool isUnsafeFilenameChar(QChar ch)
{
    const ushort code = ch.unicode();
    if (code < 0x20 || code == 0x7F)
        return true;
    return QStringLiteral("?*:<>|\"\\/").contains(ch);
}

} // namespace

QString filenameFromUrl(const QUrl &url)
{
    const QString segment = url.fileName();
    if (segment.isEmpty())
        return {};
    QString safe;
    safe.reserve(segment.size());
    for (const QChar ch : segment)
        safe += isUnsafeFilenameChar(ch) ? QLatin1Char('_') : ch;
    return truncateUtf8(safe, kMaxFilenameBytes);
}

QString truncateUtf8(const QString &text, int maxBytes)
{
    const QByteArray bytes = text.toUtf8();
    if (bytes.size() <= maxBytes)
        return text;
    // Walk back to the start of the character that crosses the limit:
    // continuation bytes (10xxxxxx) belong to a character that began
    // earlier, so the cut must move before its lead byte.
    int end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(bytes.at(end)) & 0xC0) == 0x80)
        --end;
    return QString::fromUtf8(bytes.left(end));
}

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
