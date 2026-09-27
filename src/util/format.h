#pragma once

#include <QString>
#include <QtGlobal>

class QUrl;

namespace util {

// Human-readable size (1024-based):
// base, whole bytes below 1 KB, one decimal above.
QString formatSize(qint64 bytes);

// The decoded last path segment of an http(s) URL (QUrl::fileName(),
// so query and fragment are already gone), with characters that are
// unsafe in file names replaced by '_' and the result capped to one
// path component (255 UTF-8 bytes, never split mid-character). Returns
// an empty string when the URL has no usable segment; callers fall back
// to an id-based name there. Used both when importing downloads and
// when exporting images, so an item keeps the same name throughout.
QString filenameFromUrl(const QUrl &url);

// Truncates text to at most maxBytes of UTF-8, never splitting a
// multi-byte character.
QString truncateUtf8(const QString &text, int maxBytes);

} // namespace util
