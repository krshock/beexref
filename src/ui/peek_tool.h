#pragma once

#include "tool.h"

#include <QPoint>
#include <QPointF>
#include <QTransform>

class QMouseEvent;

namespace ui {

class View;

// The peek scene mode ("look at the horizon"): while the bound button
// is held, the pointer's travel from where the peek started drives a
// temporary zoom-out and a pan in that direction, showing the
// neighbourhood without losing the current view. Every move recomputes
// the whole view from the state the peek started from -- no physics, no
// timers; the release (or cancel) restores that state exactly.
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

private:
    View *view_ = nullptr;
    bool active_ = false;
    QPoint origin_;
    // The view the peek started from, restored exactly on release.
    QTransform baseTransform_;
    double baseScale_ = 1.0;
    QPointF baseCenter_;
    int baseHorizontal_ = 0;
    int baseVertical_ = 0;
};

} // namespace ui
