#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QUrl>

class QMimeData;

namespace ui {

// Drop classification, ported from the reference's drop package: an
// ordered list of extractors (standard formats, Chromium custom
// payload, WebKitGTK custom payload). Extractors read mime data only.
enum class DropKind {
    None,
    Urls,
    Image,
};

struct DropResult
{
    DropKind kind = DropKind::None;
    QList<QUrl> urls;
    QImage image;
    bool accepted = false;
    QString message;
};

inline constexpr char kNoDropMessage[] = "Attempted drop not an image";
inline constexpr char kWebKitEmptyMessage[] =
    "Drop contains no image data - try the image on its own page or another browser";

// Parses a raw text/uri-list payload, skipping comments and blank
// lines. The reference falls back to this because some sources
// (WebKitGTK on X11) deliver the data while Qt's parser comes up empty.
QList<QUrl> parseUriList(const QByteArray &data);

// Whether a drag-enter should be accepted; never reads payloads (an
// early read can poison the drop-time read on some sources).
bool dropAccepts(const QMimeData &data);

// Classifies a drop, returning the first extractor whose kind is not
// none, or the most specific "none" result.
DropResult inspectDrop(const QMimeData &data);

} // namespace ui
