#include "color_tools.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace ui::colors {

QString hex(const QColor &color)
{
    const QString rgb = color.name(QColor::HexRgb);
    if (color.alpha() == 255)
        return rgb;
    return rgb + QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
}

int gamutSampleStep(int width, int height)
{
    const int longest = std::max(width, height);
    if (longest <= 1000)
        return 1;
    return longest / 1000;
}

QHash<GamutKey, int> gamutHistogram(const QImage &image)
{
    QHash<GamutKey, int> gamut;
    if (image.isNull())
        return gamut;

    const int step = gamutSampleStep(image.width(), image.height());
    for (int y = 0; y < image.height(); y += step) {
        for (int x = 0; x < image.width(); x += step) {
            const QColor color = image.pixelColor(x, y);
            if (color.alpha() <= 5)
                continue;
            const int red = color.red();
            const int green = color.green();
            const int blue = color.blue();
            // Only consider pixels that are not close to transparent,
            // white or black.
            if (std::min({red, green, blue}) >= 250 || std::max({red, green, blue}) <= 5)
                continue;

            GamutKey key;
            key.hue = color.hue(); // -1 when achromatic
            key.saturation = color.saturation();
            ++gamut[key];
        }
    }
    return gamut;
}

QPointF gamutDotPosition(const GamutKey &key, int radius)
{
    const double hypotenuse = key.saturation / 255.0 * radius;
    const double angle = qDegreesToRadians(-90.0 - key.hue);
    return QPointF(std::sin(angle) * hypotenuse + radius,
                   std::cos(angle) * hypotenuse + radius);
}

} // namespace ui::colors
