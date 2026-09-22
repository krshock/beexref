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

SessionCache::SessionCache(QString path)
    : path_(std::move(path))
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
        auto unavailable = std::shared_ptr<SessionCache>(new SessionCache(newCachePath(dir)));
        logging::warn(QStringLiteral("Session cache unavailable; keeping data in memory"),
                      {{QStringLiteral("path"), unavailable->path()}});
        return unavailable;
    }
    return createAt(newCachePath(dir));
}

std::shared_ptr<SessionCache> SessionCache::createAt(const QString &path)
{
    auto cache = std::shared_ptr<SessionCache>(new SessionCache(path));
    if (!cache->openStorage()) {
        logging::warn(QStringLiteral("Session cache unavailable; keeping data in memory"),
                      {{QStringLiteral("path"), cache->path()}});
        return cache;
    }
    // Only the owner may read the payloads.
    QFile::setPermissions(cache->path(), QFile::ReadOwner | QFile::WriteOwner);
    return cache;
}

bool SessionCache::openStorage()
{
    auto connection = board::Connection::open(path_, board::Connection::OpenMode::Create);
    if (!connection) {
        return false;
    }
    connection_ = connection.take();
    for (const QString &pragma :
         {QStringLiteral("PRAGMA busy_timeout=5000"), QStringLiteral("PRAGMA journal_mode=OFF"),
          QStringLiteral("PRAGMA synchronous=OFF"), QStringLiteral("PRAGMA cache_size=-2000")}) {
        if (board::Status status = connection_.exec(pragma); !status)
            return false;
    }
    if (!ensureSchema())
        return false;
    available_ = true;
    return true;
}

bool SessionCache::ensureSchema()
{
    auto version = connection_.prepare(QStringLiteral("PRAGMA user_version"));
    if (!version)
        return false;
    auto row = version.value().step();
    const int stored = (row.isOk() && row.value()) ? int(version.value().columnInt64(0)) : 0;
    if (stored != kCacheUserVersion) {
        // The cache is disposable: start over instead of migrating, so
        // there is never a half-migrated state to reason about.
        connection_.close();
        removeFileAndSidecars(path_);
        auto reopened = board::Connection::open(path_, board::Connection::OpenMode::Create);
        if (!reopened)
            return false;
        connection_ = reopened.take();
    }
    if (board::Status status = connection_.execScript(QString::fromLatin1(kSchema)); !status)
        return false;
    return connection_.exec(QStringLiteral("PRAGMA user_version=%1").arg(kCacheUserVersion)).isOk();
}

void SessionCache::closeStorage()
{
    available_ = false;
    connection_ = {};
}

std::optional<QByteArray> SessionCache::get(const QString &kind, const QString &key)
{
    if (!available_)
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
    if (!available_)
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
    if (!available_)
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
