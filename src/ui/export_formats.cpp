#include "export_formats.h"

#include "theme.h"

#include <QFile>
#include <QImage>

namespace ui {
namespace {

// One raster writer for every raster format: QImage picks the encoding
// from the file name, exactly as the pre-registry code did (so an
// unregistered suffix still writes what QImage makes of it).
QString writeRaster(QGraphicsScene &scene, const SceneExportFrame &frame, const QSize &size,
                    const QString &path)
{
    const QImage image = renderSceneToImage(scene, frame, size, theme::canvas);
    if (!image.save(path, nullptr, kSceneExportQuality))
        return QStringLiteral("Error writing file");
    return {};
}

QString writeSvg(QGraphicsScene &scene, const SceneExportFrame &frame, const QSize &,
                 const QString &path)
{
    const QByteArray svg = renderSceneToSvg(scene, frame);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return file.errorString();
    file.write(svg);
    file.close();
    return {};
}

} // namespace

const QVector<SceneExportFormat> &sceneExportFormats()
{
    static const QVector<SceneExportFormat> formats = {
        {QStringLiteral("png"), {QStringLiteral("png")}, QStringLiteral("PNG"), true, writeRaster},
        {QStringLiteral("jpeg"), {QStringLiteral("jpg"), QStringLiteral("jpeg")},
         QStringLiteral("JPEG"), true, writeRaster},
        {QStringLiteral("svg"), {QStringLiteral("svg")}, QStringLiteral("SVG"), false, writeSvg},
    };
    return formats;
}

const SceneExportFormat *sceneExportFormatForSuffix(const QString &suffix)
{
    const QString lower = suffix.toLower();
    for (const SceneExportFormat &format : sceneExportFormats()) {
        if (format.suffixes.contains(lower))
            return &format;
    }
    return nullptr;
}

QString sceneExportFilter()
{
    QStringList all;
    QStringList perFormat;
    for (const SceneExportFormat &format : sceneExportFormats()) {
        QStringList patterns;
        for (const QString &suffix : format.suffixes)
            patterns.append(QStringLiteral("*.") + suffix);
        all += patterns;
        perFormat.append(QStringLiteral("%1 (%2)").arg(format.name, patterns.join(QLatin1Char(' '))));
    }
    return QStringLiteral("Image Files (%1);;%2")
        .arg(all.join(QLatin1Char(' ')), perFormat.join(QStringLiteral(";;")));
}

} // namespace ui
