#pragma once

#include <QWidget>

namespace ui {

// The canvas corner's move handle: a 30x30 HUD element in the view's
// top-left corner (a child of the view, like the toasts) that starts the
// platform's interactive window move (the same as dragging a title bar)
// when pressed. The window shows it only while its title bar is off.
class MoveHandle : public QWidget
{
    Q_OBJECT

public:
    explicit MoveHandle(QWidget *parent = nullptr);

    static constexpr int kSize = 21;

signals:
    // The handle was pressed: start the window move.
    void moveRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
};

} // namespace ui
