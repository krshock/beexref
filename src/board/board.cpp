#include "board.h"

#include "schema.h"
#include "util/process.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <utility>

namespace board {
namespace {

constexpr int kStaleTempDays = 7;

QString nextTempPath(const QString &dir)
{
    static std::atomic<quint64> counter{0};
    const qint64 pid = QCoreApplication::applicationPid();
    for (;;) {
        const QString name =
            QStringLiteral("open-%1-%2.beex").arg(pid).arg(counter.fetch_add(1) + 1);
        const QString path = QDir(dir).filePath(name);
        if (!QFile::exists(path))
            return path;
    }
}

Result<QString> copyToTemp(const QString &source, const QString &tempDir)
{
    if (!QDir().mkpath(tempDir))
        return Error{0, QStringLiteral("Cannot create directory"), tempDir};
    const QString tempPath = nextTempPath(tempDir);
    if (!QFile::copy(source, tempPath)) {
        QFile::remove(tempPath);
        return Error{0, QStringLiteral("Cannot copy board file"), source};
    }
    return tempPath;
}

Error notABoardError(const QString &path)
{
    return Error{0, QStringLiteral("Not a BeeXRef board file (no items table or image data)"), path};
}

} // namespace

Board::Board(Connection db, QString path, QString tempPath, Columns columns)
    : db_(std::move(db))
    , path_(std::move(path))
    , tempPath_(std::move(tempPath))
    , columns_(columns)
{
    recordFileIdentity();
}

void Board::recordFileIdentity()
{
    const QFileInfo info(path_);
    fileSize_ = info.exists() ? info.size() : -1;
    fileMtime_ = info.lastModified();

    dataVersion_ = -1;
    auto statement = db_.prepare(QStringLiteral("PRAGMA data_version"));
    if (!statement)
        return;
    auto row = statement.value().step();
    if (row && row.value())
        dataVersion_ = statement.value().columnInt64(0);
}

bool Board::hasChangedOnDisk()
{
    // A replaced, resized or retimed file is a change whatever SQLite
    // thinks.
    const QFileInfo info(path_);
    if (!info.exists() || info.size() != fileSize_ || info.lastModified() != fileMtime_)
        return true;

    // A commit from another connection bumps data_version; this
    // connection's own writes do not, so the app's own saves stay quiet.
    auto statement = db_.prepare(QStringLiteral("PRAGMA data_version"));
    if (!statement)
        return true; // cannot tell: treat it as changed
    auto row = statement.value().step();
    if (!row || !row.value())
        return true;
    return statement.value().columnInt64(0) != dataVersion_;
}

Board::~Board()
{
    close();
}

// Hand-written moves: every new member must be moved here too.
Board::Board(Board &&other) noexcept
    : db_(std::move(other.db_))
    , path_(std::move(other.path_))
    , tempPath_(std::move(other.tempPath_))
    , columns_(other.columns_)
    , newerVersion_(other.newerVersion_)
    , salvaged_(other.salvaged_)
    , dataVersion_(other.dataVersion_)
    , fileSize_(other.fileSize_)
    , fileMtime_(other.fileMtime_)
{
    other.tempPath_.clear();
}

Board &Board::operator=(Board &&other) noexcept
{
    if (this == &other)
        return *this;
    close();
    db_ = std::move(other.db_);
    path_ = std::move(other.path_);
    tempPath_ = std::move(other.tempPath_);
    columns_ = other.columns_;
    newerVersion_ = other.newerVersion_;
    salvaged_ = other.salvaged_;
    dataVersion_ = other.dataVersion_;
    fileSize_ = other.fileSize_;
    fileMtime_ = other.fileMtime_;
    other.tempPath_.clear();
    return *this;
}

void Board::close()
{
    db_ = {};
    if (!tempPath_.isEmpty()) {
        QFile::remove(tempPath_);
        tempPath_.clear();
    }
}

Result<Board> Board::open(const QString &path, const QString &tempDir)
{
    auto db = Connection::open(path, Connection::OpenMode::ReadOnly);
    if (!db)
        return db.error();

    auto hasItems = schema::hasItemsTable(db.value());
    if (!hasItems)
        return hasItems.error();
    if (!hasItems.value()) {
        // The item table is gone, but the blob store may still hold the
        // images: open such a file so the caller can recover them.
        auto hasBlobs = schema::hasTable(db.value(), QStringLiteral("sqlar"));
        if (!hasBlobs)
            return hasBlobs.error();
        if (!hasBlobs.value())
            return notABoardError(path);
        Columns columns;
        auto lod = schema::hasTable(db.value(), QStringLiteral("lod"));
        if (!lod)
            return lod.error();
        columns.lod = lod.value();
        Board board(db.take(), path, QString(), columns);
        board.salvaged_ = true;
        return board;
    }

    auto version = schema::readUserVersion(db.value());
    if (!version)
        return version.error();

    if (version.value() > schema::kUserVersion) {
        // A newer file still opens read-only: the scene is not lost, and
        // the caller marks it so a save never downgrades it in place.
        auto columns = detectColumns(db.value());
        if (!columns)
            return columns.error();
        Board board(db.take(), path, QString(), columns.take());
        board.newerVersion_ = true;
        return board;
    }

    if (version.value() < schema::kUserVersion)
        return prepare(path, tempDir);

    auto columns = detectColumns(db.value());
    if (!columns)
        return columns.error();
    return Board(db.take(), path, QString(), columns.take());
}

Result<Board> Board::prepare(const QString &path, const QString &tempDir)
{
    auto tempPath = copyToTemp(path, tempDir);
    if (!tempPath)
        return tempPath.error();
    const QString temp = tempPath.value();

    {
        auto writeDb = Connection::open(temp, Connection::OpenMode::ReadWrite);
        if (!writeDb) {
            QFile::remove(temp);
            return writeDb.error();
        }
        if (Status status = schema::migrateToCurrent(writeDb.value()); !status) {
            QFile::remove(temp);
            return status.error();
        }
    }

    auto db = Connection::open(temp, Connection::OpenMode::ReadOnly);
    if (!db) {
        QFile::remove(temp);
        return db.error();
    }
    auto columns = detectColumns(db.value());
    if (!columns) {
        QFile::remove(temp);
        return columns.error();
    }
    return Board(db.take(), path, temp, columns.take());
}

Result<Columns> detectColumns(Connection &db)
{
    Columns columns;

    auto tableInfo = db.prepare(QStringLiteral("PRAGMA table_info(items)"));
    if (!tableInfo)
        return tableInfo.error();
    while (true) {
        auto row = tableInfo.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        const QString name = tableInfo.value().columnText(1);
        if (name == QLatin1String("meta"))
            columns.meta = true;
        else if (name == QLatin1String("uuid"))
            columns.uuid = true;
    }

    auto lod = db.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='lod'"));
    if (!lod)
        return lod.error();
    auto row = lod.value().step();
    if (!row)
        return row.error();
    columns.lod = row.value();
    return columns;
}

Result<QVector<ItemRow>> Board::items()
{
    const QString metaExpr =
        columns_.meta ? QStringLiteral("COALESCE(meta, '')") : QStringLiteral("''");
    const QString uuidExpr =
        columns_.uuid ? QStringLiteral("COALESCE(uuid, '')") : QStringLiteral("''");
    const QString sql =
        QStringLiteral("SELECT id, type, x, y, z, scale, rotation, flip, "
                       "COALESCE(data, ''), %1, %2 FROM items ORDER BY id")
            .arg(metaExpr, uuidExpr);

    auto statement = db_.prepare(sql);
    if (!statement)
        return statement.error();

    QVector<ItemRow> rows;
    while (true) {
        auto row = statement.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;

        Statement &stmt = statement.value();
        ItemRow item;
        item.id = stmt.columnInt64(0);
        item.type = stmt.columnText(1);
        item.x = stmt.columnDouble(2);
        item.y = stmt.columnDouble(3);
        item.z = stmt.columnDouble(4);
        item.scale = stmt.columnDouble(5);
        item.rotation = stmt.columnDouble(6);
        // The app treats "1" as normal and anything else as flipped;
        // a 0 would otherwise zero the item's width.
        item.flip = stmt.columnInt64(7) == 1 ? 1 : -1;
        item.data = stmt.columnText(8);
        item.meta = stmt.columnText(9);
        item.uuid = stmt.columnText(10);
        rows.append(std::move(item));
    }
    return rows;
}

Result<QVector<BlobRow>> Board::blobRows()
{
    auto statement = db_.prepare(QStringLiteral("SELECT item_id, name FROM sqlar"));
    if (!statement)
        return statement.error();
    QVector<BlobRow> rows;
    while (true) {
        auto row = statement.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        BlobRow blob;
        blob.itemId = statement.value().columnInt64(0);
        blob.name = statement.value().columnText(1);
        rows.append(std::move(blob));
    }
    return rows;
}

Result<QVector<ItemRow>> Board::salvageItems()
{
    auto blobs = blobRows();
    if (!blobs)
        return blobs.error();
    QVector<ItemRow> rows;
    rows.reserve(blobs.value().size());
    for (const BlobRow &blob : blobs.value()) {
        ItemRow row;
        row.id = blob.itemId;
        row.type = QStringLiteral("pixmap");
        row.flip = 1;
        // The blob store carries only the name: the position and the rest
        // of the item state were in the lost item table.
        QJsonObject data;
        data.insert(QStringLiteral("filename"), QFileInfo(blob.name).fileName());
        row.data = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
        rows.append(std::move(row));
    }
    salvaged_ = true;
    return rows;
}

Result<QByteArray> Board::blob(qint64 itemId)
{
    auto statement = db_.prepare(QStringLiteral("SELECT data FROM sqlar WHERE item_id=?"));
    if (!statement)
        return statement.error();
    if (Status status = statement.value().bind(1, itemId); !status)
        return status.error();

    auto row = statement.value().step();
    if (!row)
        return row.error();
    if (!row.value()) {
        return Error{0,
                     QStringLiteral("No image data for item %1").arg(itemId),
                     db_.path()};
    }
    return statement.value().columnBlob(0);
}

Result<QString> Board::blobFormat(qint64 itemId)
{
    auto statement = db_.prepare(QStringLiteral("SELECT name FROM sqlar WHERE item_id=?"));
    if (!statement)
        return statement.error();
    if (Status status = statement.value().bind(1, itemId); !status)
        return status.error();

    auto row = statement.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return QString();
    const QString name = statement.value().columnText(0);
    return QFileInfo(name).suffix().toLower();
}

Result<QSet<qint64>> Board::blobIds()
{
    QSet<qint64> ids;
    auto statement = db_.prepare(QStringLiteral("SELECT item_id FROM sqlar"));
    if (!statement)
        return statement.error();
    while (true) {
        auto row = statement.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        ids.insert(statement.value().columnInt64(0));
    }
    return ids;
}

Result<qint64> Board::orphanedFloorCount()
{
    if (!columns_.lod)
        return qint64(0);
    auto statement = db_.prepare(
        QStringLiteral("SELECT count(*) FROM lod WHERE item_id NOT IN (SELECT id FROM items)"));
    if (!statement)
        return statement.error();
    auto row = statement.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return Error{0, QStringLiteral("Floor count returned no row"), path_};
    return statement.value().columnInt64(0);
}

Result<QHash<qint64, QSize>> Board::originalSizes()
{
    QHash<qint64, QSize> sizes;
    if (!columns_.lod)
        return sizes;

    auto statement = db_.prepare(QStringLiteral(
        "SELECT item_id, orig_w, orig_h FROM lod WHERE orig_w > 0 AND orig_h > 0"));
    if (!statement)
        return statement.error();

    while (true) {
        auto row = statement.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        sizes.insert(statement.value().columnInt64(0),
                     QSize(static_cast<int>(statement.value().columnInt64(1)),
                           static_cast<int>(statement.value().columnInt64(2))));
    }
    return sizes;
}

Result<QHash<qint64, FloorLevel>> Board::floorLevels()
{
    QHash<qint64, FloorLevel> levels;
    if (!columns_.lod)
        return levels;

    auto statement = db_.prepare(QStringLiteral(
        "SELECT l.item_id, l.fraction, COALESCE(l.format, ''), l.data FROM lod l "
        "JOIN (SELECT item_id, MIN(fraction) AS f FROM lod GROUP BY item_id) m "
        "  ON m.item_id = l.item_id AND m.f = l.fraction"));
    if (!statement)
        return statement.error();

    while (true) {
        auto row = statement.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        FloorLevel level;
        level.itemId = statement.value().columnInt64(0);
        level.fraction = statement.value().columnDouble(1);
        level.format = statement.value().columnText(2);
        level.data = statement.value().columnBlob(3);
        levels.insert(level.itemId, std::move(level));
    }
    return levels;
}

Result<QHash<QString, qint64>> Board::counts()
{
    return board::counts(db_);
}

Result<QHash<QString, qint64>> counts(Connection &db)
{
    QHash<QString, qint64> result;
    for (const QString &table : {QStringLiteral("items"), QStringLiteral("sqlar"),
                                 QStringLiteral("lod")}) {
        auto statement = db.prepare(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table));
        if (!statement) {
            result.insert(table, -1);
            continue;
        }
        auto row = statement.value().step();
        if (!row || !row.value()) {
            result.insert(table, -1);
            continue;
        }
        result.insert(table, statement.value().columnInt64(0));
    }
    return result;
}

void sweepStaleTempFiles(const QString &tempDir)
{
    const QDir dir(tempDir);
    const QStringList names =
        dir.entryList({QStringLiteral("open-*.beex")}, QDir::Files, QDir::Name);
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &name : names) {
        const QStringList parts = name.split(QLatin1Char('-'));
        bool parsed = false;
        const qint64 pid = parts.size() > 1 ? parts.at(1).toLongLong(&parsed) : 0;
        const bool old =
            QFileInfo(dir.filePath(name)).lastModified().daysTo(now) > kStaleTempDays;
        if (!parsed || !util::isProcessAlive(pid) || old)
            QFile::remove(dir.filePath(name));
    }
}

} // namespace board
