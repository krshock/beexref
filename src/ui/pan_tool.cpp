#include "pan_tool.h"

#include "view.h"

#include <QMouseEvent>

namespace ui {

PanTool::PanTool(View *view)
    : view_(view)
{
}

void PanTool::start(const QPoint &viewportPos)
{
    active_ = true;
    start_ = viewportPos;
    view_->viewport()->setCursor(Qt::ClosedHandCursor);
}

void PanTool::cancel()
{
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
    view_->panStep(start_ - position);
    start_ = position;
    return true;
}

bool PanTool::mouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    // A release ends the pan, whatever button it is.
    cancel();
    return true;
}

} // namespace ui
