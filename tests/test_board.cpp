#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <thread>

#include "board/board.h"
#include "board/schema.h"
#include "board/sqlite.h"

namespace {

const QByteArray kBlob("image-bytes");

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

QStringList dirEntries(const QString &path)
{
    return QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
}

// Creates a board with one pixmap item and matching sqlar row, plus a
// lod row when requested. Old versions get the older shape.
board::Status createBoard(const QString &path, int userVersion, bool withMeta, bool withUuid,
                          bool withLod, int flip = 1)
{
    auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
    if (!db)
        return db.error();
    board::Connection &d = db.value();

    QStringList itemColumns{QStringLiteral("id INTEGER PRIMARY KEY"),
                            QStringLiteral("type TEXT NOT NULL"),
                            QStringLiteral("x REAL DEFAULT 0"),
                            QStringLiteral("y REAL DEFAULT 0"),
                            QStringLiteral("z REAL DEFAULT 0"),
                            QStringLiteral("scale REAL DEFAULT 1"),
                            QStringLiteral("rotation REAL DEFAULT 0"),
                            QStringLiteral("flip INTEGER DEFAULT 1"),
                            QStringLiteral("data JSON")};
    if (withMeta)
        itemColumns << QStringLiteral("meta JSON");
    if (withUuid)
        itemColumns << QStringLiteral("uuid TEXT");
    if (board::Status status =
            d.exec(QStringLiteral("CREATE TABLE items (%1)")
                       .arg(itemColumns.join(QStringLiteral(", "))));
        !status)
        return status;
    if (board::Status status = d.exec(QStringLiteral(
            "CREATE TABLE sqlar (name TEXT PRIMARY KEY, item_id INTEGER NOT NULL UNIQUE, "
            "mode INT, mtime INT default current_timestamp, sz INT, data BLOB)"));
        !status)
        return status;
    if (withLod) {
        if (board::Status status = d.exec(QStringLiteral(
                "CREATE TABLE lod (item_id INTEGER NOT NULL, fraction REAL NOT NULL, "
                "format TEXT, orig_w INTEGER, orig_h INTEGER, sz INT, data BLOB, "
                "PRIMARY KEY (item_id, fraction))"));
            !status)
            return status;
    }

    QStringList insertColumns{QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("x"),
                              QStringLiteral("y"), QStringLiteral("data")};
    QStringList insertValues{QStringLiteral("1"), QStringLiteral("'pixmap'"), QStringLiteral("10"),
                             QStringLiteral("20"), QStringLiteral("'{\"filename\":\"a.png\"}'")};
    insertColumns << QStringLiteral("flip");
    insertValues << QString::number(flip);
    if (withMeta) {
        insertColumns << QStringLiteral("meta");
        insertValues << QStringLiteral("'{}'");
    }
    if (withUuid) {
        insertColumns << QStringLiteral("uuid");
        insertValues << QStringLiteral("'abc-uuid'");
    }
    if (board::Status status =
            d.exec(QStringLiteral("INSERT INTO items (%1) VALUES (%2)")
                       .arg(insertColumns.join(QStringLiteral(", ")),
                            insertValues.join(QStringLiteral(", "))));
        !status)
        return status;
    if (board::Status status = d.exec(QStringLiteral(
            "INSERT INTO sqlar (item_id, name, mode, sz, data) "
            "VALUES (1, '0001-a.png', 420, %1, x'%2')")
            .arg(kBlob.size())
            .arg(QString::fromLatin1(kBlob.toHex())));
        !status)
        return status;
    if (withLod) {
        if (board::Status status = d.exec(QStringLiteral(
                "INSERT INTO lod (item_id, fraction, format, orig_w, orig_h, sz, data) "
                "VALUES (1, 0.25, 'png', 400, 300, 3, x'6c766c')"));
            !status)
            return status;
    }
    if (board::Status status = d.exec(QStringLiteral("PRAGMA user_version=%1").arg(userVersion));
        !status)
        return status;
    return d.exec(QStringLiteral("PRAGMA application_id=1382546008"));
}

} // namespace

class TestBoard : public QObject
{
    Q_OBJECT

private slots:
    void opensCurrentBoardInPlace();
    void migratesOldBoardOnCopy();
    void opensNewerVersionReadOnlyUntouched();
    void salvagesImagesWithoutItemRows();
    void detectsExternalChanges();
    void detectsReplacedFiles();
    void rejectsNonBoardFile();
    void readsFilesWithMissingOptionalColumns();
    void missingBlobIsAnError();
    void sweepsStaleTempFiles();
    void readsConcurrentlyWithWorker();
    void normalizesOddFlipValues();
};

