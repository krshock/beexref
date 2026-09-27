#pragma once

#include <QPointF>
#include <QRectF>
#include <Qt>

namespace ui::selection {

// Handle geometry in device pixels, as the app defines it: the
// interactable areas keep their screen size at any zoom.
inline constexpr double kLineWidth = 2.0;
inline constexpr double kHandleSize = 15.0; // drawn dot
inline constexpr double kResizeSize = 20.0; // scale hover area
inline constexpr double kRotateSize = 10.0; // rotation band around a corner
inline constexpr double kFreeCenter = 20.0; // always moves, even on a handle

enum class Part {
    None,
    Scale,
    Rotate,
    FlipHorizontal,
    FlipVertical,
};

struct Hit
{
    Part part = Part::None;
    int corner = -1; // 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left
};

// Hit test against the selection bounds; all sizes are divided by the
// view scale so they stay constant on screen. A hit in the free centre
// (None) always moves, so small items stay editable.
Hit hitTest(const QRectF &bounds, double viewScale, const QPointF &scenePos);

// Corner of the bounds; index 0 TL, 1 TR, 2 BR, 3 BL.
QPointF corner(const QRectF &bounds, int index);

// The anchor a corner scales around: the opposite corner.
QPointF scaleAnchor(const QRectF &bounds, int index);

// The scale cursor for a corner, following the mapping for
// rotated and flipped items.
Qt::CursorShape scaleCursor(int index, double rotation, bool flipped);

// Relative scale factor for a drag from pressScene to moveScene: the
// mouse travel along the direction from the selection centre to the
// pressed corner, relative to the selection's diagonal.
double scaleFactor(const QRectF &bounds, const QPointF &pressScene, const QPointF &moveScene);

// Angle of a point towards the anchor, in degrees; rotation deltas are
// differences of this.
double rotationAngle(const QPointF &anchorScene, const QPointF &scenePos);

// Rounds an angle to the given step (the app snaps with
// Ctrl or Shift).
double snapAngle(double angle, double step = 15.0);

} // namespace ui::selection
