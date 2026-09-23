#include "color_swatch.h"

#include <QPainter>

namespace ui {

ColorSwatch::ColorSwatch(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // The patch is painted by hand; no background of its own, so an
    // empty swatch stays transparent.
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kSize + 2 * kBorder, kSize + 2 * kBorder);
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
    // Black frame outside, white frame inside, then the sampled colour
    // (transparent when there is none, like the reference's NONE_COLOR).
    painter.fillRect(rect(), QColor(0, 0, 0));
    painter.fillRect(rect().adjusted(kBorder, kBorder, -kBorder, -kBorder), QColor(255, 255, 255));
    const QRect inner = rect().adjusted(2 * kBorder, 2 * kBorder, -2 * kBorder, -2 * kBorder);
    if (color_.isValid()) {
        painter.fillRect(inner, color_);
        return;
    }
    // No sampled colour: the patch is a hole, like the reference's
    // NONE_COLOR (a plain transparent fill would leave the white
    // border showing).
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(inner, Qt::transparent);
}

} // namespace ui
