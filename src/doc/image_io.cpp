#include "image_io.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>

namespace doc {
namespace {

struct Decoded
{
    QImage image;    // oriented for display
    QSize rawSize;   // stored pixel size, before EXIF orientation
    QString format;
};

Decoded decodeOriented(const QByteArray &bytes)
{
    Decoded decoded;
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return decoded;

    QImageReader reader(&buffer);
    // Capture the stored size before autoTransform: the reference keeps
    // the original bytes only when orientation did not change it.
    decoded.rawSize = reader.size();
    decoded.format = QString::fromLatin1(reader.format());
    reader.setAutoTransform(true);
    decoded.image = reader.read();
    return decoded;
}

} // namespace

QByteArray encodePng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    QImageWriter writer(&buffer, QByteArrayLiteral("PNG"));
    if (!writer.write(image))
        return {};
    return bytes;
}

LoadedImage loadImageData(const QByteArray &bytes, const QString &source)
{
    const Decoded decoded = decodeOriented(bytes);
    if (decoded.image.isNull())
        return {};

    LoadedImage loaded;
    loaded.image = decoded.image;
    loaded.source = source;
    if (decoded.rawSize.isValid() && !decoded.rawSize.isEmpty()
        && decoded.rawSize == decoded.image.size()) {
        loaded.encoded = bytes;
        loaded.format = decoded.format.isEmpty() ? QStringLiteral("png") : decoded.format;
    } else {
        loaded.encoded = encodePng(decoded.image);
        loaded.format = QStringLiteral("png");
    }
    if (loaded.encoded.isEmpty())
        return {};
    return loaded;
}

LoadedImage loadImageFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return loadImageData(file.readAll(), QFileInfo(path).absoluteFilePath());
}

LoadedImage imageToLoaded(const QImage &image, const QString &source)
{
    if (image.isNull())
        return {};
    LoadedImage loaded;
    loaded.image = image;
    loaded.encoded = encodePng(image);
    loaded.format = QStringLiteral("png");
    loaded.source = source;
    if (loaded.encoded.isEmpty())
        return {};
    return loaded;
}

} // namespace doc
