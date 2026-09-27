#pragma once

#include <QImage>

namespace ui {

// The grayscale conversion: the image is drawn onto an
// 8-bit grayscale canvas filled with the canvas colour, which flattens
// transparency onto that colour (the Python port documents this as the
// least-bad option; a straight Grayscale8 conversion would lose alpha).
QImage grayscaleImage(const QImage &image);

} // namespace ui
