#include "session_cache.h"

#include "logging.h"
#include "util/process.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace cache {
namespace {

constexpr int kCacheUserVersion = 1;
constexpr int kStaleDays = 7;
// The cache is disposable, so a lock is never worth waiting for: the
// board connections keep SQLite's default five seconds, the cache does
// not (a busy cache must not stall a decode worker).
constexpr int kBusyTimeoutMs = 250;
const char *const kSchema = R"(
    CREATE TABLE IF NOT EXISTS blobs (
        kind TEXT NOT NULL,
        key TEXT NOT NULL,
        format TEXT,
        data BLOB,
        PRIMARY KEY (kind, key)
    )
)";

// Suffixes SQLite may leave next to the database.
const QStringList &sidecars()
{
    static const QStringList suffixes{QString(), QStringLiteral("-journal"), QStringLiteral("-wal"),
                                      QStringLiteral("-shm")};
    return suffixes;
}

void removeFileAndSidecars(const QString &path)
{
    for (const QString &suffix : sidecars())
        QFile::remove(path + suffix);
}

QString newCachePath(const QString &dir)
{
    const QString name = QStringLiteral("session-%1-%2.cachedb")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    return QDir(dir).filePath(name);
}

} // namespace

SessionCache::SessionCache(QString path, bool enabled)
    : path_(std::move(path))
    , enabled_(enabled)
{
}

SessionCache::~SessionCache()
{
    closeStorage();
    if (!path_.isEmpty())
        removeFileAndSidecars(path_);
}

std::shared_ptr<SessionCache> SessionCache::create(const QString &dir)
{
    if (!QDir().mkpath(dir)) {
        auto unavailable =
            std::shared_ptr<SessionCache>(new SessionCache(newCachePath(dir), false));
        logging::warn(QStringLiteral("Session cache unavailable; keeping data in memory"),
                      {{QStringLiteral("path"), unavailable->path()},
                       {QStringLiteral("step"), QStringLiteral("mkdir")}});
        return unavailable;
    }
    return createAt(newCachePath(dir));
}

std::shared_ptr<SessionCache> SessionCache::createAt(const QString &path)
{
    // The connection opens on first use, so startup never waits on the
    // disk (see ensureOpen).
    return std::shared_ptr<SessionCache>(new SessionCache(path, true));
}

bool SessionCache::isAvailable() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_ && !failed_;
}

bool SessionCache::ensureOpen()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_)
        return true;
    if (!enabled_ || failed_)
        return false;
    if (!openStorage()) {
        failed_ = true;
        return false;
    }
    open_ = true;
    return true;
}

bool SessionCache::openStorage()
{
    auto connection = board::Connection::open(path_, board::Connection::OpenMode::Create);
    if (!connection) {
        fail(QStringLiteral("open"), connection.error());
        return false;
    }
    connection_ = connection.take();
    for (const QString &pragma :
         {QStringLiteral("PRAGMA busy_timeout=%1").arg(kBusyTimeoutMs),
          QStringLiteral("PRAGMA journal_mode=OFF"), QStringLiteral("PRAGMA synchronous=OFF"),
          QStringLiteral("PRAGMA cache_size=-2000")}) {
        if (board::Status status = connection_.exec(pragma); !status) {
            fail(QStringLiteral("pragma"), status.error());
            return false;
        }
    }
    if (!ensureSchema())
        return false;
    return true;
}

