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
#include <QStringList>
#include <QTemporaryFile>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>

#if defined(Q_OS_WIN)
// This file uses std::max below; without NOMINMAX the windows.h macros
// would rewrite it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
// bilinear step never exceeds a 2x ratio.
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

// Thumbnail rule: the smaller of WebP
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

// "2 images could not be read (ids 12, 47); nothing was written". The id
// list is capped so a badly broken board cannot flood the dialog.
QString missingImagesMessage(const QVector<qint64> &ids)
{
    constexpr int kMaxShown = 8;
    QStringList shown;
    for (int i = 0; i < ids.size() && i < kMaxShown; ++i)
        shown.append(QString::number(ids.at(i)));
    QString list = shown.join(QStringLiteral(", "));
    if (ids.size() > shown.size())
        list += QStringLiteral(", ...");
    return QStringLiteral("%1 %2 could not be read (%3 %4); nothing was written")
        .arg(ids.size())
        .arg(ids.size() == 1 ? QStringLiteral("image") : QStringLiteral("images"))
        .arg(ids.size() == 1 ? QStringLiteral("id") : QStringLiteral("ids"))
        .arg(list);
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

    QVector<qint64> missingBytes;

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
        if (pixmap.isEmpty()) {
            // An image row is never written without its image, unless it
            // is an explicit placeholder from a recovered board; the row
            // data carries the mark. The .bee interchange stays strict.
            if (!record.placeholder || legacy)
                missingBytes.append(id);
            continue;
        }

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
    if (!missingBytes.isEmpty())
        return Error{0, missingImagesMessage(missingBytes), {}};
    return transaction.value().commit();
}

#if defined(Q_OS_WIN)
// Replacing a file can fail while antivirus, the indexer or a sync
// client holds the target open for a moment. Retry the transient errors
// with a short backoff: six attempts over about 1.5 s, long enough for
// the usual holds and short enough not to feel hung. A directory target
// or a missing path fails immediately, so a real error is not delayed.
bool replaceWithRetry(const QString &from, const QString &to)
{
    constexpr int kAttempts = 6;
    int delayMs = 50;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (::MoveFileExW(reinterpret_cast<const wchar_t *>(from.utf16()),
                          reinterpret_cast<const wchar_t *>(to.utf16()),
                          MOVEFILE_REPLACE_EXISTING)
            != 0) {
            return true;
        }
        const DWORD error = ::GetLastError();
        const bool transient = error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION
            || error == ERROR_ACCESS_DENIED;
        if (!transient || attempt + 1 == kAttempts)
            return false;
        ::Sleep(static_cast<DWORD>(delayMs));
        delayMs *= 2;
    }
    return false;
}
#endif

bool renameOverwrite(const QString &from, const QString &to)
{
#if defined(Q_OS_WIN)
    return replaceWithRetry(from, to);
#else
    return ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}

// SQLite reports a full disk as "database or disk is full" or "disk I/O
// error"; name the cause in the user's terms.
Error friendlyWriteError(const Error &error)
{
    const QString message = error.message.toLower();
    if (message.contains(QStringLiteral("disk")) || message.contains(QStringLiteral("full"))
        || message.contains(QStringLiteral("no space"))) {
        return Error{error.code,
                     QStringLiteral("Not enough disk space to write the board (%1)")
                         .arg(error.message),
                     error.path};
    }
    return error;
}

// One scalar from a query that must return exactly one row.
Result<qint64> scalar(Connection &db, const QString &sql)
{
    auto statement = db.prepare(sql);
    if (!statement)
        return statement.error();
    auto row = statement.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return Error{0, QStringLiteral("Verification query returned no row"), db.path()};
    return statement.value().columnInt64(0);
}

// One scalar from a query with one bound id.
Result<qint64> scalarForId(Connection &db, const QString &sql, qint64 id)
{
    auto statement = db.prepare(sql);
    if (!statement)
        return statement.error();
    if (Status status = statement.value().bind(1, id); !status)
        return status.error();
    auto row = statement.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return Error{0, QStringLiteral("Verification query returned no row"), db.path()};
    return statement.value().columnInt64(0);
}

