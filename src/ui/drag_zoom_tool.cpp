#include "drag_zoom_tool.h"

#include "view.h"

#include <QMouseEvent>

namespace ui {

DragZoomTool::DragZoomTool(View *view)
    : view_(view)
{
}

void DragZoomTool::start(const QPoint &viewportPos, bool inverted)
{
    active_ = true;
    inverted_ = inverted;
    last_ = viewportPos;
    anchor_ = viewportPos;
}

void DragZoomTool::cancel()
{
    active_ = false;
}

bool DragZoomTool::mouseMove(QMouseEvent *event)
{
    if (!active_)
        return false;
    const QPoint position = event->position().toPoint();
    // The reference zooms by the vertical drag, twenty times the wheel
    // step per pixel, anchored where the drag started.
    int delta = last_.y() - position.y();
    if (inverted_)
        delta *= -1;
    last_ = position;
    view_->zoomAt(delta * 20, anchor_);
    return true;
}

bool DragZoomTool::mouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    active_ = false;
    return true;
}

} // namespace ui
