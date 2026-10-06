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
    // True when `encoded` is the source file's own bytes (so re-encoding
    // would add another lossy generation); false for bytes this port
    // produced from a QImage.
    bool originalBytes = false;

    bool isValid() const { return !image.isNull(); }
};

// How an incoming image is encoded for storage.
enum class StorageMode
{
    // Keep the source bytes; images without a source (clipboard,
    // screenshots) become lossless WebP.
    Original,
    // Lossless WebP, or the smaller of it and what was there.
    Lossless,
    // Imperceptible loss: WebP at quality 95 for photographs, lossless
    // where artifacts would show (transparency). Already lossy sources
    // are never re-encoded.
    Compact,
};

// The mode stored in Items/image_storage_format. Unknown values -- the
// upstream format names included -- are the default: keep the
// originals.
StorageMode storageModeForSetting(const QString &value);

// Decodes bytes and applies EXIF orientation. The original bytes are
// kept when they decode to the same pixel size; otherwise the oriented
// image is stored losslessly, so nothing is lost to re-encoding.
LoadedImage loadImageData(const QByteArray &bytes, const QString &source = {});

LoadedImage loadImageFile(const QString &path);

// Raw image data (clipboard, canvas drags): stored losslessly.
LoadedImage imageToLoaded(const QImage &image, const QString &source = {});

QByteArray encodePng(const QImage &image);

// WebP bytes at the given quality: 100 is lossless (Qt maps it that
// way), anything lower is lossy. Empty when the WebP plugin is missing or
// the encoder produced a stream that does not decode back -- it truncates
// some trivial images (a solid 16x16, say), so every WebP this port
// writes is verified by decoding it again first.
QByteArray encodeWebp(const QImage &image, int quality);

// The smallest lossless encoding of the image: WebP when the plugin is
// there, PNG otherwise. Both decode to the exact same pixels; `format`
// receives which one it was.
QByteArray encodeLossless(const QImage &image, QString *format = nullptr);

// Rewrites loaded.encoded for the mode. The result is never larger than
// what was there: each path keeps the smaller of the two encodings, and
// an already lossy source is left alone.
void applyStorageMode(LoadedImage &loaded, StorageMode mode);

} // namespace doc
