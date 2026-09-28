#include "grayscale.h"

#include "theme.h"

#include <QPainter>

#include <array>
#include <cmath>
#include <cstddef>

namespace ui {
namespace {

// The classic conversion: draw the colour image onto a Grayscale8 canvas
// filled with the canvas colour, which flattens transparency onto it.
// Kept as the painter path so the default output stays byte-identical to
// what the app always produced.
QImage classicGrayscale(const QImage &image)
{
    QImage gray(image.size(), QImage::Format_Grayscale8);
    gray.fill(theme::canvas);
    QPainter painter(&gray);
    painter.drawImage(0, 0, image);
    painter.end();
    return gray;
}

// The shared per-pixel path for the other methods: alpha is composited
// onto the canvas colour first, then the formula maps the three channels
// to one. One scanline pass, integer math, no per-pixel QImage calls.
template <typename Formula>
QImage convertGrayscale(const QImage &image, Formula formula)
{
    QImage source = image;
    if (source.format() != QImage::Format_ARGB32 && source.format() != QImage::Format_RGB32)
        source = source.convertToFormat(QImage::Format_ARGB32);

    QImage gray(source.size(), QImage::Format_Grayscale8);
    if (gray.isNull())
        return {};
    const int bgR = theme::canvas.red();
    const int bgG = theme::canvas.green();
    const int bgB = theme::canvas.blue();

    for (int y = 0; y < source.height(); ++y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        uchar *out = gray.scanLine(y);
        for (int x = 0; x < source.width(); ++x) {
            const QRgb pixel = in[x];
            const int alpha = qAlpha(pixel);
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            if (alpha != 255) {
                r = (r * alpha + bgR * (255 - alpha) + 127) / 255;
                g = (g * alpha + bgG * (255 - alpha) + 127) / 255;
                b = (b * alpha + bgB * (255 - alpha) + 127) / 255;
            }
            out[x] = static_cast<uchar>(qBound(0, formula(r, g, b), 255));
        }
    }
    return gray;
}

QImage bt601Grayscale(const QImage &image)
{
    // 0.299/0.587/0.114 in 8-bit fixed point (weights sum to 256).
    return convertGrayscale(
        image, [](int r, int g, int b) { return (77 * r + 150 * g + 29 * b + 128) >> 8; });
}

QImage bt709Grayscale(const QImage &image)
{
    // 0.2126/0.7152/0.0722 in 8-bit fixed point.
    return convertGrayscale(
        image, [](int r, int g, int b) { return (54 * r + 183 * g + 19 * b + 128) >> 8; });
}

QImage averageGrayscale(const QImage &image)
{
    return convertGrayscale(image, [](int r, int g, int b) { return (r + g + b) / 3; });
}

QImage lightnessGrayscale(const QImage &image)
{
    return convertGrayscale(image, [](int r, int g, int b) {
        return (qMax(r, qMax(g, b)) + qMin(r, qMin(g, b))) / 2;
    });
}

QImage maxGrayscale(const QImage &image)
{
    return convertGrayscale(image, [](int r, int g, int b) { return qMax(r, qMax(g, b)); });
}

QImage minGrayscale(const QImage &image)
{
    return convertGrayscale(image, [](int r, int g, int b) { return qMin(r, qMin(g, b)); });
}

// sRGB <-> linear light tables, built once: the linear method sums the
// linearised channels with the Rec.709 weights and re-encodes.
const std::array<quint16, 256> &srgbToLinear()
{
    static const std::array<quint16, 256> table = [] {
        std::array<quint16, 256> values{};
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            const double linear = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            values[std::size_t(i)] = static_cast<quint16>(std::lround(linear * 65535.0));
        }
        return values;
    }();
    return table;
}

const std::array<uchar, 4096> &linearToSrgb()
{
    static const std::array<uchar, 4096> table = [] {
        std::array<uchar, 4096> values{};
        for (int i = 0; i < 4096; ++i) {
            const double linear = i / 4095.0;
            const double c = linear <= 0.0031308 ? 12.92 * linear
                                                 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
            values[std::size_t(i)] = static_cast<uchar>(qBound(0, int(std::lround(c * 255.0)), 255));
        }
        return values;
    }();
    return table;
}

QImage linearGrayscale(const QImage &image)
{
    const std::array<quint16, 256> &toLinear = srgbToLinear();
    const std::array<uchar, 4096> &toSrgb = linearToSrgb();
    return convertGrayscale(image, [&toLinear, &toSrgb](int r, int g, int b) {
        // Rec.709 luminance in 16-bit fixed point, then back to sRGB.
        const quint32 y = (2126u * toLinear[std::size_t(r)] + 7152u * toLinear[std::size_t(g)]
                           + 722u * toLinear[std::size_t(b)])
            / 10000u;
        return int(toSrgb[(y * 4095u + 32767u) / 65535u]);
    });
}

} // namespace

const QVector<GrayscaleMethod> &grayscaleMethods()
{
    static const QVector<GrayscaleMethod> methods = {
        {QStringLiteral("classic"), QStringLiteral("Classic"),
         QStringLiteral("The grayscale BeeXRef has always used"), classicGrayscale},
        {QStringLiteral("bt601"), QStringLiteral("BT.601 luma"),
         QStringLiteral("TV-style luma, gentle on skin tones"), bt601Grayscale},
        {QStringLiteral("bt709"), QStringLiteral("BT.709 luma"),
         QStringLiteral("The sRGB standard, what CSS and SVG filters use"), bt709Grayscale},
        {QStringLiteral("linear"), QStringLiteral("Linear luminance"),
         QStringLiteral("Physically correct light, brighter saturated colours"), linearGrayscale},
        {QStringLiteral("average"), QStringLiteral("Average"),
         QStringLiteral("The plain mean of red, green and blue"), averageGrayscale},
        {QStringLiteral("lightness"), QStringLiteral("Lightness"),
         QStringLiteral("Halfway between the brightest and darkest channel"), lightnessGrayscale},
        {QStringLiteral("max"), QStringLiteral("Max"),
         QStringLiteral("The brightest channel only"), maxGrayscale},
        {QStringLiteral("min"), QStringLiteral("Min"),
         QStringLiteral("The darkest channel only"), minGrayscale},
    };
    return methods;
}

const GrayscaleMethod *grayscaleMethod(const QString &id)
{
    for (const GrayscaleMethod &method : grayscaleMethods()) {
        if (method.id == id)
            return &method;
    }
    return nullptr;
}

QString defaultGrayscaleMethod()
{
    return grayscaleMethods().first().id;
}

QImage grayscaleImage(const QImage &image, const QString &methodId)
{
    if (image.isNull())
        return {};
    const GrayscaleMethod *method = grayscaleMethod(methodId);
    if (!method)
        method = &grayscaleMethods().first();
    return method->convert(image);
}

} // namespace ui
