#include "board.h"

#include "schema.h"
#include "util/process.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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
    return Error{0, QStringLiteral("Not a BeeXRef board file (no items table)"), path};
}

Error newerVersionError(int version, const QString &path)
{
    return Error{0,
                 QStringLiteral("File format version %1 is newer than supported (%2)")
                     .arg(version)
                     .arg(schema::kUserVersion),
                 path};
}

} // namespace

Board::Board(Connection db, QString path, QString tempPath, Columns columns)
    : db_(std::move(db))
    , path_(std::move(path))
    , tempPath_(std::move(tempPath))
    , columns_(columns)
{
}

Board::~Board()
{
    close();
}

Board::Board(Board &&other) noexcept
    : db_(std::move(other.db_))
    , path_(std::move(other.path_))
    , tempPath_(std::move(other.tempPath_))
    , columns_(other.columns_)
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
    if (!hasItems.value())
        return notABoardError(path);

    auto version = schema::readUserVersion(db.value());
    if (!version)
        return version.error();

    if (version.value() > schema::kUserVersion)
        return newerVersionError(version.value(), path);

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
        // The reference treats "1" as normal and anything else as
        // flipped (its loader calls do_flip() when the stored value
        // differs from 1); a 0 would otherwise zero the item's width.
        item.flip = stmt.columnInt64(7) == 1 ? 1 : -1;
        item.data = stmt.columnText(8);
        item.meta = stmt.columnText(9);
        item.uuid = stmt.columnText(10);
        rows.append(std::move(item));
    }
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
