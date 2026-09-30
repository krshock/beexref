#pragma once

#include "tool.h"

#include <QPoint>
#include <QPointF>
#include <QTransform>

class QKeyEvent;
class QMouseEvent;

namespace ui {

class View;

// The peek scene mode ("look at the horizon"): while the bound button
// is held, the pointer's travel from where the peek started drives a
// temporary zoom-out and a pan in that direction, showing the
// neighbourhood without losing the current view. Every move recomputes
// the whole view from the state the peek started from -- no physics, no
// timers. Releasing with Shift still held commits the look: the
// original zoom comes back, centered on the canvas point under the
// pointer. Releasing with Shift already let go cancels, and the state
// the peek started from is restored exactly.
class PeekTool : public Tool
{
public:
    explicit PeekTool(View *view);

    QString id() const override { return QStringLiteral("peek"); }
    bool active() const override { return active_; }
    void cancel() override;

    // Starts a peek at a viewport position, remembering the view to
    // return to.
    void start(const QPoint &viewportPos);

    bool mouseMove(QMouseEvent *event) override;
    bool mouseRelease(QMouseEvent *event) override;
    // Shift going up or down switches the preview between the commit
    // destination and the cancel one, so it is handled even while the
    // pointer does not move.
    bool keyPress(QKeyEvent *event) override;
    bool keyRelease(QKeyEvent *event) override;

private:
    // Shows the destination the release would pick: with Shift held the
    // commit lands on the pointer, with Shift let go the peek returns to
    // where it started.
    void updatePreview();
    View *view_ = nullptr;
    bool active_ = false;
    QPoint origin_;
    // The view the peek started from, restored exactly on release.
    QTransform baseTransform_;
    double baseScale_ = 1.0;
    QPointF baseCenter_;
    int baseHorizontal_ = 0;
    int baseVertical_ = 0;
    // The view target applied last, so a saturated peek (nothing left to
    // change) does not repaint the viewport on every pointer move.
    double appliedScale_ = 0.0;
    QPointF appliedCenter_;
    // The pointer's last viewport position and whether Shift is held:
    // together they pick which destination the preview marks.
    QPoint lastPosition_;
    bool shiftHeld_ = true;
};

} // namespace ui
