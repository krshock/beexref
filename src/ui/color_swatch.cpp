#include "color_swatch.h"

#include <QPainter>

namespace ui {

ColorSwatch::ColorSwatch(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(kSize, kSize);
    hide();
}

void ColorSwatch::setColor(const QColor &color)
{
    if (color_ == color)
        return;
    color_ = color;
    update();
}

void ColorSwatch::moveNear(const QPoint &pos)
{
    move(pos.x() + kOffset, pos.y() + kOffset);
    raise();
}

void ColorSwatch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    // Without a sampled colour the swatch is transparent, like the
    // reference's NONE_COLOR.
    painter.fillRect(rect(), color_.isValid() ? color_ : QColor(0, 0, 0, 0));
}

} // namespace ui
