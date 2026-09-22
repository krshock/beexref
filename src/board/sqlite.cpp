#include "sqlite.h"

#include <sqlite3.h>

#include <utility>

namespace board {

QString Error::toString() const
{
    if (path.isEmpty())
        return message;
    return QStringLiteral("%1 [%2]").arg(message, path);
}

namespace {

Error closedError(const QString &path = QString())
{
    return Error{SQLITE_MISUSE, QStringLiteral("Connection is closed"), path};
}

} // namespace

Connection::Connection(std::shared_ptr<State> state)
    : state_(std::move(state))
{
}

Connection::~Connection()
{
    close();
}

Connection::Connection(Connection &&other) noexcept
    : state_(std::move(other.state_))
{
}

Connection &Connection::operator=(Connection &&other) noexcept
{
    if (this == &other)
        return *this;
    close();
    state_ = std::move(other.state_);
    return *this;
}

Result<Connection> Connection::open(const QString &path, OpenMode mode)
{
    int flags = SQLITE_OPEN_URI;
    switch (mode) {
    case OpenMode::ReadOnly:
        flags |= SQLITE_OPEN_READONLY;
        break;
    case OpenMode::ReadWrite:
        flags |= SQLITE_OPEN_READWRITE;
        break;
    case OpenMode::Create:
        flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        break;
    }

    sqlite3 *db = nullptr;
    const QByteArray utf8 = path.toUtf8();
    const int rc = sqlite3_open_v2(utf8.constData(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        Error error = closedError(path);
        error.code = rc;
        if (db) {
            error.message = QString::fromUtf8(sqlite3_errmsg(db));
            sqlite3_close_v2(db);
        }
        return error;
    }
    sqlite3_busy_timeout(db, 5000);

    auto state = std::make_shared<State>();
    state->db = db;
    state->path = path;
    return Connection(std::move(state));
}

Error Connection::makeError(const State &state, int code)
{
    Error error;
    error.code = code != 0 ? code : (state.db ? sqlite3_extended_errcode(state.db) : 0);
    error.message = state.db ? QString::fromUtf8(sqlite3_errmsg(state.db))
                             : QStringLiteral("Connection is closed");
    error.path = state.path;
    return error;
}

Status Connection::execOnState(const std::shared_ptr<State> &state, const QString &sql)
{
    if (!state)
        return Status::fail(closedError());

    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->closed || !state->db)
        return Status::fail(closedError(state->path));

    char *rawError = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int rc = sqlite3_exec(state->db, utf8.constData(), nullptr, nullptr, &rawError);
    if (rc == SQLITE_OK)
        return Status::ok();

    Error error = makeError(*state, rc);
    if (rawError)
        error.message = QString::fromUtf8(rawError);
    sqlite3_free(rawError);
    return Status::fail(std::move(error));
}

bool Connection::isOpen() const
{
    if (!state_)
        return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->db != nullptr && !state_->closed;
}

QString Connection::path() const
{
    return state_ ? state_->path : QString();
}

Status Connection::exec(const QString &sql)
{
    return execOnState(state_, sql);
}

Status Connection::execScript(const QString &sql)
{
    return execOnState(state_, sql);
}

Result<Statement> Connection::prepare(const QString &sql)
{
    if (!state_)
        return closedError();

    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return closedError(state_->path);

    sqlite3_stmt *stmt = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int rc = sqlite3_prepare_v2(state_->db, utf8.constData(), utf8.size(), &stmt, nullptr);
    if (rc != SQLITE_OK)
        return makeError(*state_, rc);
    if (!stmt)
        return makeError(*state_, SQLITE_ERROR);
    return Statement(state_, stmt);
}

qint64 Connection::lastInsertRowId() const
{
    if (!state_)
        return 0;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return 0;
    return sqlite3_last_insert_rowid(state_->db);
}

int Connection::changes() const
{
    if (!state_)
        return 0;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return 0;
    return sqlite3_changes(state_->db);
}

void Connection::close()
{
    if (!state_)
        return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed)
        return;
    state_->closed = true;
    if (state_->db) {
        // close_v2 marks the handle as a zombie while statements are
        // outstanding, so existing statements are finalized safely.
        sqlite3_close_v2(state_->db);
        state_->db = nullptr;
    }
}

QString Connection::quote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

Statement::Statement(std::shared_ptr<Connection::State> state, sqlite3_stmt *stmt)
    : state_(std::move(state))
    , stmt_(stmt)
{
}

Statement::~Statement()
{
    if (!stmt_)
        return;
    if (state_) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        sqlite3_finalize(stmt_);
    } else {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement &&other) noexcept
    : state_(std::move(other.state_))
    , stmt_(std::exchange(other.stmt_, nullptr))
{
}

