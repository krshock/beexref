#pragma once

#include <QColor>
#include <QWidget>

namespace ui {

// The reference's cursor-following colour preview: a square filled with
// the colour under the pointer (transparent when there is none), offset
// from the cursor. It never takes mouse events.
class ColorSwatch : public QWidget
{
    Q_OBJECT

public:
    // The reference's size and offset from the pointer.
    static constexpr int kSize = 50;
    static constexpr int kOffset = 10;

    explicit ColorSwatch(QWidget *parent = nullptr);

    QColor color() const { return color_; }
    void setColor(const QColor &color);
    // Places the swatch at a point in the parent's coordinates.
    void moveNear(const QPoint &pos);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor color_;
};

} // namespace ui
