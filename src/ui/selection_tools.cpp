#include "selection_tools.h"

#include <QLineF>

#include <cmath>
#include <numbers>

namespace ui::selection {
namespace {

QPointF directionFromCenter(const QRectF &bounds, const QPointF &pos)
{
    const QPointF diff = pos - bounds.center();
    const double length = std::hypot(diff.x(), diff.y());
    if (length <= 0.0)
        return {};
    return diff / length;
}

// The corner's facing direction, e.g. (+1,+1) for the bottom right.
QPointF cornerDirection(const QRectF &bounds, int index)
{
    const QPointF point = corner(bounds, index);
    return QPointF(point.x() > bounds.center().x() ? 1.0 : -1.0,
                   point.y() > bounds.center().y() ? 1.0 : -1.0);
}

double diagonal(const QRectF &bounds)
{
    return std::hypot(bounds.width(), bounds.height());
}

} // namespace

QPointF corner(const QRectF &bounds, int index)
{
    switch (index) {
    case 0:
        return bounds.topLeft();
    case 1:
        return bounds.topRight();
    case 2:
        return bounds.bottomRight();
    default:
        return bounds.bottomLeft();
    }
}

QPointF scaleAnchor(const QRectF &bounds, int index)
{
    // The opposite corner.
    const QPointF center = bounds.center();
    return 2.0 * center - corner(bounds, index);
}

Hit hitTest(const QRectF &bounds, double viewScale, const QPointF &scenePos)
{
    Hit hit;
    if (bounds.isEmpty() || viewScale <= 0)
        return hit;

    const double scale = viewScale;
    const double resize = kResizeSize / scale;
    const double rotate = kRotateSize / scale;
    const double freeCenter = kFreeCenter / scale;

    // The free centre always moves, even when a handle overlaps it.
    const QRectF freeRect(bounds.center().x() - freeCenter / 2.0,
                          bounds.center().y() - freeCenter / 2.0, freeCenter, freeCenter);
    if (freeRect.contains(scenePos))
        return hit;

    for (int index = 0; index < 4; ++index) {
        const QPointF point = corner(bounds, index);
        const QRectF scaleRect(point.x() - resize / 2.0, point.y() - resize / 2.0, resize,
                               resize);
        if (scaleRect.contains(scenePos)) {
            hit.part = Part::Scale;
            hit.corner = index;
            return hit;
        }

        // Rotation sits around the scale area like an L shape.
        const QPointF direction = cornerDirection(bounds, index);
        const QPointF first = point - direction * (resize / 2.0);
        const QPointF second = first + direction * (resize + rotate);
        const QRectF rotateRect(QPointF(qMin(first.x(), second.x()), qMin(first.y(), second.y())),
                                QPointF(qMax(first.x(), second.x()), qMax(first.y(), second.y())));
        if (rotateRect.contains(scenePos)) {
            hit.part = Part::Rotate;
            hit.corner = index;
            return hit;
        }
    }

    // Flip bands stretch along the four edges between the handles.
    const double outer = resize / 2.0;
    const double inner = resize / 2.0;
    const QRectF top(bounds.left() + inner, bounds.top() - outer,
                     bounds.width() - 2 * inner, outer + inner);
    const QRectF bottom(bounds.left() + inner, bounds.bottom() - inner,
                        bounds.width() - 2 * inner, outer + inner);
    const QRectF left(bounds.left() - outer, bounds.top() + inner, outer + inner,
                      bounds.height() - 2 * inner);
    const QRectF right(bounds.right() - inner, bounds.top() + inner, outer + inner,
                       bounds.height() - 2 * inner);
    if (top.contains(scenePos) || bottom.contains(scenePos)) {
        hit.part = Part::FlipVertical;
        return hit;
    }
    if (left.contains(scenePos) || right.contains(scenePos)) {
        hit.part = Part::FlipHorizontal;
        return hit;
    }
    return hit;
}

Qt::CursorShape scaleCursor(int index, double rotation, bool flipped)
{
    const bool topLeftOrBottomRight = index == 0 || index == 2;
    const double angle = std::fmod(std::abs(rotation), 180.0);
    if (topLeftOrBottomRight) {
        if (angle > 22.5 && angle < 67.5)
            return Qt::SizeVerCursor;
        if (angle > 67.5 && angle < 112.5)
            return flipped ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
        if (angle > 112.5 && angle < 157.5)
            return Qt::SizeHorCursor;
        return flipped ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
    }
    if (angle > 22.5 && angle < 67.5)
        return Qt::SizeHorCursor;
    if (angle > 67.5 && angle < 112.5)
        return flipped ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
    if (angle > 112.5 && angle < 157.5)
        return Qt::SizeVerCursor;
    return flipped ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
}

double scaleFactor(const QRectF &bounds, const QPointF &pressScene, const QPointF &moveScene)
{
    const double reference = diagonal(bounds);
    if (reference <= 0.0)
        return 1.0;
    const QPointF direction = directionFromCenter(bounds, pressScene);
    const QPointF travel = moveScene - pressScene;
    const double along = direction.x() * travel.x() + direction.y() * travel.y();
    return 1.0 + along / reference;
}

double rotationAngle(const QPointF &anchorScene, const QPointF &scenePos)
{
    const QPointF diff = scenePos - anchorScene;
    // std::numbers::pi, not M_PI: the latter is a POSIX extension that
    // MinGW and MSVC do not define without _USE_MATH_DEFINES.
    return -std::atan2(diff.x(), diff.y()) * 180.0 / std::numbers::pi;
}

double snapAngle(double angle, double step)
{
    return std::round(angle / step) * step;
}

} // namespace ui::selection
