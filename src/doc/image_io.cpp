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
    // Capture the stored size before autoTransform: the app keeps
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

QByteArray encodeWebp(const QImage &image, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    QImageWriter writer(&buffer, QByteArrayLiteral("webp"));
    writer.setQuality(quality);
    if (!writer.write(image))
        return {};
    // Qt's WebP writer happily returns a truncated stream for some
    // trivial images (a solid image up to 16x16, say). Decode it back
    // and fail instead of storing something unreadable.
    QImage decoded;
    if (!decoded.loadFromData(bytes, "webp") || decoded.size() != image.size())
        return {};
    return bytes;
}

QByteArray encodeLossless(const QImage &image, QString *format)
{
    // WebP lossless is the smaller of the two when the plugin is there;
    // PNG is the guaranteed fallback.
    const QByteArray webp = encodeWebp(image, 100);
    if (!webp.isEmpty()) {
        if (format)
            *format = QStringLiteral("webp");
        return webp;
    }
    if (format)
        *format = QStringLiteral("png");
    return encodePng(image);
}

StorageMode storageModeForSetting(const QString &value)
{
    if (value == QLatin1String("lossless"))
        return StorageMode::Lossless;
    if (value == QLatin1String("compact"))
        return StorageMode::Compact;
    return StorageMode::Original;
}

void applyStorageMode(LoadedImage &loaded, StorageMode mode)
{
    if (loaded.encoded.isEmpty() || loaded.image.isNull() || mode == StorageMode::Original)
        return;

    const QString format = loaded.format.toLower();
    if (format == QLatin1String("webp"))
        return;
    // Another generation of a lossy source only loses more: a source
    // file's JPEG stays as it is.
    if (loaded.originalBytes
        && (format == QLatin1String("jpeg") || format == QLatin1String("jpg")))
        return;

    const QByteArray webp = mode == StorageMode::Compact && !loaded.image.hasAlphaChannel()
                                ? encodeWebp(loaded.image, 95)
                                : encodeWebp(loaded.image, 100);
    if (webp.isEmpty() || webp.size() >= loaded.encoded.size())
        return;
    loaded.encoded = webp;
    loaded.format = QStringLiteral("webp");
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
        loaded.originalBytes = true;
    } else {
        QString format;
        loaded.encoded = encodeLossless(decoded.image, &format);
        loaded.format = format;
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
    loaded.encoded = encodeLossless(image, &loaded.format);
    loaded.source = source;
    if (loaded.encoded.isEmpty())
        return {};
    return loaded;
}

} // namespace doc
