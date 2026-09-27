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
class Transaction;

// SQLite connection wrapper.
//
// One Connection is exactly one sqlite3* handle. It is not a database
// file and not a pool: a file is just the path passed to open(), and a
// second file is either another Connection or ATTACHed to this one.
//
// Connection layout in the app — one connection per thread that needs
// a file, never one connection shared across threads:
//   * board file, UI reads      read-only Connection
//   * board file, worker reads  a second read-only Connection
//   * session cache file        its own Connection, worker-owned
//   * save target file          its own Connection, save worker
// Operations are serialized on the connection's mutex, so sharing is
// safe, but per-thread connections are the intended pattern. Pragmas
// with connection scope (busy_timeout, query_only, cache_size,
// foreign_keys) are set on every connection at open; file-scoped ones
// (application_id, user_version, journal_mode) are set once through
// the schema layer.
//
// close() is serialized with in-flight operations and subsequent use
// fails cleanly. Statements keep the connection state alive, so they
// may outlive the Connection object, but one Statement must still be
// used by one thread at a time.
class Connection
{
public:
    enum class OpenMode {
        ReadOnly,  // SQLITE_OPEN_READONLY; the file must exist
        ReadWrite, // SQLITE_OPEN_READWRITE; the file must exist
        Create,    // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
    };

    Connection() = default;
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection(Connection &&other) noexcept;
    Connection &operator=(Connection &&other) noexcept;

    // Opens path; URIs ("file:...?mode=ro") are honored. Sets
    // busy_timeout=5000.
    static Result<Connection> open(const QString &path, OpenMode mode);

    bool isOpen() const;
    QString path() const;

    // Runs a statement without using result rows (DDL, PRAGMA writes).
    Status exec(const QString &sql);

    // Runs a multi-statement script, as used for the schema.
    Status execScript(const QString &sql);

    Result<Statement> prepare(const QString &sql);

    qint64 lastInsertRowId() const;
    int changes() const;

    // Serialized with in-flight operations. Called by the destructor.
    void close();

    // Renders value as a SQL string literal, for statements that cannot
    // use bindings (VACUUM INTO).
    static QString quote(const QString &value);

private:
    friend class Statement;
    friend class Transaction;

    struct State
    {
        std::mutex mutex;
        sqlite3 *db = nullptr;
        QString path;
        bool closed = false;
    };

    explicit Connection(std::shared_ptr<State> state);

    static Error makeError(const State &state, int code);
    static Status execOnState(const std::shared_ptr<State> &state, const QString &sql);

    // Shared so statements stay valid (and see the closed flag) when the
    // Connection is moved or destroyed.
    std::shared_ptr<State> state_;
};

// Prepared statement, owned by one Connection; move-only. Blob bindings
// use SQLITE_STATIC: the caller keeps the bytes alive until step()
// returns, which is what keeps large payloads from being copied.
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
    friend class Connection;
    Statement(std::shared_ptr<Connection::State> state, sqlite3_stmt *stmt);

    Error errorFromDb(int code) const;

    std::shared_ptr<Connection::State> state_;
    sqlite3_stmt *stmt_ = nullptr;
};

// BEGIN/COMMIT guard on one Connection: rolls back on destruction
// unless committed. Keeps the connection state alive, so it is safe to
// destroy after the Connection itself (the rollback is skipped when
// the connection is already closed).
class Transaction
{
public:
    static Result<Transaction> begin(Connection &connection);

    Transaction() = default;
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    Transaction(Transaction &&other) noexcept;
    Transaction &operator=(Transaction &&other) noexcept;

    Status commit();

private:
    explicit Transaction(std::shared_ptr<Connection::State> state);

    std::shared_ptr<Connection::State> state_;
};

} // namespace board
