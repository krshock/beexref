#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QString>

namespace ui::colors {

// The hex format: #rrggbb, plus the alpha byte when the
// colour is not opaque (#rrggbbaa).
QString hex(const QColor &color);

// Row/column stride for the gamut histogram: about a thousand samples
// per side.
int gamutSampleStep(int width, int height);

// One hue/saturation bucket of the gamut. Hue follows Qt: 0..359, and
// -1 for achromatic pixels.
struct GamutKey
{
    int hue = -1;
    int saturation = 0; // 0..255

    bool operator==(const GamutKey &other) const
    {
        return hue == other.hue && saturation == other.saturation;
    }
};

inline uint qHash(const GamutKey &key, uint seed = 0)
{
    return ::qHash((key.hue + 1) * 256 + key.saturation, seed);
}

// Counts hue/saturation pairs, ignoring pixels that are close to
// transparent, white or black.
QHash<GamutKey, int> gamutHistogram(const QImage &image);

// Where a bucket's dot goes on a colour wheel of the given radius,
// centred on (radius, radius): hue around the circle, saturation out
// from the centre.
QPointF gamutDotPosition(const GamutKey &key, int radius);

} // namespace ui::colors