Statement &Statement::operator=(Statement &&other) noexcept
{
    if (this == &other)
        return *this;
    if (stmt_) {
        if (state_) {
            std::lock_guard<std::mutex> lock(state_->mutex);
            sqlite3_finalize(stmt_);
        } else {
            sqlite3_finalize(stmt_);
        }
    }
    state_ = std::move(other.state_);
    stmt_ = std::exchange(other.stmt_, nullptr);
    return *this;
}

Error Statement::errorFromDb(int code) const
{
    if (!state_)
        return closedError();
    return Connection::makeError(*state_, code);
}

Status Statement::bind(int index, std::nullptr_t)
{
    if (!state_)
        return Status::fail(closedError());
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return Status::fail(closedError(state_->path));
    const int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

Status Statement::bind(int index, int value)
{
    return bind(index, static_cast<qint64>(value));
}

Status Statement::bind(int index, qint64 value)
{
    if (!state_)
        return Status::fail(closedError());
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return Status::fail(closedError(state_->path));
    const int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

Status Statement::bind(int index, double value)
{
    if (!state_)
        return Status::fail(closedError());
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return Status::fail(closedError(state_->path));
    const int rc = sqlite3_bind_double(stmt_, index, value);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

Status Statement::bind(int index, const QString &value)
{
    if (!state_)
        return Status::fail(closedError());
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return Status::fail(closedError(state_->path));
    const QByteArray utf8 = value.toUtf8();
    const int rc = sqlite3_bind_text64(stmt_, index, utf8.constData(),
                                       static_cast<sqlite3_uint64>(utf8.size()),
                                       SQLITE_TRANSIENT, SQLITE_UTF8);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

Status Statement::bind(int index, const QByteArray &blob)
{
    if (!state_)
        return Status::fail(closedError());
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return Status::fail(closedError(state_->path));
    // SQLITE_STATIC: the caller keeps blob alive until step() returns.
    const int rc = sqlite3_bind_blob64(stmt_, index, blob.constData(),
                                       static_cast<sqlite3_uint64>(blob.size()),
                                       SQLITE_STATIC);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

Result<bool> Statement::step()
{
    if (!state_)
        return closedError();
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return closedError(state_->path);

    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;
    return errorFromDb(rc);
}

Status Statement::exec()
{
    while (true) {
        auto result = step();
        if (!result)
            return Status::fail(result.error());
        if (!result.value())
            return Status::ok();
    }
}

Status Statement::reset()
{
    if (!state_)
        return Status::fail(closedError());
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->db)
        return Status::fail(closedError(state_->path));
    const int rc = sqlite3_reset(stmt_);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

int Statement::columnCount() const
{
    if (!state_)
        return 0;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed)
        return 0;
    return sqlite3_column_count(stmt_);
}

bool Statement::isNull(int column) const
{
    if (!state_)
        return true;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed)
        return true;
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

qint64 Statement::columnInt64(int column) const
{
    if (!state_)
        return 0;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed)
        return 0;
    return sqlite3_column_int64(stmt_, column);
}

double Statement::columnDouble(int column) const
{
    if (!state_)
        return 0;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed)
        return 0;
    return sqlite3_column_double(stmt_, column);
}

QString Statement::columnText(int column) const
{
    if (!state_)
        return {};
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed)
        return {};
    const auto *text = sqlite3_column_text(stmt_, column);
    if (!text)
        return {};
    return QString::fromUtf8(reinterpret_cast<const char *>(text),
                             sqlite3_column_bytes(stmt_, column));
}

QByteArray Statement::columnBlob(int column) const
{
    if (!state_)
        return {};
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed)
        return {};
    const void *data = sqlite3_column_blob(stmt_, column);
    const int size = sqlite3_column_bytes(stmt_, column);
    if (!data || size <= 0)
        return {};
    return QByteArray(static_cast<const char *>(data), size);
}

Result<Transaction> Transaction::begin(Connection &connection)
{
    Status status = connection.exec(QStringLiteral("BEGIN"));
    if (!status)
        return status.error();
    return Transaction(connection.state_);
}

Transaction::Transaction(std::shared_ptr<Connection::State> state)
    : state_(std::move(state))
{
}

Transaction::~Transaction()
{
    if (!state_)
        return;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->closed && state_->db)
        sqlite3_exec(state_->db, "ROLLBACK", nullptr, nullptr, nullptr);
}

Transaction::Transaction(Transaction &&other) noexcept
    : state_(std::move(other.state_))
{
}

Transaction &Transaction::operator=(Transaction &&other) noexcept
{
    if (this == &other)
        return *this;
    if (state_) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->closed && state_->db)
            sqlite3_exec(state_->db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    state_ = std::move(other.state_);
    return *this;
}

Status Transaction::commit()
{
    if (!state_)
        return Status::ok();
    auto state = std::move(state_);
    return Connection::execOnState(state, QStringLiteral("COMMIT"));
}

} // namespace board
