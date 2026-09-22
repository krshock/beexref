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

Error errorFrom(sqlite3 *db, const QString &path, int code)
{
    Error error;
    error.code = code != 0 ? code : (db ? sqlite3_extended_errcode(db) : 0);
    error.message = db ? QString::fromUtf8(sqlite3_errmsg(db)) : QString();
    error.path = path;
    return error;
}

} // namespace

Database::Database(sqlite3 *db, QString path)
    : db_(db)
    , path_(std::move(path))
{
}

Database::~Database()
{
    if (db_)
        sqlite3_close_v2(db_);
}

Database::Database(Database &&other) noexcept
    : db_(std::exchange(other.db_, nullptr))
    , path_(std::move(other.path_))
    , mutex_(std::move(other.mutex_))
{
}

Database &Database::operator=(Database &&other) noexcept
{
    if (this == &other)
        return *this;
    if (db_)
        sqlite3_close_v2(db_);
    db_ = std::exchange(other.db_, nullptr);
    path_ = std::move(other.path_);
    mutex_ = std::move(other.mutex_);
    return *this;
}

Result<Database> Database::open(const QString &path, OpenMode mode)
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
        Error error = errorFrom(db, path, rc);
        if (db)
            sqlite3_close_v2(db);
        return error;
    }
    sqlite3_busy_timeout(db, 5000);
    return Database(db, path);
}

Error Database::errorFromDb(int code) const
{
    return errorFrom(db_, path_, code);
}

Status Database::exec(const QString &sql)
{
    auto statement = prepare(sql);
    if (!statement)
        return statement.error();
    return statement.value().exec();
}

Status Database::execScript(const QString &sql)
{
    std::lock_guard<std::mutex> lock(*mutex_);
    char *rawError = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int rc = sqlite3_exec(db_, utf8.constData(), nullptr, nullptr, &rawError);
    if (rc == SQLITE_OK)
        return Status::ok();
    Error error = errorFromDb(rc);
    if (rawError)
        error.message = QString::fromUtf8(rawError);
    sqlite3_free(rawError);
    return Status::fail(std::move(error));
}

Result<Statement> Database::prepare(const QString &sql)
{
    std::lock_guard<std::mutex> lock(*mutex_);
    sqlite3_stmt *stmt = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int rc = sqlite3_prepare_v2(db_, utf8.constData(), utf8.size(), &stmt, nullptr);
    if (rc != SQLITE_OK)
        return errorFromDb(rc);
    if (!stmt)
        return errorFromDb(SQLITE_ERROR);
    return Statement(db_, stmt, mutex_);
}

qint64 Database::lastInsertRowId() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return sqlite3_last_insert_rowid(db_);
}

int Database::changes() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return sqlite3_changes(db_);
}

QString Database::quote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

Statement::Statement(sqlite3 *db, sqlite3_stmt *stmt, std::shared_ptr<std::mutex> mutex)
    : db_(db)
    , stmt_(stmt)
    , mutex_(std::move(mutex))
{
}

Statement::~Statement()
{
    if (stmt_)
        sqlite3_finalize(stmt_);
}

Statement::Statement(Statement &&other) noexcept
    : db_(std::exchange(other.db_, nullptr))
    , stmt_(std::exchange(other.stmt_, nullptr))
    , mutex_(std::move(other.mutex_))
    , hasRow_(std::exchange(other.hasRow_, false))
{
}

Statement &Statement::operator=(Statement &&other) noexcept
{
    if (this == &other)
        return *this;
    if (stmt_)
        sqlite3_finalize(stmt_);
    db_ = std::exchange(other.db_, nullptr);
    stmt_ = std::exchange(other.stmt_, nullptr);
    mutex_ = std::move(other.mutex_);
    hasRow_ = std::exchange(other.hasRow_, false);
    return *this;
}

Error Statement::errorFromDb(int code) const
{
    QString path;
    if (db_) {
        if (const char *filename = sqlite3_db_filename(db_, "main"))
            path = QString::fromUtf8(filename);
    }
    return errorFrom(db_, path, code);
}

Status Statement::bind(int index, std::nullptr_t)
{
    std::lock_guard<std::mutex> lock(*mutex_);
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
    std::lock_guard<std::mutex> lock(*mutex_);
    const int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

Status Statement::bind(int index, double value)
{
    std::lock_guard<std::mutex> lock(*mutex_);
    const int rc = sqlite3_bind_double(stmt_, index, value);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

Status Statement::bind(int index, const QString &value)
{
    std::lock_guard<std::mutex> lock(*mutex_);
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
    std::lock_guard<std::mutex> lock(*mutex_);
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
    std::lock_guard<std::mutex> lock(*mutex_);
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        hasRow_ = true;
        return true;
    }
    if (rc == SQLITE_DONE) {
        hasRow_ = false;
        return false;
    }
    hasRow_ = false;
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
    std::lock_guard<std::mutex> lock(*mutex_);
    hasRow_ = false;
    const int rc = sqlite3_reset(stmt_);
    if (rc != SQLITE_OK)
        return Status::fail(errorFromDb(rc));
    return Status::ok();
}

int Statement::columnCount() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return sqlite3_column_count(stmt_);
}

bool Statement::isNull(int column) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

qint64 Statement::columnInt64(int column) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return sqlite3_column_int64(stmt_, column);
}

double Statement::columnDouble(int column) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return sqlite3_column_double(stmt_, column);
}

QString Statement::columnText(int column) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    const auto *text = sqlite3_column_text(stmt_, column);
    if (!text)
        return {};
    return QString::fromUtf8(reinterpret_cast<const char *>(text),
                             sqlite3_column_bytes(stmt_, column));
}

QByteArray Statement::columnBlob(int column) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    const void *data = sqlite3_column_blob(stmt_, column);
    const int size = sqlite3_column_bytes(stmt_, column);
    if (!data || size <= 0)
        return {};
    return QByteArray(static_cast<const char *>(data), size);
}

Result<Transaction> Transaction::begin(Database &db)
{
    Status status = db.exec(QStringLiteral("BEGIN"));
    if (!status)
        return status.error();
    return Transaction(&db);
}

Transaction::~Transaction()
{
    if (db_)
        db_->exec(QStringLiteral("ROLLBACK"));
}

Transaction::Transaction(Transaction &&other) noexcept
    : db_(std::exchange(other.db_, nullptr))
{
}

Transaction &Transaction::operator=(Transaction &&other) noexcept
{
    if (this == &other)
        return *this;
    if (db_)
        db_->exec(QStringLiteral("ROLLBACK"));
    db_ = std::exchange(other.db_, nullptr);
    return *this;
}

Status Transaction::commit()
{
    if (!db_)
        return Status::ok();
    Database *db = db_;
    db_ = nullptr;
    return db->exec(QStringLiteral("COMMIT"));
}

} // namespace board