void TestBoard::opensCurrentBoardInPlace()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("current.beex"));
    const QString cacheDir = dir.filePath(QStringLiteral("cache"));
    QVERIFY(createBoard(path, board::schema::kUserVersion, true, true, true).isOk());

    const QByteArray hashBefore = fileHash(path);
    const QDateTime modifiedBefore = QFileInfo(path).lastModified();
    const QStringList entriesBefore = dirEntries(dir.path());

    auto board = board::Board::open(path, cacheDir);
    QVERIFY(board.isOk());
    QVERIFY(board.value().tempPath().isEmpty());
    QCOMPARE(board.value().path(), path);
    QVERIFY(!QFile::exists(cacheDir));

    auto items = board.value().items();
    QVERIFY(items.isOk());
    QCOMPARE(items.value().size(), 1);
    QCOMPARE(items.value().first().id, qint64(1));
    QCOMPARE(items.value().first().type, QStringLiteral("pixmap"));
    QCOMPARE(items.value().first().scale, 1.0);
    QCOMPARE(items.value().first().uuid, QStringLiteral("abc-uuid"));

    auto blob = board.value().blob(1);
    QVERIFY(blob.isOk());
    QCOMPARE(blob.value(), kBlob);

    auto sizes = board.value().originalSizes();
    QVERIFY(sizes.isOk());
    QCOMPARE(sizes.value().value(1), QSize(400, 300));

    auto floors = board.value().floorLevels();
    QVERIFY(floors.isOk());
    QCOMPARE(floors.value().size(), 1);
    QCOMPARE(floors.value().value(1).fraction, 0.25);
    QCOMPARE(floors.value().value(1).data, QByteArray("lvl"));

    auto counts = board.value().counts();
    QVERIFY(counts.isOk());
    QCOMPARE(counts.value().value(QStringLiteral("items")), qint64(1));
    QCOMPARE(counts.value().value(QStringLiteral("sqlar")), qint64(1));
    QCOMPARE(counts.value().value(QStringLiteral("lod")), qint64(1));

    board.value().close();
    QCOMPARE(fileHash(path), hashBefore);
    QCOMPARE(QFileInfo(path).lastModified(), modifiedBefore);
    QCOMPARE(dirEntries(dir.path()), entriesBefore);
}

void TestBoard::migratesOldBoardOnCopy()
{
    QTemporaryDir dir;
    QTemporaryDir cache;
    QVERIFY(dir.isValid());
    QVERIFY(cache.isValid());
    const QString path = dir.filePath(QStringLiteral("old.beex"));
    const QString cacheDir = cache.path();
    QVERIFY(createBoard(path, 3, true, false, false).isOk());

    const QByteArray hashBefore = fileHash(path);
    const QDateTime modifiedBefore = QFileInfo(path).lastModified();
    const QStringList entriesBefore = dirEntries(dir.path());

    auto board = board::Board::open(path, cacheDir);
    QVERIFY(board.isOk());
    const QString tempPath = board.value().tempPath();
    QVERIFY(!tempPath.isEmpty());
    QVERIFY(QFile::exists(tempPath));
    QVERIFY(tempPath.startsWith(cacheDir));
    QCOMPARE(board.value().path(), path);

    // The copy was migrated to the current shape.
    QCOMPARE(board.value().columns().uuid, true);
    QCOMPARE(board.value().columns().lod, true);
    auto items = board.value().items();
    QVERIFY(items.isOk());
    QCOMPARE(items.value().size(), 1);
    QCOMPARE(items.value().first().uuid, QString());
    auto blob = board.value().blob(1);
    QVERIFY(blob.isOk());
    QCOMPARE(blob.value(), kBlob);

    // The original file and its directory are untouched.
    QCOMPARE(fileHash(path), hashBefore);
    QCOMPARE(QFileInfo(path).lastModified(), modifiedBefore);
    QCOMPARE(dirEntries(dir.path()), entriesBefore);

    board.value().close();
    QVERIFY(!QFile::exists(tempPath));
}

