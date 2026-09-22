#include "crop_tools.h"

#include "selection_tools.h"

#include <algorithm>

namespace ui::crop {
namespace {

double handleSize(double scale)
{
    if (scale <= 0.0)
        return kHandleSize;
    return kHandleSize / scale;
}

} // namespace

QRectF handleRect(const QRectF &rect, Part part, double scale)
{
    const double size = handleSize(scale);
    switch (part) {
    case Part::TopLeft:
        return QRectF(rect.left(), rect.top(), size, size);
    case Part::TopRight:
        return QRectF(rect.right() - size, rect.top(), size, size);
    case Part::BottomLeft:
        return QRectF(rect.left(), rect.bottom() - size, size, size);
    case Part::BottomRight:
        return QRectF(rect.right() - size, rect.bottom() - size, size, size);
    default:
        return {};
    }
}

QRectF edgeRect(const QRectF &rect, Part part, double scale)
{
    const double size = handleSize(scale);
    switch (part) {
    case Part::Top:
        return QRectF(rect.left() + size, rect.top(), rect.width() - 2 * size, size);
    case Part::Bottom:
        return QRectF(rect.left() + size, rect.bottom() - size, rect.width() - 2 * size,
                      size);
    case Part::Left:
        return QRectF(rect.left(), rect.top() + size, size, rect.height() - 2 * size);
    case Part::Right:
        return QRectF(rect.right() - size, rect.top() + size, size,
                      rect.height() - 2 * size);
    default:
        return {};
    }
}

Part hitTest(const QRectF &rect, double scale, const QPointF &pos)
{
    static const Part corners[] = {Part::TopLeft, Part::BottomLeft, Part::BottomRight,
                                   Part::TopRight};
    for (Part part : corners) {
        const QRectF hit = handleRect(rect, part, scale);
        if (hit.isValid() && hit.contains(pos))
            return part;
    }
    static const Part edges[] = {Part::Top, Part::Left, Part::Bottom, Part::Right};
    for (Part part : edges) {
        const QRectF hit = edgeRect(rect, part, scale);
        if (hit.isValid() && hit.contains(pos))
            return part;
    }
    return Part::None;
}

QPointF clampPoint(const QRectF &rect, Part part, const QPointF &point,
                   const QRectF &imageBounds)
{
    // The dragged point may not cross the side opposite to it, so the
    // rectangle can collapse but never invert; and never leave the
    // image.
    QRectF limits(imageBounds);
    switch (part) {
    case Part::TopLeft:
        limits.setBottomRight(rect.bottomRight());
        break;
    case Part::TopRight:
        limits.setBottom(rect.bottom());
        limits.setLeft(rect.left());
        break;
    case Part::BottomLeft:
        limits.setTop(rect.top());
        limits.setRight(rect.right());
        break;
    case Part::BottomRight:
        limits.setTopLeft(rect.topLeft());
        break;
    case Part::Top:
        limits.setBottom(rect.bottom());
        break;
    case Part::Bottom:
        limits.setTop(rect.top());
        break;
    case Part::Left:
        limits.setRight(rect.right());
        break;
    case Part::Right:
        limits.setLeft(rect.left());
        break;
    case Part::None:
        break;
    }
    return QPointF(std::clamp(point.x(), limits.left(), limits.right()),
                   std::clamp(point.y(), limits.top(), limits.bottom()));
}

QRectF draggedRect(const QRectF &rect, Part part, const QPointF &delta,
                   const QRectF &imageBounds)
{
    QRectF result(rect);
    switch (part) {
    case Part::TopLeft: {
        const QPointF point = clampPoint(rect, part, rect.topLeft() + delta, imageBounds);
        result.setTopLeft(point);
        break;
    }
    case Part::TopRight: {
        const QPointF point = clampPoint(rect, part, rect.topRight() + delta, imageBounds);
        result.setTopRight(point);
        break;
    }
    case Part::BottomLeft: {
        const QPointF point =
            clampPoint(rect, part, rect.bottomLeft() + delta, imageBounds);
        result.setBottomLeft(point);
        break;
    }
    case Part::BottomRight: {
        const QPointF point =
            clampPoint(rect, part, rect.bottomRight() + delta, imageBounds);
        result.setBottomRight(point);
        break;
    }
    case Part::Top:
        result.setTop(clampPoint(rect, part, rect.topLeft() + delta, imageBounds).y());
        break;
    case Part::Bottom:
        result.setBottom(
            clampPoint(rect, part, rect.bottomLeft() + delta, imageBounds).y());
        break;
    case Part::Left:
        result.setLeft(clampPoint(rect, part, rect.topLeft() + delta, imageBounds).x());
        break;
    case Part::Right:
        result.setRight(clampPoint(rect, part, rect.topRight() + delta, imageBounds).x());
        break;
    case Part::None:
        break;
    }
    return result;
}

Qt::CursorShape handleCursor(Part part, double rotation, bool flipped)
{
    int corner = -1;
    switch (part) {
    case Part::TopLeft:
        corner = 0;
        break;
    case Part::TopRight:
        corner = 1;
        break;
    case Part::BottomRight:
        corner = 2;
        break;
    case Part::BottomLeft:
        corner = 3;
        break;
    default:
        return Qt::ArrowCursor;
    }
    return selection::scaleCursor(corner, rotation, flipped);
}

Qt::CursorShape edgeCursor(Part part, double rotation)
{
    const bool topOrBottom = part == Part::Top || part == Part::Bottom;
    double angle = std::fmod(std::abs(rotation), 360.0);
    if (angle < 0)
        angle += 360.0;
    const bool sideways = (angle > 45 && angle < 135) || (angle > 225 && angle < 315);
    if (topOrBottom == sideways)
        return Qt::SizeHorCursor;
    return Qt::SizeVerCursor;
}

} // namespace ui::crop
