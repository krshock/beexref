#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "board/schema.h"
#include "board/sqlite.h"

namespace {

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

bool tableHasColumn(board::Database &db, const QString &table, const QString &column)
{
    auto statement = db.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    if (!statement)
        return false;
    while (true) {
        auto row = statement.value().step();
        if (!row || !row.value())
            return false;
        if (statement.value().columnText(1) == column)
            return true;
    }
}

bool tableExists(board::Database &db, const QString &table)
{
    auto statement = db.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?"));
    if (!statement || !statement.value().bind(1, table).isOk())
        return false;
    auto row = statement.value().step();
    return row.isOk() && row.value();
}

// items/sqlar tables as they look in an older native file, optional
// fork columns included.
board::Status createOldSchema(board::Database &db, bool withData, bool withMeta, bool withUuid)
{
    QStringList columns{QStringLiteral("id INTEGER PRIMARY KEY"),
                        QStringLiteral("type TEXT NOT NULL"),
                        QStringLiteral("x REAL DEFAULT 0"),
                        QStringLiteral("y REAL DEFAULT 0"),
                        QStringLiteral("z REAL DEFAULT 0"),
                        QStringLiteral("scale REAL DEFAULT 1"),
                        QStringLiteral("rotation REAL DEFAULT 0"),
                        QStringLiteral("flip INTEGER DEFAULT 1")};
    if (withData)
        columns << QStringLiteral("data JSON");
    if (withMeta)
        columns << QStringLiteral("meta JSON");
    if (withUuid)
        columns << QStringLiteral("uuid TEXT");

    const QString itemsSql =
        QStringLiteral("CREATE TABLE items (%1)").arg(columns.join(QStringLiteral(", ")));
    if (board::Status status = db.exec(itemsSql); !status)
        return status;

    const QString sqlarSql = QStringLiteral(
        "CREATE TABLE sqlar (name TEXT PRIMARY KEY, item_id INTEGER NOT NULL UNIQUE, "
        "mode INT, mtime INT, sz INT, data BLOB)");
    if (board::Status status = db.exec(sqlarSql); !status)
        return status;

    return db.exec(QStringLiteral(
        "INSERT INTO items (id, type, data) VALUES (1, 'pixmap', '{\"filename\":\"a.png\"}')"));
}

} // namespace

class TestSchema : public QObject
{
    Q_OBJECT

private slots:
    void createsTablesAndHeader();
    void migratesFromVersion3();
    void migratesFromVersion1();
    void migrationIsIdempotentWithExistingColumns();
    void rejectsNewerVersion();
    void migratesUninitializedDatabase();
};

void TestSchema::createsTablesAndHeader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto db = board::Database::open(dir.filePath(QStringLiteral("new.beex")),
                                    board::Database::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(board::schema::createTables(db.value()).isOk());
    QVERIFY(board::schema::writeHeader(db.value()).isOk());

    QCOMPARE(board::schema::readUserVersion(db.value()).value(), board::schema::kUserVersion);
    QCOMPARE(board::schema::readApplicationId(db.value()).value(), board::schema::kApplicationId);
    QVERIFY(board::schema::hasItemsTable(db.value()).value());
    QVERIFY(tableExists(db.value(), QStringLiteral("items")));
    QVERIFY(tableExists(db.value(), QStringLiteral("sqlar")));
    QVERIFY(tableExists(db.value(), QStringLiteral("lod")));
    QVERIFY(tableHasColumn(db.value(), QStringLiteral("items"), QStringLiteral("meta")));
    QVERIFY(tableHasColumn(db.value(), QStringLiteral("items"), QStringLiteral("uuid")));

    // Creating twice is fine.
    QVERIFY(board::schema::createTables(db.value()).isOk());
}

void TestSchema::migratesFromVersion3()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("v3.beex"));

    {
        auto db = board::Database::open(path, board::Database::OpenMode::Create);
        QVERIFY(db.isOk());
        // v3 files already carry meta; migrations are keyed by target
        // version, so uuid (v4) and lod (v5) are the remaining steps.
        QVERIFY(createOldSchema(db.value(), true, true, false).isOk());
        QVERIFY(db.value().exec(QStringLiteral("PRAGMA user_version=3")).isOk());
        QVERIFY(db.value()
                    .exec(QStringLiteral("PRAGMA application_id=%1")
                              .arg(board::schema::kBeeApplicationId))
                    .isOk());
    }

    auto db = board::Database::open(path, board::Database::OpenMode::ReadWrite);
    QVERIFY(db.isOk());
    QVERIFY(board::schema::migrateToCurrent(db.value()).isOk());

    QCOMPARE(board::schema::readUserVersion(db.value()).value(), board::schema::kUserVersion);
    QCOMPARE(board::schema::readApplicationId(db.value()).value(), board::schema::kApplicationId);
    QVERIFY(tableHasColumn(db.value(), QStringLiteral("items"), QStringLiteral("meta")));
    QVERIFY(tableHasColumn(db.value(), QStringLiteral("items"), QStringLiteral("uuid")));
    QVERIFY(tableExists(db.value(), QStringLiteral("lod")));

    auto statement = db.value().prepare(QStringLiteral("SELECT data FROM items WHERE id=1"));
    QVERIFY(statement.isOk());
    auto row = statement.value().step();
    QVERIFY(row.isOk() && row.value());
    QVERIFY(statement.value().columnText(0).contains(QStringLiteral("a.png")));
}

