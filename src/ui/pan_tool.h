#pragma once

#include "tool.h"

#include <QPoint>

class QMouseEvent;

namespace ui {

class View;

// The pan mode: a bound button drag scrolls the view. The
// view starts it when the mouse binding matches and dispatches the moves
// and the release through the controller.
class PanTool : public Tool
{
public:
    explicit PanTool(View *view);

    QString id() const override { return QStringLiteral("pan"); }
    bool active() const override { return active_; }
    void cancel() override;

    // Starts a pan drag at a viewport position.
    void start(const QPoint &viewportPos);

    bool mouseMove(QMouseEvent *event) override;
    bool mouseRelease(QMouseEvent *event) override;

private:
    View *view_ = nullptr;
    bool active_ = false;
    QPoint start_;
};

} // namespace ui
