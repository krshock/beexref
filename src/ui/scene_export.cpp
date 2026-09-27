#include "scene_export.h"

#include "item_types.h"

#include "scene.h"
#include "scene_item.h"
#include "theme.h"

#include <QBuffer>
#include <QFont>
#include <QPainter>
#include <QPointF>
#include <QStringList>
#include <QXmlStreamWriter>

#include <algorithm>

namespace ui {
namespace {

// The margin and JPEG quality.
constexpr double kMarginFraction = 0.03;
constexpr int kImageQuality = 90;

// The grayscale image: a Grayscale8 image filled with
// the canvas colour, with the original composited on top.
QImage grayscaleOf(const QImage &image)
{
    QImage gray(image.size(), QImage::Format_Grayscale8);
    gray.fill(theme::canvas);
    QPainter painter(&gray);
    painter.drawImage(0, 0, image);
    painter.end();
    return gray;
}

struct EncodedImage
{
    QByteArray bytes;
    QString format;
};

// The bytes for one item's SVG <image>. Without edits the item's own
// encoded buffer is reused verbatim; with grayscale/crop the full
// resolution image is decoded and re-encoded.
// Deviation: the re-encode format would normally follow the
// image_storage_format setting; this port keeps the item's original
// format, matching how blobs are stored here.
EncodedImage encodedForItem(const doc::ItemPtr &item, bool applyGrayscale, bool applyCrop)
{
    EncodedImage out;
    out.format = item->format.isEmpty() ? QStringLiteral("png") : item->format;
    if (!item->hasSource())
        return out;

    const QByteArray original = item->source->bytes();
    if (!applyGrayscale && !applyCrop) {
        out.bytes = original;
        return out;
    }

    QImage image = QImage::fromData(original);
    if (image.isNull())
        return out;

    if (applyGrayscale && item->grayscale())
        image = grayscaleOf(image);
    if (applyCrop && item->hasCrop())
        image = image.copy(item->crop().toRect());

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, out.format.toUpper().toUtf8().constData(), kImageQuality);
    out.bytes = bytes;
    return out;
}

QString textStyle(const QFont &font, double scale)
{
    QString fontStyle = QStringLiteral("normal");
    if (font.style() == QFont::StyleItalic)
        fontStyle = QStringLiteral("italic");
    else if (font.style() == QFont::StyleOblique)
        fontStyle = QStringLiteral("oblique");
    const QString family = font.families().join(QStringLiteral(", "));
    return QStringList{QStringLiteral("white-space:pre"),
                       QStringLiteral("font-size:%1pt").arg(font.pointSizeF() * scale),
                       QStringLiteral("font-family:%1").arg(family),
                       QStringLiteral("font-weight:%1").arg(font.weight()),
                       QStringLiteral("font-stretch:%1").arg(font.stretch()),
                       QStringLiteral("font-style:%1").arg(fontStyle)}
        .join(QLatin1Char(';'));
}

QString transformsFor(const doc::ItemPtr &item, const QPointF &anchor)
{
    QStringList transforms;
    if (item->flip == -1) {
        // transform-origin is not portable, so the origin is fixed by
        // hand around the flip.
        transforms << QStringLiteral("translate(%1 %2)").arg(anchor.x()).arg(anchor.y());
        transforms << QStringLiteral("scale(%1 1)").arg(item->flip);
        transforms << QStringLiteral("translate(-%1 -%2)").arg(anchor.x()).arg(anchor.y());
    }
    transforms << QStringLiteral("rotate(%1 %2 %3)")
                      .arg(item->rotation)
                      .arg(anchor.x())
                      .arg(anchor.y());
    return transforms.join(QLatin1Char(' '));
}

} // namespace

SceneExportFrame sceneExportFrame(QGraphicsScene &scene)
{
    SceneExportFrame frame;
    frame.rect = scene.itemsBoundingRect();
    const QSize size(qMax(0, int(frame.rect.width())), qMax(0, int(frame.rect.height())));
    frame.margin = qMax(size.width(), size.height()) * kMarginFraction;
    frame.defaultSize =
        QSize(size.width() + 2 * int(frame.margin), size.height() + 2 * int(frame.margin));
    return frame;
}

