#pragma once

#include "tool.h"

#include <QPoint>

class QMouseEvent;

namespace ui {

class View;

// The reference's ZOOM_MODE: a bound button drag zooms by its vertical
// travel, twenty times the wheel step per pixel, anchored where the drag
// started.
class DragZoomTool : public Tool
{
public:
    explicit DragZoomTool(View *view);

    QString id() const override { return QStringLiteral("drag_zoom"); }
    bool active() const override { return active_; }
    void cancel() override;

    // Starts a zoom drag at a viewport position; inverted flips the
    // drag direction.
    void start(const QPoint &viewportPos, bool inverted);

    bool mouseMove(QMouseEvent *event) override;
    bool mouseRelease(QMouseEvent *event) override;

private:
    View *view_ = nullptr;
    bool active_ = false;
    bool inverted_ = false;
    QPoint last_;
    QPoint anchor_;
};

} // namespace ui
