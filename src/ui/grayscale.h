#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include <functional>

namespace ui {

// One grayscale conversion. Every method has the same contract: any
// input format in, Format_Grayscale8 of the same size out, alpha
// composited onto the canvas colour, null in -> null out. The table is
// data (grayscaleMethods()), so a new method is one entry plus its
// converter.
struct GrayscaleMethod
{
    QString id;     // Items/grayscale_method value
    QString label;  // menu label
    QString help;   // one-line tooltip
    std::function<QImage(const QImage &)> convert;
};

// The methods in menu order; the first is the default (Classic).
const QVector<GrayscaleMethod> &grayscaleMethods();
// The method for an id, or nullptr.
const GrayscaleMethod *grayscaleMethod(const QString &id);
// The default method's id.
QString defaultGrayscaleMethod();

// Converts with the named method; an unknown id uses the default, whose
// output is byte-identical to the conversion BeeXRef always used.
QImage grayscaleImage(const QImage &image, const QString &methodId);

} // namespace ui