// Deletes one item row; its blob and floors follow through the foreign
// keys, which the caller has enabled.
Status deleteItem(Connection &db, qint64 id)
{
    auto statement = db.prepare(QStringLiteral("DELETE FROM items WHERE id=?"));
    if (!statement)
        return statement.error();
    if (Status status = statement.value().bind(1, id); !status)
        return status.error();
    if (Status status = statement.value().exec(); !status)
        return status;
    if (db.changes() != 1) {
        return Error{0,
                     QStringLiteral("Cannot delete item %1: the row is missing").arg(id),
                     db.path()};
    }
    return Status::ok();
}

// Updates one existing row. The encoded blob never changes (sources are
// immutable), so only the row is written; a row that is gone is an error
// rather than a silent skip.
Status updateItem(Connection &db, const Record &record)
{
    auto statement = db.prepare(QStringLiteral(
        "UPDATE items SET type=?, x=?, y=?, z=?, scale=?, rotation=?, flip=?, data=?, "
        "meta=?, uuid=? WHERE id=?"));
    if (!statement)
        return statement.error();
    Statement &stmt = statement.value();
    if (Status status = stmt.bind(1, record.type); !status)
        return status;
    if (Status status = stmt.bind(2, record.x); !status)
        return status;
    if (Status status = stmt.bind(3, record.y); !status)
        return status;
    if (Status status = stmt.bind(4, record.z); !status)
        return status;
    if (Status status = stmt.bind(5, record.scale); !status)
        return status;
    if (Status status = stmt.bind(6, record.rotation); !status)
        return status;
    if (Status status = stmt.bind(7, record.flip); !status)
        return status;
    if (Status status = stmt.bind(8, record.dataJson); !status)
        return status;
    if (Status status = stmt.bind(9, record.metaJson); !status)
        return status;
    if (Status status = stmt.bind(10, record.uuid); !status)
        return status;
    if (Status status = stmt.bind(11, record.saveId); !status)
        return status;
    if (Status status = stmt.exec(); !status)
        return status;
    if (db.changes() != 1) {
        return Error{0,
                     QStringLiteral("Cannot update item %1: the row is missing")
                         .arg(record.saveId),
                     db.path()};
    }
    return Status::ok();
}

// What an update should leave behind, for the post-write check.
struct UpdateCounts
{
    qint64 itemsBefore = 0;
    qint64 blobsBefore = 0;
    qint64 itemsAdded = 0;
    qint64 itemsRemoved = 0;
    qint64 blobsAdded = 0;
    qint64 blobsRemoved = 0;
};

// Checks the file an update is about to commit: the row counts must be
// exactly what the change set promised, no floor may be orphaned, and the
// header must still be the one the reader expects. A mismatch fails the
// transaction, so the file keeps its previous content.
Status verifyUpdated(Connection &db, const UpdateCounts &counts)
{
    const auto items = scalar(db, QStringLiteral("SELECT count(*) FROM items"));
    if (!items)
        return items.error();
    const qint64 expectedItems = counts.itemsBefore + counts.itemsAdded - counts.itemsRemoved;
    if (items.value() != expectedItems) {
        return Error{0,
                     QStringLiteral("Update verification failed: %1 item rows, expected %2")
                         .arg(items.value())
                         .arg(expectedItems),
                     db.path()};
    }

    const auto blobs = scalar(db, QStringLiteral("SELECT count(*) FROM sqlar"));
    if (!blobs)
        return blobs.error();
    const qint64 expectedBlobs = counts.blobsBefore + counts.blobsAdded - counts.blobsRemoved;
    if (blobs.value() != expectedBlobs) {
        return Error{0,
                     QStringLiteral("Update verification failed: %1 image blobs, expected %2")
                         .arg(blobs.value())
                         .arg(expectedBlobs),
                     db.path()};
    }

    const auto orphanFloors =
        scalar(db, QStringLiteral("SELECT count(*) FROM lod WHERE item_id NOT IN (SELECT id FROM "
                                  "items)"));
    if (!orphanFloors)
        return orphanFloors.error();
    if (orphanFloors.value() != 0) {
        return Error{0,
                     QStringLiteral("Update verification failed: %1 orphaned floor rows")
                         .arg(orphanFloors.value()),
                     db.path()};
    }

    const auto version = scalar(db, QStringLiteral("PRAGMA user_version"));
    if (!version)
        return version.error();
    if (version.value() != schema::kUserVersion) {
        return Error{0,
                     QStringLiteral("Update verification failed: user_version %1")
                         .arg(version.value()),
                     db.path()};
    }
    return Status::ok();
}

} // namespace

