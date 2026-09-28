#include "peek_tool.h"

#include "view.h"

#include <QMouseEvent>
#include <QScrollBar>

#include <cmath>

namespace ui {
namespace {

// The peek's feel. The pointer's travel from where the peek started
// drives the whole gesture: at the reference radius (half the viewport
// diagonal) the view is at kMinZoomFactor of the zoom the peek started
// from, and the pan leans one screen pixel per pointer pixel at the
// base zoom (so it lags the pointer once zoomed out). The zoom is a
// plain lerp on the factor; a geometric one would be the alternative if
// this feels front-loaded.
constexpr double kMinZoomFactor = 0.4;
constexpr double kPanGain = 1.0;

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
    baseTransform_ = view_->transform();
    baseScale_ = baseTransform_.m11();
    baseCenter_ = view_->mapToScene(view_->viewport()->rect().center());
    baseHorizontal_ = view_->horizontalScrollBar()->value();
    baseVertical_ = view_->verticalScrollBar()->value();
    view_->viewport()->setCursor(Qt::ClosedHandCursor);
}

void PeekTool::cancel()
{
    if (!active_)
        return;
    active_ = false;
    view_->restorePeekView(baseTransform_, baseHorizontal_, baseVertical_);
    view_->viewport()->unsetCursor();
}

bool PeekTool::mouseMove(QMouseEvent *event)
{
    if (!active_)
        return false;
    const QPointF raw = event->position().toPoint() - origin_;
    const double distance = std::hypot(raw.x(), raw.y());
    const QSize size = view_->viewport()->size();
    const double radius = std::hypot(double(size.width()), double(size.height())) / 2.0;
    if (radius <= 0.0 || baseScale_ <= 0.0)
        return true;

    // The travel is clamped to the reference radius: at the far end the
    // peek is fully open and moving further changes nothing.
    const double travel = qMin(distance, radius);
    const QPointF direction = distance > 0.0 ? raw / distance : QPointF();
    const double t = travel / radius;

    // A simple lerp of the zoom factor, proportional to the zoom the
    // peek started from, and a pan in the pointer's direction measured
    // in screen pixels at that same zoom.
    const double scale = baseScale_ * (1.0 + t * (kMinZoomFactor - 1.0));
    const QPointF center = baseCenter_ + direction * (travel / baseScale_) * kPanGain;
    view_->applyPeekView(scale, center);
    return true;
}

bool PeekTool::mouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    // Any release ends the peek, whatever button it is, and the canvas
    // goes back to where it started.
    cancel();
    return true;
}

} // namespace ui
