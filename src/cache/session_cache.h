#pragma once

#include "board/sqlite.h"

#include <QByteArray>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>

namespace cache {

// One disposable SQLite file per run holding derived data that would
// otherwise sit in RAM: decoded LOD levels and the encoded payloads of
// items that left the board but can still be restored with Undo.
//
// Best-effort by design: if the file cannot be created or used, every
// operation becomes a no-op and callers fall back to their in-memory
// path. Entries above a fixed size (16 MB) are not written at all, so
// one huge level or payload cannot hold the disk for the session. The
// connection opens lazily on first use, so a slow or locked disk never
// delays startup; a failed open disables the cache for the rest of the
// session. The file is deleted when the cache is destroyed (normally at
// exit); files left behind by a crash are swept on the next start.
//
// Thread-safe: the underlying connection serializes its operations, so
// the decode worker may read and write while the UI thread does too.
class SessionCache
{
public:
    // Creates the cache in dir; never fails, an unusable cache reports
    // isAvailable() == false.
    static std::shared_ptr<SessionCache> create(const QString &dir);

    // Creates the cache at an explicit path (tests, embedded use). The
    // file is still deleted when the cache is destroyed.
    static std::shared_ptr<SessionCache> createAt(const QString &path);

    ~SessionCache();

    SessionCache(const SessionCache &) = delete;
    SessionCache &operator=(const SessionCache &) = delete;

    // True while the cache is enabled and its storage has not failed;
    // the first get/put/remove opens the file.
    bool isAvailable() const;
    const QString &path() const { return path_; }

    // Kinds namespace the shared table: "lod" and "undo".
    std::optional<QByteArray> get(const QString &kind, const QString &key);
    bool put(const QString &kind, const QString &key, const QString &format,
             const QByteArray &data);
    bool remove(const QString &kind, const QString &key);

    // Size of the cache file on disk.
    qint64 fileBytes() const;

    // Removes session-*.cachedb files (and their sidecars) left behind
    // by dead processes, or older than a week.
    static void sweepStale(const QString &dir);

private:
    SessionCache(QString path, bool enabled);

    // Opens the storage on first use; false once the cache is disabled.
    bool ensureOpen();
    bool openStorage();
    bool ensureSchema();
    void fail(const QString &step, const board::Error &error);
    void closeStorage();

    QString path_;
    mutable std::mutex mutex_;
    board::Connection connection_;
    bool enabled_ = true;
    bool open_ = false;
    bool failed_ = false;
};

} // namespace cache