QImage renderSceneToImage(QGraphicsScene &scene, const SceneExportFrame &frame, const QSize &size,
                          const QColor &canvas)
{
    const double margin =
        frame.defaultSize.width() > 0
            ? frame.margin * size.width() / double(frame.defaultSize.width())
            : 0.0;

    QImage image(size, QImage::Format_RGB32);
    image.fill(canvas);
    QPainter painter(&image);
    const QRectF target(margin, margin, size.width() - 2 * margin, size.height() - 2 * margin);
    scene.render(&painter, target, frame.rect);
    painter.end();
    return image;
}

QByteArray renderSceneToSvg(QGraphicsScene &scene, const SceneExportFrame &frame)
{
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument(QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeAttribute(QStringLiteral("width"), QString::number(frame.defaultSize.width()));
    xml.writeAttribute(QStringLiteral("height"), QString::number(frame.defaultSize.height()));
    xml.writeAttribute(QStringLiteral("xmlns"), QStringLiteral("http://www.w3.org/2000/svg"));
    xml.writeAttribute(QStringLiteral("xmlns:xlink"),
                       QStringLiteral("http://www.w3.org/1999/xlink"));

    const QPointF offset = frame.rect.topLeft() - QPointF(frame.margin, frame.margin);

    // Elements are ordered by z in the tree, not by an attribute.
    QList<QGraphicsItem *> items = scene.items(Qt::AscendingOrder);
    std::stable_sort(items.begin(), items.end(),
                     [](QGraphicsItem *a, QGraphicsItem *b) { return a->zValue() < b->zValue(); });

    for (QGraphicsItem *graphicsItem : items) {
        auto *item = dynamic_cast<SceneItem *>(graphicsItem);
        if (!item)
            continue;
        const doc::ItemPtr model = item->item();
        // Error and unknown types have no SVG representation; the traits
        // table says which types do.
        const item_types::Traits &traits = item_types::forType(model->type);
        if (model->isError() || !traits.svgElement)
            continue;

        const QPointF pos = item->pos() - offset;
        QPointF elementPos = pos;

        const bool isText = QLatin1String(traits.svgElement) == QLatin1String("text");
        if (isText) {
            xml.writeStartElement(QStringLiteral("text"));
            xml.writeAttribute(QStringLiteral("style"), textStyle(item->font(), model->scale));
            xml.writeAttribute(QStringLiteral("dominant-baseline"), QStringLiteral("hanging"));
        } else {
            const EncodedImage encoded =
                encodedForItem(model, /*applyGrayscale=*/true, /*applyCrop=*/true);
            const QString format =
                encoded.format.isEmpty() ? QStringLiteral("png") : encoded.format.toLower();
            xml.writeStartElement(QStringLiteral("image"));
            xml.writeAttribute(QStringLiteral("xlink:href"),
                               QStringLiteral("data:image/%1;base64,%2")
                                   .arg(format, QString::fromLatin1(encoded.bytes.toBase64())));
            // The app sizes the element to the full image width,
            // even though the bytes are cropped; kept for fidelity.
            xml.writeAttribute(QStringLiteral("width"),
                               QString::number(item->imageBounds().width() * model->scale));
            xml.writeAttribute(QStringLiteral("height"),
                               QString::number(item->imageBounds().height() * model->scale));
            xml.writeAttribute(QStringLiteral("image-rendering"),
                               model->scale > 2 ? QStringLiteral("crisp-edges")
                                                : QStringLiteral("optimizeQuality"));
            // The cropped pixmap is anchored at the crop's top-left.
            elementPos += item->displayBounds().topLeft();
        }

        xml.writeAttribute(QStringLiteral("transform"), transformsFor(model, pos));
        xml.writeAttribute(QStringLiteral("x"), QString::number(elementPos.x()));
        xml.writeAttribute(QStringLiteral("y"), QString::number(elementPos.y()));
        xml.writeAttribute(QStringLiteral("opacity"), QString::number(model->opacity()));
        // The characters come last: an attribute written after content
        // would be serialized into the text by QXmlStreamWriter.
        if (isText)
            xml.writeCharacters(model->text());
        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndDocument();
    return out;
}

} // namespace ui
