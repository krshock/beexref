#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>

class QGraphicsScene;

namespace ui {

// Scene export, ported from the reference's fileio/export.py. The render
// touches the scene and its items, so it runs on the GUI thread; only the
// file write could move to a worker (the app writes synchronously).

// The export frame: the items' bounding rect plus the reference's 3 %
// margin, and the default pixel size of that frame.
struct SceneExportFrame
{
    QRectF rect;      // items bounding rect
    double margin = 0; // 3 % of the longer side
    QSize defaultSize; // rect grown by the margin, in pixels
};

SceneExportFrame sceneExportFrame(QGraphicsScene &scene);

// JPEG quality for the pixmap exporters, matching the reference's
// QImage.save(..., quality=90).
inline constexpr int kSceneExportQuality = 90;

// Renders the scene into a size-sized image filled with the canvas
// colour, keeping the reference's margin proportional to the size.
QImage renderSceneToImage(QGraphicsScene &scene, const SceneExportFrame &frame, const QSize &size,
                          const QColor &canvas);

// Builds the SVG document as the reference does: elements in z order,
// pixmaps embedded as base64 with crop and grayscale applied, text
// elements, and flip/rotation transforms. Returns the serialized SVG.
QByteArray renderSceneToSvg(QGraphicsScene &scene, const SceneExportFrame &frame);

} // namespace ui
