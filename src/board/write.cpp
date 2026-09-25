#include "write.h"

#include "constants.h"
#include "schema.h"
#include "sqlite.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryFile>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace board {
namespace {

// Shared with the runtime level policy; see constants.h.
constexpr int kFloorLevelSize = constants::kFloorLevelSize;

struct Thumbnail
{
    QByteArray data;
    QString format;
    QSize originalSize;
};

QString exportFilename(const QString &filename, const QString &format, qint64 saveId)
{
    QString name = QStringLiteral("%1").arg(saveId, 4, 10, QLatin1Char('0'));
    if (!filename.isEmpty())
        name += QLatin1Char('-') + QFileInfo(filename).completeBaseName();
    return name + QLatin1Char('.') + format;
}

// Decodes data scaled to size, halving repeatedly first so the
// bilinear step never exceeds a 2x ratio, as the reference does.
QImage decodeScaled(const QByteArray &data, const QSize &size)
{
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};

    QImageReader reader(&buffer);
    reader.setScaledSize(size);
    QImage image = reader.read();
    if (image.isNull() || image.size() == size)
        return image;

    int width = image.width();
    int height = image.height();
    while (width / 2 >= size.width() && height / 2 >= size.height()) {
        width /= 2;
        height /= 2;
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (image.size() != size)
        image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

// Matches the reference's encode_thumbnail: the smaller of WebP
// (quality 80) and PNG, or nothing when the image is small enough, is
// unreadable, or the thumbnail would not be smaller than the original.
std::optional<Thumbnail> encodeThumbnail(const QByteArray &imageData)
{
    QSize originalSize;
    {
        QBuffer buffer;
        buffer.setData(imageData);
        if (!buffer.open(QIODevice::ReadOnly))
            return std::nullopt;
        QImageReader reader(&buffer);
        originalSize = reader.size();
    }
    if (!originalSize.isValid() || originalSize.isEmpty())
        return std::nullopt;

    const int maxSide = std::max(originalSize.width(), originalSize.height());
    if (maxSide <= kFloorLevelSize)
        return std::nullopt;

    const double fraction = static_cast<double>(kFloorLevelSize) / maxSide;
    const QSize target(std::max(1, qRound(originalSize.width() * fraction)),
                       std::max(1, qRound(originalSize.height() * fraction)));
    const QImage image = decodeScaled(imageData, target);
    if (image.isNull())
        return std::nullopt;

    struct Candidate
    {
        QByteArray data;
        QString format;
    };
    QVector<Candidate> candidates;
    for (const auto &option : {std::pair<const char *, int>{"WEBP", 80},
                               std::pair<const char *, int>{"PNG", -1}}) {
        QByteArray encoded;
        QBuffer output(&encoded);
        if (!output.open(QIODevice::WriteOnly))
            continue;
        QImageWriter writer(&output, option.first);
        if (option.second >= 0)
            writer.setQuality(option.second);
        if (writer.write(image))
            candidates.append({encoded, QString::fromLatin1(option.first).toLower()});
    }
    if (candidates.isEmpty())
        return std::nullopt;

    const Candidate &best = *std::min_element(
        candidates.cbegin(), candidates.cend(),
        [](const Candidate &left, const Candidate &right) {
            return left.data.size() < right.data.size();
        });
    if (best.data.size() >= imageData.size())
        return std::nullopt;
    return Thumbnail{best.data, best.format, originalSize};
}

bool reusableFloor(const Record &record)
{
    if (record.floorData.isEmpty() || record.floorFraction <= 0 || record.floorFraction > 1)
        return false;
    if (record.origW <= 0 || record.origH <= 0)
        return false;

    QBuffer buffer;
    buffer.setData(record.floorData);
    if (!buffer.open(QIODevice::ReadOnly))
        return false;
    QImageReader reader(&buffer);
    return reader.canRead();
}

Status insertItem(Connection &db, const Record &record, qint64 id, bool legacy)
{
    const QString sql = legacy
        ? QStringLiteral("INSERT INTO items (id, type, x, y, z, scale, rotation, flip, data) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")
        : QStringLiteral(
              "INSERT INTO items (id, type, x, y, z, scale, rotation, flip, data, meta, uuid) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    auto statement = db.prepare(sql);
    if (!statement)
        return statement.error();

    Statement &stmt = statement.value();
    if (Status status = stmt.bind(1, id); !status)
        return status;
    if (Status status = stmt.bind(2, record.type); !status)
        return status;
    if (Status status = stmt.bind(3, record.x); !status)
        return status;
    if (Status status = stmt.bind(4, record.y); !status)
        return status;
    if (Status status = stmt.bind(5, record.z); !status)
        return status;
    if (Status status = stmt.bind(6, record.scale); !status)
        return status;
    if (Status status = stmt.bind(7, record.rotation); !status)
        return status;
    if (Status status = stmt.bind(8, static_cast<qint64>(record.flip)); !status)
        return status;
    if (Status status = stmt.bind(9, record.dataJson); !status)
        return status;
    if (legacy)
        return stmt.exec();
    if (Status status = stmt.bind(10, record.metaJson); !status)
        return status;
    if (Status status = stmt.bind(11, record.uuid); !status)
        return status;
    return stmt.exec();
}

Status insertBlob(Connection &db, const Record &record, qint64 id, const QByteArray &pixmap)
{
    const QString format = record.format.isEmpty() ? QStringLiteral("png") : record.format;
    const QString name = exportFilename(record.filename, format, id);

    auto statement = db.prepare(QStringLiteral(
        "INSERT INTO sqlar (item_id, name, mode, sz, data) VALUES (?, ?, ?, ?, ?)"));
    if (!statement)
        return statement.error();

    Statement &stmt = statement.value();
    if (Status status = stmt.bind(1, id); !status)
        return status;
    if (Status status = stmt.bind(2, name); !status)
        return status;
    if (Status status = stmt.bind(3, qint64(0644)); !status)
        return status;
    if (Status status = stmt.bind(4, qint64(pixmap.size())); !status)
        return status;
    // Bound by reference; pixmap outlives the statement execution.
    if (Status status = stmt.bind(5, pixmap); !status)
        return status;
    return stmt.exec();
}

Status insertFloor(Connection &db, qint64 id, double fraction, const QString &format, int origW,
                   int origH, const QByteArray &data)
{
    auto statement = db.prepare(QStringLiteral(
        "INSERT INTO lod (item_id, fraction, format, orig_w, orig_h, sz, data) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    if (!statement)
        return statement.error();

    Statement &stmt = statement.value();
    if (Status status = stmt.bind(1, id); !status)
        return status;
    if (Status status = stmt.bind(2, fraction); !status)
        return status;
    if (Status status = stmt.bind(3, format); !status)
        return status;
    if (Status status = stmt.bind(4, qint64(origW)); !status)
        return status;
    if (Status status = stmt.bind(5, qint64(origH)); !status)
        return status;
    if (Status status = stmt.bind(6, qint64(data.size())); !status)
        return status;
    if (Status status = stmt.bind(7, data); !status)
        return status;
    return stmt.exec();
}

Status writeThumbnail(Connection &db, qint64 id, const Record &record, const QByteArray &pixmap)
{
    if (reusableFloor(record)) {
        const QString format = record.floorFormat.isEmpty() ? QStringLiteral("png")
                                                            : record.floorFormat;
        return insertFloor(db, id, record.floorFraction, format, record.origW, record.origH,
                           record.floorData);
    }

    const auto thumbnail = encodeThumbnail(pixmap);
    if (!thumbnail)
        return Status::ok();

    // Backfill: the row is replaced when a thumbnail already exists.
    if (Status status = db.exec(QStringLiteral("DELETE FROM lod WHERE item_id=%1").arg(id));
        !status)
        return status;

    const int maxSide =
        std::max(thumbnail->originalSize.width(), thumbnail->originalSize.height());
    if (maxSide <= 0)
        return Status::ok();
    return insertFloor(db, id, static_cast<double>(kFloorLevelSize) / maxSide,
                       thumbnail->format, thumbnail->originalSize.width(),
                       thumbnail->originalSize.height(), thumbnail->data);
}

Status writeAll(Connection &db, const QVector<Record> &records, bool storeThumbnails,
                const Progress &progress, QVector<qint64> *assignedIds, Format format)
{
    const bool legacy = format == Format::Bee;
    if (Status status = db.exec(QStringLiteral("PRAGMA foreign_keys=ON")); !status)
        return status;
    if (legacy) {
        if (Status status = schema::createBeeTables(db); !status)
            return status;
        if (Status status =
                schema::writeHeader(db, schema::kBeeUserVersion, schema::kBeeApplicationId);
            !status)
            return status;
    } else {
        if (Status status = schema::createTables(db); !status)
            return status;
        if (Status status = schema::writeHeader(db); !status)
            return status;
    }

    auto transaction = Transaction::begin(db);
    if (!transaction)
        return transaction.error();

    // New ids must start above every id the records carry, so pasted
    // items (no id) do not collide with saved ones.
    qint64 nextId = 1;
    for (const Record &record : records) {
        if (record.saveId >= nextId)
            nextId = record.saveId + 1;
    }

    for (int i = 0; i < records.size(); ++i) {
        if (progress)
            progress(i, records.size());

        const Record &record = records.at(i);
        const qint64 id = record.saveId > 0 ? record.saveId : nextId++;
        if (assignedIds)
            assignedIds->append(id);

        if (Status status = insertItem(db, record, id, legacy); !status)
            return status;
        if (record.type != QLatin1String("pixmap"))
            continue;

        QByteArray pixmap = record.pixmap;
        if (pixmap.isEmpty() && record.pixmapSource)
            pixmap = record.pixmapSource();
        if (pixmap.isEmpty())
            continue;

        if (Status status = insertBlob(db, record, id, pixmap); !status)
            return status;
        // The legacy format stores no thumbnails.
        if (legacy || !storeThumbnails)
            continue;
        if (Status status = writeThumbnail(db, id, record, pixmap); !status)
            return status;
    }
    if (progress)
        progress(records.size(), records.size());
    return transaction.value().commit();
}

bool renameOverwrite(const QString &from, const QString &to)
{
#if defined(Q_OS_WIN)
    return ::MoveFileExW(reinterpret_cast<const wchar_t *>(from.utf16()),
                         reinterpret_cast<const wchar_t *>(to.utf16()),
                         MOVEFILE_REPLACE_EXISTING)
        != 0;
#else
    return ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}

} // namespace

Status save(const QString &path, const QVector<Record> &records, bool storeThumbnails,
            const Progress &progress, QVector<qint64> *assignedIds, Format format)
{
    const QFileInfo target(path);
    const QDir dir(target.absolutePath());
    if (!dir.exists())
        return Error{0, QStringLiteral("Directory does not exist"), target.absolutePath()};

    QTemporaryFile probe(dir.filePath(QStringLiteral(".beex-XXXXXX.tmp")));
    probe.setAutoRemove(false);
    if (!probe.open()) {
        return Error{0,
                     QStringLiteral("Cannot create temporary file: %1").arg(probe.errorString()),
                     dir.absolutePath()};
    }
    const QString tempPath = probe.fileName();
    probe.close();
    if (!probe.remove()) {
        QFile::remove(tempPath);
        return Error{0, QStringLiteral("Cannot remove temporary file"), tempPath};
    }

    {
        auto db = Connection::open(tempPath, Connection::OpenMode::Create);
        if (!db) {
            QFile::remove(tempPath);
            return db.error();
        }
        if (Status status =
                writeAll(db.value(), records, storeThumbnails, progress, assignedIds, format);
            !status) {
            QFile::remove(tempPath);
            return status;
        }
    }

    if (!renameOverwrite(tempPath, path)) {
        QFile::remove(tempPath);
        return Error{0, QStringLiteral("Cannot replace target file"), path};
    }
    return Status::ok();
}

} // namespace board
