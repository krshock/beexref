#include "move_window_tool.h"

#include "view.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace ui {

MoveWindowTool::MoveWindowTool(View *view, QTimer *timer)
    : view_(view)
    , timer_(timer)
{
}

void MoveWindowTool::toggle()
{
    if (active_)
        exit();
    else
        enter();
}

void MoveWindowTool::enter()
{
    if (active_)
        return;
    active_ = true;
    view_->viewport()->setCursor(Qt::SizeAllCursor);
    global_ = QCursor::pos();
    // A drag ends when its button comes up; an armed mode (the action
    // from the keyboard or the menu) has no button and ends on the next
    // press or key instead.
    pressed_ = QGuiApplication::mouseButtons() != Qt::NoButton;
    wasActive_ = view_->window() && view_->window()->isActiveWindow();
    timer_->start();
}

void MoveWindowTool::exit()
{
    if (!active_)
        return;
    active_ = false;
    pressed_ = false;
    wasActive_ = false;
    timer_->stop();
    view_->viewport()->unsetCursor();
}

void MoveWindowTool::tick()
{
    if (!active_)
        return;
    // A drag that was released outside the window never reaches
    // mouseReleaseEvent; the global button state does.
    if (pressed_ && QGuiApplication::mouseButtons() == Qt::NoButton) {
        exit();
        return;
    }
    // The armed mode should not keep following the cursor once another
    // application comes to the front (only when the window was active
    // when it was armed).
    if (!pressed_ && wasActive_ && view_->window() && !view_->window()->isActiveWindow()) {
        exit();
        return;
    }
    const QPointF global = QCursor::pos();
    const QPointF delta = global - global_;
    if (delta.isNull())
        return;
    global_ = global;
    if (QWidget *top = view_->window())
        top->move(top->pos() + delta.toPoint());
}

bool MoveWindowTool::mousePress(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    // Any press ends the mode.
    exit();
    return true;
}

bool MoveWindowTool::mouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    // A drag ends when its button comes up.
    exit();
    return true;
}

bool MoveWindowTool::keyPress(QKeyEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    exit();
    return true;
}

} // namespace ui
