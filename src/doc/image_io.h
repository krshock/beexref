#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace doc {

// An image on its way into the board: the display image (EXIF
// orientation applied) plus the authoritative encoded bytes to keep.
struct LoadedImage
{
    QImage image;
    QByteArray encoded;
    QString format;
    QString source; // file path or URL

    bool isValid() const { return !image.isNull(); }
};

// Decodes bytes and applies EXIF orientation. The original bytes are
// kept when they decode to the same pixel size; otherwise the oriented
// image is stored as lossless PNG, so nothing is lost to re-encoding.
// This is the reference's encoded_image_data rule.
LoadedImage loadImageData(const QByteArray &bytes, const QString &source = {});

LoadedImage loadImageFile(const QString &path);

// Raw image data (clipboard, canvas drags): stored as lossless PNG.
LoadedImage imageToLoaded(const QImage &image, const QString &source = {});

QByteArray encodePng(const QImage &image);

} // namespace doc
