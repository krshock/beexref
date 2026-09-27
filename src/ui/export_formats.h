#pragma once

#include "scene_export.h"

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QGraphicsScene;

namespace ui {

// One scene export format, as data (see sceneExportFormats()): the file
// dialog's filter, the suffix lookup and the write path all follow from
// the registry, so a new format is one entry.
struct SceneExportFormat
{
    QString id;           // "png"
    QStringList suffixes; // {"png"}
    QString name;         // "PNG", the filter's label
    // Raster formats ask for a pixel size first; vector formats
    // export directly.
    bool asksSize = true;
    // Writes the scene to path: an empty string on success, otherwise
    // the message to show. `size` is the dialog's choice for raster
    // formats and the frame's default size otherwise.
    std::function<QString(QGraphicsScene &, const SceneExportFrame &, const QSize &size,
                          const QString &path)> write;
};

// The registered formats in dialog order; the first is the default for
// an unknown or missing suffix (the pixmap exporter).
const QVector<SceneExportFormat> &sceneExportFormats();
// The format owning a file suffix (case-insensitive), or nullptr.
const SceneExportFormat *sceneExportFormatForSuffix(const QString &suffix);
// The "Image Files (...);;PNG (...);;..." filter built from the registry.
QString sceneExportFilter();

} // namespace ui
