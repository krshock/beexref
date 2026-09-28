#pragma once

#include "tool.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>

class QMouseEvent;
class QTimer;

namespace ui {

class View;

// The pan mode: a bound button drag scrolls the view, and a fast release
// lets the canvas glide to a stop (momentum). The view starts it when the
// mouse binding matches and dispatches the moves and the release through
// the controller; the glide timer is owned by the view, like the
// move-window timer.
class PanTool : public Tool
{
public:
    PanTool(View *view, QTimer *glideTimer);

    QString id() const override { return QStringLiteral("pan"); }
    bool active() const override { return active_; }
    void cancel() override;

    // Starts a pan drag at a viewport position; a glide in progress ends.
    void start(const QPoint &viewportPos);
    // Ends a glide without touching the drag state (a new press, a wheel
    // step): the canvas stops where it is.
    void stopGlide();

    bool mouseMove(QMouseEvent *event) override;
    bool mouseRelease(QMouseEvent *event) override;

    // One glide frame, driven by the view's timer.
    void tick();

private:
    View *view_ = nullptr;
    QTimer *glideTimer_ = nullptr;
    bool active_ = false;
    QPoint start_;
    // The pan velocity in panStep pixels per second, smoothed over the
    // last moves; the glide decays it exponentially.
    QPointF velocity_;
    QElapsedTimer clock_;
    qint64 lastMoveNs_ = 0;
    qint64 glideStartNs_ = 0;
    qint64 lastTickNs_ = 0;
};

} // namespace ui