Status verifyWritten(Connection &db, const QVector<Record> &records, Format format)
{
    const bool legacy = format == Format::Bee;

    const auto itemRows = scalar(db, QStringLiteral("SELECT count(*) FROM items"));
    if (!itemRows)
        return itemRows.error();
    if (itemRows.value() != records.size()) {
        return Error{0,
                     QStringLiteral("Verification failed: %1 item rows for %2 records")
                         .arg(itemRows.value())
                         .arg(records.size()),
                     db.path()};
    }

    qint64 expectedBlobs = 0;
    for (const Record &record : records) {
        if (record.type == QLatin1String("pixmap") && !record.placeholder)
            ++expectedBlobs;
    }
    const auto blobRows = scalar(db, QStringLiteral("SELECT count(*) FROM sqlar"));
    if (!blobRows)
        return blobRows.error();
    if (blobRows.value() != expectedBlobs) {
        return Error{0,
                     QStringLiteral("Verification failed: %1 image blobs for %2 images")
                         .arg(blobRows.value())
                         .arg(expectedBlobs),
                     db.path()};
    }

    if (!legacy) {
        const auto orphanFloors = scalar(
            db,
            QStringLiteral("SELECT count(*) FROM lod WHERE item_id NOT IN (SELECT id FROM items)"));
        if (!orphanFloors)
            return orphanFloors.error();
        if (orphanFloors.value() != 0) {
            return Error{0,
                         QStringLiteral("Verification failed: %1 orphaned floor rows")
                             .arg(orphanFloors.value()),
                         db.path()};
        }
    }

    const int expectedVersion = legacy ? schema::kBeeUserVersion : schema::kUserVersion;
    const auto version = scalar(db, QStringLiteral("PRAGMA user_version"));
    if (!version)
        return version.error();
    if (version.value() != expectedVersion) {
        return Error{0,
                     QStringLiteral("Verification failed: user_version %1, expected %2")
                         .arg(version.value())
                         .arg(expectedVersion),
                     db.path()};
    }

    const int expectedApp = legacy ? schema::kBeeApplicationId : schema::kApplicationId;
    const auto appId = scalar(db, QStringLiteral("PRAGMA application_id"));
    if (!appId)
        return appId.error();
    if (appId.value() != expectedApp) {
        return Error{0,
                     QStringLiteral("Verification failed: application_id %1, expected %2")
                         .arg(appId.value())
                         .arg(expectedApp),
                     db.path()};
    }
    return Status::ok();
}

Result<QString> writeTemp(const QString &path, const QVector<Record> &records,
                         bool storeThumbnails, const Progress &progress,
                         QVector<qint64> *assignedIds, Format format)
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
            // Windows cannot remove an open file: release the connection
            // before dropping the temp.
            db.value().close();
            QFile::remove(tempPath);
            return friendlyWriteError(status.error());
        }
        // The temp file must match what the records promised before it
        // replaces the target; a mismatch means the target keeps its
        // previous content.
        if (Status status = verifyWritten(db.value(), records, format); !status) {
            db.value().close();
            QFile::remove(tempPath);
            return status.error();
        }
    }
    return tempPath;
}

Status replaceTemp(const QString &tempPath, const QString &path)
{
    if (!renameOverwrite(tempPath, path)) {
        // The complete new file is kept: the target could not be
        // replaced, so the temp is the only copy of this save.
        return Error{0,
                     QStringLiteral("Cannot replace the target file; the new version is kept "
                                    "at %1")
                         .arg(tempPath),
                     path};
    }
    return Status::ok();
}

Status save(const QString &path, const QVector<Record> &records, bool storeThumbnails,
            const Progress &progress, QVector<qint64> *assignedIds, Format format)
{
    auto temp = writeTemp(path, records, storeThumbnails, progress, assignedIds, format);
    if (!temp)
        return temp.error();
    return replaceTemp(temp.value(), path);
}

