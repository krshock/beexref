#pragma once

#include "tool.h"

#include <QPointF>

class QKeyEvent;
class QMouseEvent;
class QTimer;

namespace ui {

class View;

// The move-window mode: while active the window follows the
// global cursor on a timer, so it keeps moving when the pointer leaves
// the window. Any press or key ends it; a drag also ends when its button
// comes up, and an armed mode (menu/keyboard) ends when the window loses
// the activation it had when armed.
class MoveWindowTool : public Tool
{
public:
    MoveWindowTool(View *view, QTimer *timer);

    QString id() const override { return QStringLiteral("move_window"); }
    bool active() const override { return active_; }
    void cancel() override { exit(); }

    void toggle();
    void enter();
    void exit();

    // One timer step: follows the global cursor.
    void tick();

    bool mousePress(QMouseEvent *event) override;
    bool mouseRelease(QMouseEvent *event) override;
    bool keyPress(QKeyEvent *event) override;

private:
    View *view_ = nullptr;
    QTimer *timer_ = nullptr;
    bool active_ = false;
    // Whether the mode started from a held mouse button (a drag) or was
    // armed by the action (keyboard/menu), where no button is held.
    bool pressed_ = false;
    // Whether the window was active when the mode was armed: only then
    // does losing activation end it.
    bool wasActive_ = false;
    QPointF global_;
};

} // namespace ui
