#pragma once

#include "error.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <mutex>

struct sqlite3;
struct sqlite3_stmt;

namespace board {

class Statement;

// Thin RAII wrapper around a SQLite connection. All operations are
// serialized on an internal mutex so one connection can be shared
// between threads (the app reads board blobs from the decode worker).
class Database
{
public:
    enum class OpenMode {
        ReadOnly,  // SQLITE_OPEN_READONLY; the file must exist
        ReadWrite, // SQLITE_OPEN_READWRITE; the file must exist
        Create,    // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
    };

    Database() = default;
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Database(Database &&other) noexcept;
    Database &operator=(Database &&other) noexcept;

    // Opens path; URIs ("file:...?mode=ro") are honored. Uses
    // busy_timeout=5000 like the Python reference.
    static Result<Database> open(const QString &path, OpenMode mode);

    bool isOpen() const { return db_ != nullptr; }
    QString path() const { return path_; }

    sqlite3 *handle() const { return db_; }

    // Runs a statement without using result rows (DDL, PRAGMA writes).
    Status exec(const QString &sql);

    // Runs a multi-statement script, as used for the schema.
    Status execScript(const QString &sql);

    Result<Statement> prepare(const QString &sql);

    qint64 lastInsertRowId() const;
    int changes() const;

    // Renders value as a SQL string literal, for statements that cannot
    // use bindings (VACUUM INTO).
    static QString quote(const QString &value);

private:
    friend class Statement;
    friend class Transaction;

    Database(sqlite3 *db, QString path);
    Error errorFromDb(int code = 0) const;

    sqlite3 *db_ = nullptr;
    QString path_;
    // Shared so statements keep locking the same mutex when the
    // Database is moved.
    std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

// Prepared statement. Move-only; must not outlive its Database.
class Statement
{
public:
    Statement() = default;
    ~Statement();
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    Statement(Statement &&other) noexcept;
    Statement &operator=(Statement &&other) noexcept;

    bool isValid() const { return stmt_ != nullptr; }

    Status bind(int index, std::nullptr_t);
    Status bind(int index, int value);
    Status bind(int index, qint64 value);
    Status bind(int index, double value);
    Status bind(int index, const QString &value);
    // The blob is bound by reference (SQLITE_STATIC): it must stay
    // alive and unchanged until step() returns. This keeps large image
    // payloads from being copied.
    Status bind(int index, const QByteArray &blob);

    // Steps once: true when a row is available, false when done.
    Result<bool> step();

    // Steps to completion, ignoring rows.
    Status exec();

    Status reset();

    int columnCount() const;
    bool isNull(int column) const;
    qint64 columnInt64(int column) const;
    double columnDouble(int column) const;
    QString columnText(int column) const;
    QByteArray columnBlob(int column) const;

private:
    friend class Database;
    Statement(sqlite3 *db, sqlite3_stmt *stmt, std::shared_ptr<std::mutex> mutex);

    Error errorFromDb(int code = 0) const;
    bool hasRow() const { return hasRow_; }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    std::shared_ptr<std::mutex> mutex_;
    bool hasRow_ = false;
};

// BEGIN/COMMIT guard: rolls back on destruction unless committed.
class Transaction
{
public:
    static Result<Transaction> begin(Database &db);

    Transaction() = default;
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    Transaction(Transaction &&other) noexcept;
    Transaction &operator=(Transaction &&other) noexcept;

    Status commit();

private:
    explicit Transaction(Database *db)
        : db_(db)
    {
    }

    Database *db_ = nullptr;
};

} // namespace board
