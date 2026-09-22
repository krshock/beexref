#include "grayscale.h"

#include "theme.h"

#include <QPainter>

namespace ui {

QImage grayscaleImage(const QImage &image)
{
    if (image.isNull())
        return {};

    QImage gray(image.size(), QImage::Format_Grayscale8);
    gray.fill(theme::canvas);
    QPainter painter(&gray);
    painter.drawImage(0, 0, image);
    painter.end();
    return gray;
}

} // namespace ui
