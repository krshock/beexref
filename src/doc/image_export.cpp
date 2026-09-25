#include "image_export.h"

#include "util/format.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace doc {
namespace {

// 255 bytes is the usual limit for one path component (ext4, XFS, ...).
constexpr int kMaxNameBytes = 255;

} // namespace

QString exportFilename(const QString &filename, const QString &format, qint64 saveId)
{
    QString stem;
    if (!filename.isEmpty()) {
        const QUrl url(filename);
        const bool remote =
            url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https");
        if (remote) {
            // Boards from the Python reference store the full URL as the
            // item's filename; its os.path.splitext(basename()) keeps the
            // query string and can leave characters no filesystem
            // accepts. filenameFromUrl() is the same rule used when the
            // item was imported, so the export name matches the item.
            stem = QFileInfo(util::filenameFromUrl(url)).completeBaseName();
            // Leave room for the "NNNN-" prefix and the ".format" suffix.
            const int budget = kMaxNameBytes - 5 - 1 - format.toUtf8().size();
            stem = util::truncateUtf8(stem, qMax(0, budget));
        } else {
            // Local names are used as they are, like the reference.
            stem = QFileInfo(filename).completeBaseName();
        }
    }
    if (stem.isEmpty())
        return QStringLiteral("%1.%2").arg(saveId, 4, 10, QLatin1Char('0')).arg(format);
    return QStringLiteral("%1-%2.%3")
        .arg(saveId, 4, 10, QLatin1Char('0'))
        .arg(stem, format);
}

ImageExportSummary exportImages(
    const QVector<ItemPtr> &items, const QString &dir,
    const std::function<std::optional<ExportConflict>(const QString &)> &resolve,
    const std::function<void(int done, int total)> &progress,
    const std::function<bool()> &cancelled)
{
    ImageExportSummary summary;

    // Only pixmaps are exported; the reference walks items_by_type().
    QVector<ItemPtr> pixmaps;
    qint64 maxSaveId = 0;
    for (const ItemPtr &item : items) {
        if (!item->isPixmap())
            continue;
        pixmaps << item;
        if (item->id > maxSaveId)
            maxSaveId = item->id;
    }

    const int total = pixmaps.size();
    if (progress)
        progress(0, total);

    bool skipAll = false;
    bool overwriteAll = false;

    for (int i = 0; i < total; ++i) {
        if (cancelled && cancelled()) {
            summary.cancelled = true;
            break;
        }
        const ItemPtr &item = pixmaps.at(i);

        const QByteArray bytes = item->hasSource() ? item->source->bytes() : QByteArray();
        if (bytes.isEmpty()) {
            summary.errors << QStringLiteral("%1: no readable image data").arg(item->filename);
            continue;
        }
        const QString format =
            item->format.isEmpty() ? QStringLiteral("png") : item->format.toLower();

        qint64 saveId = item->id;
        if (saveId <= 0)
            saveId = ++maxSaveId;
        const QString name = exportFilename(item->filename, format, saveId);
        const QString target = QDir(dir).filePath(name);

        const bool exists = QFileInfo::exists(target);
        if (exists && !skipAll && !overwriteAll) {
            const std::optional<ExportConflict> choice =
                resolve ? resolve(target) : ExportConflict::Overwrite;
            if (!choice) {
                summary.cancelled = true;
                break;
            }
            switch (*choice) {
            case ExportConflict::Skip:
                ++summary.skipped;
                continue;
            case ExportConflict::SkipAll:
                skipAll = true;
                ++summary.skipped;
                continue;
            case ExportConflict::Overwrite:
                break;
            case ExportConflict::OverwriteAll:
                overwriteAll = true;
                break;
            }
        } else if (exists) {
            if (skipAll) {
                ++summary.skipped;
                continue;
            }
            // overwriteAll falls through to the write.
        }

        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            summary.errors << QStringLiteral("%1: %2").arg(target, file.errorString());
            continue;
        }
        if (file.write(bytes) != bytes.size()) {
            summary.errors << QStringLiteral("%1: %2").arg(target, file.errorString());
            file.close();
            continue;
        }
        file.close();
        ++summary.written;

        if (progress)
            progress(i + 1, total);
    }

    if (progress)
        progress(total, total);

    return summary;
}

} // namespace doc
