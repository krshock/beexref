#include "schema.h"

#include "sqlite.h"

namespace board::schema {
namespace {

// The three schema scripts define the board schema.
constexpr const char *kItemsTable = R"(
    CREATE TABLE IF NOT EXISTS items (
        id INTEGER PRIMARY KEY,
        type TEXT NOT NULL,
        x REAL DEFAULT 0,
        y REAL DEFAULT 0,
        z REAL DEFAULT 0,
        scale REAL DEFAULT 1,
        rotation REAL DEFAULT 0,
        flip INTEGER DEFAULT 1,
        data JSON,
        meta JSON,
        uuid TEXT
    )
)";

constexpr const char *kSqlarTable = R"(
    CREATE TABLE IF NOT EXISTS sqlar (
        name TEXT PRIMARY KEY,
        item_id INTEGER NOT NULL UNIQUE,
        mode INT,
        mtime INT default current_timestamp,
        sz INT,
        data BLOB,
        FOREIGN KEY (item_id)
          REFERENCES items (id)
             ON DELETE CASCADE
             ON UPDATE NO ACTION
    )
)";

// Upstream BeeRef's legacy items table: no meta or uuid columns.
constexpr const char *kBeeItemsTable = R"(
    CREATE TABLE IF NOT EXISTS items (
        id INTEGER PRIMARY KEY,
        type TEXT NOT NULL,
        x REAL DEFAULT 0,
        y REAL DEFAULT 0,
        z REAL DEFAULT 0,
        scale REAL DEFAULT 1,
        rotation REAL DEFAULT 0,
        flip INTEGER DEFAULT 1,
        data JSON
    )
)";

constexpr const char *kLodTable = R"(
    CREATE TABLE IF NOT EXISTS lod (
        item_id INTEGER NOT NULL,
        fraction REAL NOT NULL,
        format TEXT,
        orig_w INTEGER,
        orig_h INTEGER,
        sz INT,
        data BLOB,
        PRIMARY KEY (item_id, fraction),
        FOREIGN KEY (item_id)
          REFERENCES items (id)
             ON DELETE CASCADE
             ON UPDATE NO ACTION
    )
)";

Status execScript(Connection &db, const char *sql)
{
    return db.execScript(QString::fromLatin1(sql));
}

Result<bool> hasColumn(Connection &db, const QString &table, const QString &column)
{
    auto statement = db.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    if (!statement)
        return statement.error();

    Statement &stmt = statement.value();
    while (true) {
        auto row = stmt.step();
        if (!row)
            return row.error();
        if (!row.value())
            return false;
        // table_info columns: cid, name, type, notnull, dflt_value, pk
        if (stmt.columnText(1) == column)
            return true;
    }
}

// Adds a column only when it is absent. Must be idempotent: a file
// saved by an older app version may report a lower user_version while
// already carrying the column.
Status ensureColumn(Connection &db, const QString &column, const QString &alter)
{
    auto present = hasColumn(db, QStringLiteral("items"), column);
    if (!present)
        return Status::fail(present.error());
    if (present.value())
        return Status::ok();
    return db.exec(alter);
}

Status applyMigration(Connection &db, int target)
{
    switch (target) {
    case 2:
        if (Status status = db.exec(QStringLiteral("ALTER TABLE items ADD COLUMN data JSON")); !status)
            return status;
        return db.exec(
            QStringLiteral("UPDATE items SET data = json_object('filename', filename)"));
    case 3:
        return ensureColumn(db, QStringLiteral("meta"),
                            QStringLiteral("ALTER TABLE items ADD COLUMN meta JSON"));
    case 4:
        return ensureColumn(db, QStringLiteral("uuid"),
                            QStringLiteral("ALTER TABLE items ADD COLUMN uuid TEXT"));
    case 5:
        return execScript(db, kLodTable);
    default:
        return Status::fail(Error{0,
                                  QStringLiteral("No migration path to version %1").arg(target),
                                  db.path()});
    }
}

} // namespace

Status createTables(Connection &db)
{
    if (Status status = execScript(db, kItemsTable); !status)
        return status;
    if (Status status = execScript(db, kSqlarTable); !status)
        return status;
    return execScript(db, kLodTable);
}

Status createBeeTables(Connection &db)
{
    // The legacy format has no meta, uuid or lod columns.
    if (Status status = execScript(db, kBeeItemsTable); !status)
        return status;
    return execScript(db, kSqlarTable);
}

Status writeHeader(Connection &db, int userVersion, int applicationId)
{
    if (Status status = db.exec(QStringLiteral("PRAGMA application_id=%1").arg(applicationId));
        !status)
        return status;
    return db.exec(QStringLiteral("PRAGMA user_version=%1").arg(userVersion));
}

Result<int> readUserVersion(Connection &db)
{
    auto statement = db.prepare(QStringLiteral("PRAGMA user_version"));
    if (!statement)
        return statement.error();
    auto row = statement.value().step();
    if (!row)
        return row.error();
    if (!row.value()) {
        return Error{0, QStringLiteral("PRAGMA user_version returned no row"), db.path()};
    }
    return static_cast<int>(statement.value().columnInt64(0));
}

Result<int> readApplicationId(Connection &db)
{
    auto statement = db.prepare(QStringLiteral("PRAGMA application_id"));
    if (!statement)
        return statement.error();
    auto row = statement.value().step();
    if (!row)
        return row.error();
    if (!row.value()) {
        return Error{0, QStringLiteral("PRAGMA application_id returned no row"), db.path()};
    }
    return static_cast<int>(statement.value().columnInt64(0));
}

Result<bool> hasItemsTable(Connection &db)
{
    auto statement = db.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='items'"));
    if (!statement)
        return statement.error();
    auto row = statement.value().step();
    if (!row)
        return row.error();
    return row.value();
}

Status migrateToCurrent(Connection &db)
{
    auto version = readUserVersion(db);
    if (!version)
        return Status::fail(version.error());
    const int from = version.value();

    if (from > kUserVersion) {
        return Status::fail(
            Error{0,
                  QStringLiteral("File format version %1 is newer than supported (%2)")
                      .arg(from)
                      .arg(kUserVersion),
                  db.path()});
    }
    if (from == kUserVersion)
        return Status::ok();

    auto transaction = Transaction::begin(db);
    if (!transaction)
        return Status::fail(transaction.error());

    if (from == 0) {
        // Uninitialized database: create the schema instead of
        // migrating.
        if (Status status = createTables(db); !status)
            return status;
    } else {
        for (int target = from + 1; target <= kUserVersion; ++target) {
            if (Status status = applyMigration(db, target); !status)
                return status;
        }
    }

    if (Status status = writeHeader(db); !status)
        return status;
    return transaction.value().commit();
}

} // namespace board::schema
