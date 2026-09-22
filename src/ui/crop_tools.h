#pragma once

#include <QPointF>
#include <QRectF>
#include <Qt>

namespace ui::crop {

// The crop UI's handle size in device pixels, as the reference defines
// it; all hit areas keep their screen size at any zoom.
inline constexpr double kHandleSize = 15.0;

enum class Part {
    None,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Top,
    Left,
    Bottom,
    Right,
};

// The editable crop rectangle, in item (original image) coordinates.
// Everything here is sized for the item's current on-screen scale, so
// the caller passes viewScale * itemScale.
QRectF handleRect(const QRectF &rect, Part part, double scale);
QRectF edgeRect(const QRectF &rect, Part part, double scale);

// Handles win over edges, like the reference's hit testing order.
Part hitTest(const QRectF &rect, double scale, const QPointF &pos);

// Clamps a dragged point so the rectangle cannot invert and stays
// inside the image, exactly as ensure_point_within_bounds does.
QPointF clampPoint(const QRectF &rect, Part part, const QPointF &point, const QRectF &imageBounds);

// The rectangle after dragging `part` by `delta` (item coordinates).
QRectF draggedRect(const QRectF &rect, Part part, const QPointF &delta, const QRectF &imageBounds);

// Corner handles follow the same diagonal-cursor mapping as the
// selection handles; edges flip between horizontal and vertical when
// the item is rotated sideways.
Qt::CursorShape handleCursor(Part part, double rotation, bool flipped);
Qt::CursorShape edgeCursor(Part part, double rotation);

} // namespace ui::crop
