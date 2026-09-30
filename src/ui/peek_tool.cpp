#include "peek_tool.h"

#include "view.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>

#include <cmath>

namespace ui {
namespace {

// The peek's feel. The pointer's travel from where the peek started
// drives the whole gesture: at the reference radius the view is at
// kMinZoomFactor of the zoom the peek started from, and the pan leans
// kPanGain screen pixels per pointer pixel at the base zoom (so it lags
// the pointer once zoomed out). The travel is normalized per viewport
// axis, so reaching the window edge means the same full peek whatever
// the window's size or aspect ratio; kPeekRadius is that edge (1.0 is
// the ellipse through the mid-edges). The zoom is interpolated
// geometrically (m^t): a constant proportional rate, which is what
// reads as linear -- a plain lerp on the factor accelerates toward the
// end.
constexpr double kMinZoomFactor = 0.2;
constexpr double kPanGain = 0.87;
constexpr double kPeekRadius = 1.0;
// The preview rectangle only appears once the peek has zoomed out this
// much; closer to the start it would just hug the viewport border.
constexpr double kPreviewMinZoomOut = 0.98;

} // namespace

PeekTool::PeekTool(View *view)
    : view_(view)
{
}

void PeekTool::start(const QPoint &viewportPos)
{
    if (!view_)
        return;
    active_ = true;
    origin_ = viewportPos;
    lastPosition_ = viewportPos;
    shiftHeld_ = true;
    baseTransform_ = view_->transform();
    baseScale_ = baseTransform_.m11();
    baseCenter_ = view_->mapToScene(view_->viewport()->rect().center());
    baseHorizontal_ = view_->horizontalScrollBar()->value();
    baseVertical_ = view_->verticalScrollBar()->value();
    appliedScale_ = 0.0;
    appliedCenter_ = {};
    view_->setPeekPreview({});
    view_->viewport()->setCursor(Qt::ClosedHandCursor);
}

void PeekTool::cancel()
{
    if (!active_)
        return;
    active_ = false;
    view_->setPeekPreview({});
    view_->restorePeekView(baseTransform_, baseHorizontal_, baseVertical_);
    view_->viewport()->unsetCursor();
}

bool PeekTool::mouseMove(QMouseEvent *event)
{
    if (!active_)
        return false;
    const QPoint position = event->position().toPoint();
    const QPointF raw = position - origin_;
    lastPosition_ = position;
    shiftHeld_ = event->modifiers().testFlag(Qt::ShiftModifier);
    const QSize size = view_->viewport()->size();
    if (size.isEmpty() || baseScale_ <= 0.0)
        return true;
    const double halfWidth = size.width() / 2.0;
    const double halfHeight = size.height() / 2.0;

    // Normalized per viewport axis: +-kPeekRadius is the window edge
    // along an axis, and the distance saturates there, so the feel does
    // not depend on the window size or its aspect ratio.
    const QPointF normalized(raw.x() / halfWidth, raw.y() / halfHeight);
    const double distance = std::hypot(normalized.x(), normalized.y());
    const double t = qMin(distance / kPeekRadius, 1.0);
    const QPointF clamped =
        distance > kPeekRadius ? normalized * (kPeekRadius / distance) : normalized;
    const QPointF travel(clamped.x() * halfWidth, clamped.y() * halfHeight);

    // A geometric interpolation of the zoom factor, proportional to the
    // zoom the peek started from, and a pan in the pointer's direction
    // measured in screen pixels at that same zoom, saturating with the
    // travel.
    const double scale = baseScale_ * std::pow(kMinZoomFactor, t);
    const QPointF center = baseCenter_ + travel / baseScale_ * kPanGain;

    // Apply only when the view actually changes: a saturated peek would
    // otherwise repaint the whole viewport on every pointer move.
    if (!(appliedScale_ > 0.0 && qFuzzyCompare(scale, appliedScale_)
          && center == appliedCenter_)) {
        view_->applyPeekView(scale, center);
        appliedScale_ = scale;
        appliedCenter_ = center;
    }

    updatePreview();
    return true;
}

void PeekTool::updatePreview()
{
    // The rectangle always has the size of the base-zoom viewport, but
    // where it sits depends on what the release would do: with Shift held
    // the commit lands on the pointer, so it previews that view; with
    // Shift let go the peek cancels, so it marks the view it returns to.
    // It only appears once the peek has zoomed out enough to be worth
    // reading, and the view repaints just the strip it occupies.
    const double factor = appliedScale_ > 0.0 ? appliedScale_ / baseScale_ : 1.0;
    if (factor > kPreviewMinZoomOut) {
        view_->setPeekPreview({});
        return;
    }
    const QSize viewport = view_->viewport()->size();
    const double width = viewport.width() / baseScale_;
    const double height = viewport.height() / baseScale_;
    const QPointF center = shiftHeld_ ? view_->mapToScene(lastPosition_) : baseCenter_;
    view_->setPeekPreview(
        QRectF(center.x() - width / 2.0, center.y() - height / 2.0, width, height));
}

bool PeekTool::keyPress(QKeyEvent *event)
{
    if (!active_ || event->key() != Qt::Key_Shift)
        return false;
    shiftHeld_ = true;
    updatePreview();
    return true;
}

bool PeekTool::keyRelease(QKeyEvent *event)
{
    if (!active_ || event->key() != Qt::Key_Shift)
        return false;
    shiftHeld_ = false;
    updatePreview();
    return true;
}

bool PeekTool::mouseRelease(QMouseEvent *event)
{
    if (!active_)
        return false;
    // Any release ends the peek, whatever button it is. With Shift still
    // held the look is kept: the original zoom comes back, centered on
    // the canvas point under the pointer. With Shift already let go it
    // is a plain cancel: the canvas goes back to where the peek started.
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const QPointF scenePoint = view_->mapToScene(event->position().toPoint());
        active_ = false;
        view_->setPeekPreview({});
        view_->viewport()->unsetCursor();
        view_->commitPeekView(baseTransform_, scenePoint);
        return true;
    }
    cancel();
    return true;
}

} // namespace ui
