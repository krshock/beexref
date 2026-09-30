#include "tool.h"

#include <QKeyEvent>
#include <QMouseEvent>

namespace ui {

ToolController::ToolController(View *view)
    : view_(view)
{
}

Tool *ToolController::active() const
{
    for (const auto &tool : tools_) {
        if (tool->active())
            return tool.get();
    }
    return nullptr;
}

QString ToolController::activeId() const
{
    if (const Tool *tool = active())
        return tool->id();
    return {};
}

void ToolController::cancel()
{
    for (const auto &tool : tools_) {
        if (tool->active())
            tool->cancel();
    }
}

bool ToolController::mousePress(QMouseEvent *event)
{
    Tool *tool = active();
    return tool && tool->mousePress(event);
}

bool ToolController::mouseMove(QMouseEvent *event)
{
    Tool *tool = active();
    return tool && tool->mouseMove(event);
}

bool ToolController::mouseRelease(QMouseEvent *event)
{
    Tool *tool = active();
    return tool && tool->mouseRelease(event);
}

bool ToolController::keyPress(QKeyEvent *event)
{
    Tool *tool = active();
    return tool && tool->keyPress(event);
}

bool ToolController::keyRelease(QKeyEvent *event)
{
    Tool *tool = active();
    return tool && tool->keyRelease(event);
}

} // namespace ui
