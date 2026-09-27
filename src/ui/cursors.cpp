#include "cursors.h"

#include <QImage>
#include <QPixmap>

namespace ui::cursors {
namespace {

// Hotspot in device pixels, as the app uses for these assets.
constexpr int kHotspot = 20;

QCursor cursorFrom(const QString &resource, const QCursor &fallback)
{
    QPixmap pixmap(resource);
    if (pixmap.isNull())
        return fallback;
    return QCursor(pixmap, kHotspot, kHotspot);
}

} // namespace

QCursor rotate()
{
    return cursorFrom(QStringLiteral(":/assets/cursors/cursor_rotate.png"), Qt::CrossCursor);
}

QCursor flipHorizontal()
{
    return cursorFrom(QStringLiteral(":/assets/cursors/cursor_flip_h.png"), Qt::SizeHorCursor);
}

QCursor flipVertical()
{
    return cursorFrom(QStringLiteral(":/assets/cursors/cursor_flip_v.png"), Qt::SizeVerCursor);
}

} // namespace ui::cursors