Status update(const QString &path, const QVector<Record> &changed, const QVector<Record> &added,
              const QVector<qint64> &removedIds, qint64 firstId, bool storeThumbnails,
              const Progress &progress, QVector<qint64> *assignedIds)
{
    if (changed.isEmpty() && added.isEmpty() && removedIds.isEmpty())
        return Status::ok();

    auto db = Connection::open(path, Connection::OpenMode::ReadWrite);
    if (!db)
        return db.error();
    Connection &connection = db.value();

    // Only a file that already is the current native shape may be
    // updated; anything else goes through save() (temp file + rename).
    const auto version = scalar(connection, QStringLiteral("PRAGMA user_version"));
    if (!version)
        return version.error();
    if (version.value() != schema::kUserVersion) {
        return Error{0,
                     QStringLiteral("Cannot update in place: file version %1 is not current (%2)")
                         .arg(version.value())
                         .arg(schema::kUserVersion),
                     path};
    }

    // Where new ids start. Derived from every table that carries one,
    // before any delete, so a freed id is not handed out again in the
    // same update.
    qint64 nextId = firstId;
    if (nextId <= 0) {
        const auto maxId = scalar(
            connection,
            QStringLiteral("SELECT COALESCE(MAX(m), 0) FROM (SELECT MAX(id) AS m FROM items "
                           "UNION ALL SELECT MAX(item_id) AS m FROM sqlar "
                           "UNION ALL SELECT MAX(item_id) AS m FROM lod)"));
        if (!maxId)
            return maxId.error();
        nextId = maxId.value() + 1;
    }

    if (Status status = connection.exec(QStringLiteral("PRAGMA foreign_keys=ON")); !status)
        return status;

    // What the file holds before the transaction, for the post-write
    // check that runs just before the commit.
    UpdateCounts counts;
    {
        const auto items = scalar(connection, QStringLiteral("SELECT count(*) FROM items"));
        if (!items)
            return items.error();
        counts.itemsBefore = items.value();
        const auto blobs = scalar(connection, QStringLiteral("SELECT count(*) FROM sqlar"));
        if (!blobs)
            return blobs.error();
        counts.blobsBefore = blobs.value();
    }

    auto transaction = Transaction::begin(connection);
    if (!transaction)
        return transaction.error();

    const int total = removedIds.size() + changed.size() + added.size();
    int done = 0;
    const auto report = [&progress, total](int count) {
        if (progress)
            progress(count, total);
    };

    for (qint64 id : removedIds) {
        const auto blobs =
            scalarForId(connection, QStringLiteral("SELECT count(*) FROM sqlar WHERE item_id=?"), id);
        if (!blobs)
            return blobs.error();
        counts.blobsRemoved += blobs.value();
        if (Status status = deleteItem(connection, id); !status)
            return status;
        counts.itemsRemoved += 1;
        report(++done);
    }

    for (const Record &record : changed) {
        if (Status status = updateItem(connection, record); !status)
            return status;
        report(++done);
    }

    for (const Record &record : added) {
        const qint64 id = record.saveId > 0 ? record.saveId : nextId++;
        if (assignedIds)
            assignedIds->append(id);
        if (Status status = insertItem(connection, record, id, false); !status)
            return status;
        counts.itemsAdded += 1;

        // Only pixmaps carry a blob; text and other types are just rows.
        if (record.type == QLatin1String("pixmap")) {
            QByteArray pixmap = record.pixmap;
            if (pixmap.isEmpty() && record.pixmapSource)
                pixmap = record.pixmapSource();
            if (pixmap.isEmpty()) {
                // A placeholder is an explicit, known-gone image; anything
                // else must not be written without its bytes.
                if (!record.placeholder)
                    return Error{0, missingImagesMessage({id}), {}};
            } else {
                if (Status status = insertBlob(connection, record, id, pixmap); !status)
                    return status;
                counts.blobsAdded += 1;
                if (storeThumbnails) {
                    if (Status status = writeThumbnail(connection, id, record, pixmap); !status)
                        return status;
                }
            }
        }
        report(++done);
    }

    // The counts must be exactly what the change set promised; a mismatch
    // rolls the transaction back and the file keeps its content.
    if (Status status = verifyUpdated(connection, counts); !status)
        return status;

    return transaction.value().commit();
}

} // namespace board