bool SessionCache::ensureSchema()
{
    int stored = 0;
    {
        // Finalize the statement before the recreate below: a stepped
        // statement keeps a read lock (and, on Windows, the file itself)
        // alive, so closing the connection would not release it.
        auto version = connection_.prepare(QStringLiteral("PRAGMA user_version"));
        if (!version) {
            fail(QStringLiteral("schema"), version.error());
            return false;
        }
        auto row = version.value().step();
        if (!row) {
            fail(QStringLiteral("schema"), row.error());
            return false;
        }
        stored = row.value() ? int(version.value().columnInt64(0)) : 0;
    }
    if (stored != kCacheUserVersion) {
        // The cache is disposable: start over instead of migrating, so
        // there is never a half-migrated state to reason about.
        connection_.close();
        removeFileAndSidecars(path_);
        auto reopened = board::Connection::open(path_, board::Connection::OpenMode::Create);
        if (!reopened) {
            fail(QStringLiteral("recreate"), reopened.error());
            return false;
        }
        connection_ = reopened.take();
    }
    if (board::Status status = connection_.execScript(QString::fromLatin1(kSchema)); !status) {
        fail(QStringLiteral("schema"), status.error());
        return false;
    }
    if (board::Status status =
            connection_.exec(QStringLiteral("PRAGMA user_version=%1").arg(kCacheUserVersion));
        !status) {
        fail(QStringLiteral("schema"), status.error());
        return false;
    }
    return true;
}

void SessionCache::fail(const QString &step, const board::Error &error)
{
    logging::warn(QStringLiteral("Session cache unavailable; keeping data in memory"),
                  {{QStringLiteral("path"), path_},
                   {QStringLiteral("step"), step},
                   {QStringLiteral("code"), error.code},
                   {QStringLiteral("error"), error.message}});
}

void SessionCache::closeStorage()
{
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    connection_ = {};
}

std::optional<QByteArray> SessionCache::get(const QString &kind, const QString &key)
{
    if (!ensureOpen())
        return std::nullopt;
    auto statement =
        connection_.prepare(QStringLiteral("SELECT data FROM blobs WHERE kind=? AND key=?"));
    if (!statement)
        return std::nullopt;
    if (board::Status status = statement.value().bind(1, kind); !status)
        return std::nullopt;
    if (board::Status status = statement.value().bind(2, key); !status)
        return std::nullopt;
    auto row = statement.value().step();
    if (!row.isOk() || !row.value())
        return std::nullopt;
    return statement.value().columnBlob(0);
}

bool SessionCache::put(const QString &kind, const QString &key, const QString &format,
                       const QByteArray &data)
{
    if (!ensureOpen())
        return false;
    auto statement = connection_.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blobs (kind, key, format, data) VALUES (?, ?, ?, ?)"));
    if (!statement)
        return false;
    if (board::Status status = statement.value().bind(1, kind); !status)
        return false;
    if (board::Status status = statement.value().bind(2, key); !status)
        return false;
    if (board::Status status = statement.value().bind(3, format); !status)
        return false;
    if (board::Status status = statement.value().bind(4, data); !status)
        return false;
    return statement.value().exec().isOk();
}

bool SessionCache::remove(const QString &kind, const QString &key)
{
    if (!ensureOpen())
        return false;
    auto statement =
        connection_.prepare(QStringLiteral("DELETE FROM blobs WHERE kind=? AND key=?"));
    if (!statement)
        return false;
    if (board::Status status = statement.value().bind(1, kind); !status)
        return false;
    if (board::Status status = statement.value().bind(2, key); !status)
        return false;
    return statement.value().exec().isOk();
}

qint64 SessionCache::fileBytes() const
{
    return QFileInfo(path_).size();
}

void SessionCache::sweepStale(const QString &dir)
{
    const QDir cacheDir(dir);
    const QStringList names =
        cacheDir.entryList({QStringLiteral("session-*.cachedb*")}, QDir::Files, QDir::Name);
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &name : names) {
        const QStringList parts = name.split(QLatin1Char('-'));
        bool parsed = false;
        const qint64 pid = parts.size() > 1 ? parts.at(1).toLongLong(&parsed) : 0;
        const bool old = QFileInfo(cacheDir.filePath(name)).lastModified().daysTo(now) > kStaleDays;
        if (!parsed || !util::isProcessAlive(pid) || old)
            QFile::remove(cacheDir.filePath(name));
    }
}

} // namespace cache