void TestBoard::opensNewerVersionReadOnlyUntouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("future.beex"));
    QVERIFY(createBoard(path, board::schema::kUserVersion, true, true, true).isOk());
    {
        auto db = board::Connection::open(path, board::Connection::OpenMode::ReadWrite);
        QVERIFY(db.isOk());
        QVERIFY(db.value().exec(QStringLiteral("PRAGMA user_version=6")).isOk());
    }

    // A newer file still opens -- the scene is not lost -- but it is
    // marked so a save never downgrades it in place, and it is opened in
    // place (no migration copy).
    const QByteArray hashBefore = fileHash(path);
    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    if (!board)
        QFAIL(qPrintable(board.error().toString()));
    QVERIFY(board.value().isNewerVersion());
    QVERIFY(board.value().tempPath().isEmpty());
    auto items = board.value().items();
    QVERIFY(items.isOk());
    QCOMPARE(items.value().size(), 1);
    QCOMPARE(fileHash(path), hashBefore);
}

void TestBoard::salvagesImagesWithoutItemRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("blobs-only.beex"));

    // A file whose item table is gone but whose blob store survived.
    {
        auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(db.value()
                    .exec(QStringLiteral("CREATE TABLE sqlar (name TEXT PRIMARY KEY, "
                                         "item_id INTEGER NOT NULL UNIQUE, mode INT, mtime INT, "
                                         "sz INT, data BLOB)"))
                    .isOk());
        QVERIFY(db.value()
                    .exec(QStringLiteral("INSERT INTO sqlar (name, item_id, sz, data) "
                                         "VALUES ('a.png', 1, 4, x'01020304')"))
                    .isOk());
        QVERIFY(db.value()
                    .exec(QStringLiteral("INSERT INTO sqlar (name, item_id, sz, data) "
                                         "VALUES ('b.jpg', 2, 4, x'05060708')"))
                    .isOk());
    }

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    if (!board)
        QFAIL(qPrintable(board.error().toString()));
    QVERIFY(board.value().isSalvaged());
    QVERIFY(!board.value().items().isOk()); // there is no items table

    auto salvaged = board.value().salvageItems();
    QVERIFY(salvaged.isOk());
    QCOMPARE(salvaged.value().size(), 2);
    QCOMPARE(salvaged.value().first().type, QStringLiteral("pixmap"));
    QCOMPARE(salvaged.value().first().id, qint64(1));
    QVERIFY(salvaged.value().first().data.contains(QStringLiteral("a.png")));
    QCOMPARE(board.value().blob(1).value(), QByteArray::fromHex("01020304"));
}

void TestBoard::rejectsNonBoardFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("other.db"));
    {
        auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(db.value().exec(QStringLiteral("CREATE TABLE t (id INTEGER)")).isOk());
    }

    const QByteArray hashBefore = fileHash(path);
    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(!board.isOk());
    QVERIFY(board.error().message.contains(QStringLiteral("no items table")));
    QCOMPARE(fileHash(path), hashBefore);
}

void TestBoard::readsFilesWithMissingOptionalColumns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sparse.beex"));
    // Stamped current, but without meta/uuid columns or lod table.
    QVERIFY(createBoard(path, board::schema::kUserVersion, false, false, false).isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QVERIFY(board.value().tempPath().isEmpty());
    QVERIFY(!board.value().columns().meta);
    QVERIFY(!board.value().columns().uuid);
    QVERIFY(!board.value().columns().lod);

    auto items = board.value().items();
    QVERIFY(items.isOk());
    QCOMPARE(items.value().size(), 1);
    QCOMPARE(items.value().first().meta, QString());
    QCOMPARE(items.value().first().uuid, QString());

    QVERIFY(board.value().originalSizes().isOk());
    QVERIFY(board.value().originalSizes().value().isEmpty());
    QVERIFY(board.value().floorLevels().isOk());
    QVERIFY(board.value().floorLevels().value().isEmpty());
    QCOMPARE(board.value().counts().value().value(QStringLiteral("lod")), qint64(-1));
}

void TestBoard::missingBlobIsAnError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("current.beex"));
    QVERIFY(createBoard(path, board::schema::kUserVersion, true, true, true).isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    auto blob = board.value().blob(999);
    QVERIFY(!blob.isOk());
    QVERIFY(blob.error().message.contains(QStringLiteral("No image data")));
}

void TestBoard::sweepsStaleTempFiles()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());

    const QString alive = cache.filePath(
        QStringLiteral("open-%1-1.beex").arg(QCoreApplication::applicationPid()));
    const QString dead = cache.filePath(QStringLiteral("open-2147483646-1.beex"));
    const QString unparsable = cache.filePath(QStringLiteral("open-garbage-1.beex"));
    for (const QString &path : {alive, dead, unparsable}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }

    board::sweepStaleTempFiles(cache.path());
    QVERIFY(QFile::exists(alive));
    QVERIFY(!QFile::exists(dead));
    QVERIFY(!QFile::exists(unparsable));
}

