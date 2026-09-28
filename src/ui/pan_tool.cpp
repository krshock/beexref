#include "pan_tool.h"

#include "view.h"

#include <QMouseEvent>
#include <QTimer>

#include <cmath>

namespace ui {
namespace {

// The glide's feel: a short, subtle coast, not a slide. A release below
// kMinGlideSpeed (px/s) stops dead, a pause longer than kStaleMoveNs
// before the release does not glide at all, and the whole coast is capped
// at kMaxGlideNs. The velocity decays with kGlideDecayTau, so the distance
// is roughly speed * tau.
constexpr double kVelocitySmoothingTau = 0.08;
constexpr double kGlideDecayTau = 0.28;
constexpr double kMinGlideSpeed = 60.0;
constexpr double kMaxGlideSpeed = 2500.0;
constexpr double kMaxGlideStep = 60.0;
constexpr qint64 kStaleMoveNs = 120'000'000;
constexpr qint64 kMaxGlideNs = 1'500'000'000;

} // namespace

PanTool::PanTool(View *view, QTimer *glideTimer)
    : view_(view)
    , glideTimer_(glideTimer)
{
}

void PanTool::start(const QPoint &viewportPos)
{
    stopGlide();
    active_ = true;
    start_ = viewportPos;
    velocity_ = {};
    clock_.start();
    lastMoveNs_ = clock_.nsecsElapsed();
    view_->viewport()->setCursor(Qt::ClosedHandCursor);
}

void PanTool::stopGlide()
{
    if (glideTimer_)
        glideTimer_->stop();
}

void PanTool::cancel()
{
    stopGlide();
    if (!active_)
        return;
    active_ = false;
    view_->viewport()->unsetCursor();
}

bool PanTool::mouseMove(QMouseEvent *event)
{
    if (!active_)
        return false;
    const QPoint position = event->position().toPoint();
    // Content follows the cursor: the app pans by
    // (start - current), which is the negated scrollbar delta.
    const QPoint delta = start_ - position;
    view_->panStep(delta);
    start_ = position;

    // Smooth the velocity over the last moves, so a jittery drag does not
    // launch the canvas.
    const qint64 now = clock_.nsecsElapsed();
    const double dt = qMax(1e-3, (now - lastMoveNs_) / 1e9);
    lastMoveNs_ = now;
    const double alpha = 1.0 - std::exp(-dt / kVelocitySmoothingTau);
    velocity_ += alpha * (QPointF(delta) / dt - velocity_);
    return true;
}

bool PanTool::mouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    const qint64 now = clock_.nsecsElapsed();
    const bool moving = (now - lastMoveNs_) < kStaleMoveNs;
    cancel(); // ends the drag and any previous glide
    if (!moving)
        return true;

    double speed = std::hypot(velocity_.x(), velocity_.y());
    if (speed < kMinGlideSpeed)
        return true;
    if (speed > kMaxGlideSpeed)
        velocity_ *= kMaxGlideSpeed / speed;
    glideStartNs_ = now;
    lastTickNs_ = now;
    glideTimer_->start();
    return true;
}

void PanTool::tick()
{
    const qint64 now = clock_.nsecsElapsed();
    const double dt = qMax(1e-3, (now - lastTickNs_) / 1e9);
    lastTickNs_ = now;

    const QPointF step = velocity_ * dt;
    const QPoint clamped(int(qBound(-kMaxGlideStep, step.x(), kMaxGlideStep)),
                         int(qBound(-kMaxGlideStep, step.y(), kMaxGlideStep)));
    if (!clamped.isNull())
        view_->panStep(clamped);

    velocity_ *= std::exp(-dt / kGlideDecayTau);
    const double speed = std::hypot(velocity_.x(), velocity_.y());
    if (speed < kMinGlideSpeed || (now - glideStartNs_) > kMaxGlideNs)
        stopGlide();
}

} // namespace ui
