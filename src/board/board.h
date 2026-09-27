#pragma once

#include "error.h"
#include "sqlite.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>

namespace board {

struct ItemRow
{
    qint64 id = 0;
    QString type;
    double x = 0;
    double y = 0;
    double z = 0;
    double scale = 1;
    double rotation = 0;
    qint64 flip = 1;
    QString data;
    QString meta;
    QString uuid;
};

// The coarsest saved level of an item: the thumbnail the app uses as
// its floor level instead of decoding the original.
struct FloorLevel
{
    qint64 itemId = 0;
    double fraction = 0;
    QString format;
    QByteArray data;
};

// One image in the blob store, for salvaging a board whose item rows are
// gone: the blob store keeps the image and the name it was saved under.
struct BlobRow
{
    qint64 itemId = 0;
    QString name;
};

// Which optional columns and tables the file has; files written before
// them are still readable.
struct Columns
{
    bool meta = false;
    bool uuid = false;
    bool lod = false;
};

// A board opened for reading. A file at the current version is opened
// read-only in place; an older file is copied to a temporary file,
// migrated there and opened read-only, so the original is never
// written to. The copy is removed on close.
class Board
{
public:
    Board() = default;
    ~Board();
    Board(const Board &) = delete;
    Board &operator=(const Board &) = delete;
    Board(Board &&other) noexcept;
    Board &operator=(Board &&other) noexcept;

    // Opens path for the app. Older files are migrated on a temp copy.
    // A newer file, and a file whose item table is gone but whose blob
    // store survives, still open read-only and are flagged
    // (isNewerVersion()/isSalvaged()) so the caller can recover them;
    // nothing is written either way. A file with neither an items table
    // nor blobs fails.
    static Result<Board> open(const QString &path, const QString &tempDir);

    // Copies path to tempDir, migrates the copy and opens it read-only.
    // Used by the format gate.
    static Result<Board> prepare(const QString &path, const QString &tempDir);

    bool isOpen() const { return db_.isOpen(); }
    const QString &path() const { return path_; }
    // Empty when the file was opened in place.
    const QString &tempPath() const { return tempPath_; }
    Connection &connection() { return db_; }
    const Columns &columns() const { return columns_; }

    // The file was written by a newer version of the app. It is opened
    // read-only and the caller marks it, so a save never downgrades it in
    // place.
    bool isNewerVersion() const { return newerVersion_; }
    // The item table was missing or unreadable and the caller recovered
    // the images from the blob store.
    bool isSalvaged() const { return salvaged_; }

    Result<QVector<ItemRow>> items();
    // The images in the blob store, and item rows synthesized from them,
    // for a board whose item table cannot be read. The rows carry the
    // filename from the blob's name and default geometry: the position is
    // lost with the item table.
    Result<QVector<BlobRow>> blobRows();
    Result<QVector<ItemRow>> salvageItems();
    Result<QByteArray> blob(qint64 itemId);
    // The encoded blob's format, from the sqlar name's extension. Empty
    // when the item has no blob.
    Result<QString> blobFormat(qint64 itemId);
    // The item ids that have an encoded blob, in one query, so a load
    // can spot rows whose image is gone.
    Result<QSet<qint64>> blobIds();
    // Floor rows whose item is gone; 0 when the file has no lod table.
    Result<qint64> orphanedFloorCount();
    Result<QHash<qint64, QSize>> originalSizes();
    Result<QHash<qint64, FloorLevel>> floorLevels();
    Result<QHash<QString, qint64>> counts();

    // Closes the database and removes the migrated copy, if any.
    void close();

private:
    Board(Connection db, QString path, QString tempPath, Columns columns);

    Connection db_;
    QString path_;
    QString tempPath_;
    Columns columns_;
    bool newerVersion_ = false;
    bool salvaged_ = false;
};

Result<Columns> detectColumns(Connection &db);

// Row counts for the items, sqlar and lod tables; -1 for missing tables.
Result<QHash<QString, qint64>> counts(Connection &db);

// Removes open-* copies in tempDir whose creating process is gone (or
// which are older than a week).
void sweepStaleTempFiles(const QString &tempDir);

} // namespace board