void TestBoard::readsConcurrentlyWithWorker()
{
    QTemporaryDir dir;
    QTemporaryDir cache;
    QVERIFY(dir.isValid());
    QVERIFY(cache.isValid());
    const QString path = dir.filePath(QStringLiteral("concurrent.beex"));

    {
        auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(board::schema::createTables(db.value()).isOk());
        QVERIFY(board::schema::writeHeader(db.value()).isOk());
        for (int i = 1; i <= 20; ++i) {
            auto item = db.value().prepare(QStringLiteral(
                "INSERT INTO items (id, type, data) VALUES (?, 'pixmap', '{}')"));
            QVERIFY(item.isOk());
            QVERIFY(item.value().bind(1, qint64(i)).isOk());
            QVERIFY(item.value().exec().isOk());

            const QByteArray blob = QByteArrayLiteral("blob-") + QByteArray::number(i);
            auto sqlar = db.value().prepare(QStringLiteral(
                "INSERT INTO sqlar (item_id, name, mode, sz, data) VALUES (?, ?, 420, ?, ?)"));
            QVERIFY(sqlar.isOk());
            QVERIFY(sqlar.value().bind(1, qint64(i)).isOk());
            QVERIFY(sqlar.value().bind(2, QStringLiteral("%1.png").arg(i)).isOk());
            QVERIFY(sqlar.value().bind(3, qint64(blob.size())).isOk());
            QVERIFY(sqlar.value().bind(4, blob).isOk());
            QVERIFY(sqlar.value().exec().isOk());
        }
    }

    auto board = board::Board::open(path, cache.path());
    QVERIFY(board.isOk());
    QVERIFY(board.value().tempPath().isEmpty());

    std::atomic<int> mismatches{0};
    const auto worker = [&board, &mismatches]() {
        for (int i = 0; i < 200; ++i) {
            const qint64 id = (i % 20) + 1;
            auto blob = board.value().blob(id);
            if (!blob || blob.value() != QByteArrayLiteral("blob-") + QByteArray::number(id))
                ++mismatches;
        }
    };

    std::thread workerThread(worker);
    for (int i = 0; i < 200; ++i) {
        auto items = board.value().items();
        if (!items || items.value().size() != 20)
            ++mismatches;
    }
    workerThread.join();
    QCOMPARE(mismatches.load(), 0);
}

void TestBoard::normalizesOddFlipValues()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // The reference flips an item unless the stored value is exactly 1
    // (its loader calls do_flip() when it differs); a stored 0 would
    // otherwise render the item zero-width.
    const int storedValues[] = {1, -1, 0, 2};
    for (int stored : storedValues) {
        const QString path = dir.filePath(QStringLiteral("flip-%1.beex").arg(stored));
        QVERIFY(createBoard(path, board::schema::kUserVersion, true, true, false, stored).isOk());
        auto board = board::Board::open(path, dir.path());
        QVERIFY(board.isOk());
        auto items = board.value().items();
        QVERIFY(items.isOk());
        QCOMPARE(items.value().size(), 1);
        QCOMPARE(items.value().first().flip, stored == 1 ? qint64(1) : qint64(-1));
    }
}

void TestBoard::detectsExternalChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));
    QVERIFY(createBoard(path, board::schema::kUserVersion, true, true, true).isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QVERIFY(!board.value().hasChangedOnDisk());

    // A commit from another connection is a change.
    {
        auto other = board::Connection::open(path, board::Connection::OpenMode::ReadWrite);
        QVERIFY(other.isOk());
        QVERIFY(other.value().exec(QStringLiteral("UPDATE items SET x = x + 1")).isOk());
    }
    QVERIFY(board.value().hasChangedOnDisk());
}

void TestBoard::detectsReplacedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));
    QVERIFY(createBoard(path, board::schema::kUserVersion, true, true, true).isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QVERIFY(!board.value().hasChangedOnDisk());

    // Growing the file behind the board's back is a change even though
    // its connection saw no commit (the app's own atomic save replaces
    // the file the same way, which is why a board is reopened after a
    // save).
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::Append));
        QVERIFY(file.write(QByteArrayLiteral("x")) == 1);
    }
    QVERIFY(board.value().hasChangedOnDisk());

    // A reopen records a fresh identity, so the next check is quiet.
    auto reopened = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QVERIFY(!reopened.value().hasChangedOnDisk());
}

QTEST_GUILESS_MAIN(TestBoard)

#include "test_board.moc"