void TestSchema::migratesFromVersion1()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("v1.bee"));

    {
        auto db = board::Database::open(path, board::Database::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(db.value()
                    .exec(QStringLiteral("CREATE TABLE items (id INTEGER PRIMARY KEY, "
                                         "type TEXT NOT NULL, x REAL, y REAL, z REAL, "
                                         "scale REAL, rotation REAL, flip INTEGER, "
                                         "filename TEXT)"))
                    .isOk());
        QVERIFY(db.value()
                    .exec(QStringLiteral("INSERT INTO items (id, type, filename) "
                                         "VALUES (1, 'pixmap', 'legacy.png')"))
                    .isOk());
        QVERIFY(db.value().exec(QStringLiteral("PRAGMA user_version=1")).isOk());
    }

    auto db = board::Database::open(path, board::Database::OpenMode::ReadWrite);
    QVERIFY(db.isOk());
    QVERIFY(board::schema::migrateToCurrent(db.value()).isOk());
    QCOMPARE(board::schema::readUserVersion(db.value()).value(), board::schema::kUserVersion);

    auto statement = db.value().prepare(
        QStringLiteral("SELECT json_extract(data, '$.filename') FROM items WHERE id=1"));
    QVERIFY(statement.isOk());
    auto row = statement.value().step();
    QVERIFY(row.isOk() && row.value());
    QCOMPARE(statement.value().columnText(0), QStringLiteral("legacy.png"));
}

void TestSchema::migrationIsIdempotentWithExistingColumns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("downgraded.beex"));

    {
        auto db = board::Database::open(path, board::Database::OpenMode::Create);
        QVERIFY(db.isOk());
        // meta already exists while the version stamp says 3.
        QVERIFY(createOldSchema(db.value(), true, true, false).isOk());
        QVERIFY(db.value().exec(QStringLiteral("PRAGMA user_version=3")).isOk());
    }

    auto db = board::Database::open(path, board::Database::OpenMode::ReadWrite);
    QVERIFY(db.isOk());
    QVERIFY(board::schema::migrateToCurrent(db.value()).isOk());
    QCOMPARE(board::schema::readUserVersion(db.value()).value(), board::schema::kUserVersion);
    QVERIFY(tableHasColumn(db.value(), QStringLiteral("items"), QStringLiteral("meta")));
    QVERIFY(tableHasColumn(db.value(), QStringLiteral("items"), QStringLiteral("uuid")));

    // Running again is a no-op.
    QVERIFY(board::schema::migrateToCurrent(db.value()).isOk());
}

void TestSchema::rejectsNewerVersion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("future.beex"));

    {
        auto db = board::Database::open(path, board::Database::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(createOldSchema(db.value(), true, true, true).isOk());
        QVERIFY(db.value().exec(QStringLiteral("PRAGMA user_version=6")).isOk());
    }

    const QByteArray before = fileHash(path);
    QVERIFY(!before.isEmpty());

    auto db = board::Database::open(path, board::Database::OpenMode::ReadWrite);
    QVERIFY(db.isOk());
    auto status = board::schema::migrateToCurrent(db.value());
    QVERIFY(!status.isOk());
    QVERIFY(status.error().message.contains(QStringLiteral("newer than supported")));
    QCOMPARE(board::schema::readUserVersion(db.value()).value(), 6);
    db.value() = {};

    QCOMPARE(fileHash(path), before);
}

void TestSchema::migratesUninitializedDatabase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto db = board::Database::open(dir.filePath(QStringLiteral("empty.beex")),
                                    board::Database::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(!board::schema::hasItemsTable(db.value()).value());
    QCOMPARE(board::schema::readUserVersion(db.value()).value(), 0);

    QVERIFY(board::schema::migrateToCurrent(db.value()).isOk());
    QVERIFY(board::schema::hasItemsTable(db.value()).value());
    QCOMPARE(board::schema::readUserVersion(db.value()).value(), board::schema::kUserVersion);
}

QTEST_GUILESS_MAIN(TestSchema)

#include "test_schema.moc"
