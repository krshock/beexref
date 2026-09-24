#pragma once

#include "doc/source.h"
#include "levels.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSemaphore>
#include <QSize>
#include <QThread>
#include <QVector>

#include <atomic>
#include <memory>

namespace cache {
class SessionCache;
}

namespace ui {

// Decodes item levels on dedicated worker threads. Requests carry the
// item's immutable source, never the item, so the UI thread may keep
// editing (or delete the item) while a decode runs. Results are
// delivered on the loader's thread (the UI thread) through queued
// signal delivery.
//
// The queue is explicit: the band orders it (Selected before Visible
// before Deferred) and requests run FIFO inside a band. That order is
// the loader's contract, independent of how many workers consume it.
//
// When a session cache is attached, decoded levels are read from it
// first and written back after a decode, so repeat levels (culling
// back to the floor, reopening the same board) cost a small read
// instead of a full decode. Decoded levels are additionally kept in a
// bounded in-RAM LRU shared by the whole pool.
class LevelLoader : public QObject
{
    Q_OBJECT

public:
    explicit LevelLoader(QObject *parent = nullptr);
    ~LevelLoader() override;

    void setLevelCache(std::shared_ptr<cache::SessionCache> cache);

    // How many worker threads decode. Always at least one; the pool is
    // resized in place, without dropping queued requests.
    void setThreads(int threads);
    int threads() const;

    // Cap the pool's in-RAM LRU of decoded levels at this many bytes.
    // 0 disables the RAM cache (levels then come from the session cache
    // or a decode).
    void setRamCacheBudget(qint64 bytes);
    // Bytes currently held by that LRU.
    qint64 ramCacheBytes() const;

    // Schedules a cache-cost recomputation, evicting over-budget levels.
    // The cost is delayed, because a cached image shares its pixels with
    // the installed level and only becomes resident some time after the
    // result was delivered. Callers call this once a result is installed
    // (or overridden by an equal level).
    void reconsiderRamCache();

    // requestId is the caller's token; it is echoed back with the
    // result. targetSize is the wanted pixel size (aspect preserved);
    // quality is "fast" (single step) or "smooth" (progressive halving).
    // cacheKey names the level in the caches; empty disables caching.
    // coalesceKey identifies the item: a newer request for the same key
    // supersedes a queued one, so a zoom burst decodes only the latest
    // fraction, as the reference worker does.
    //
    // The band decides the queue order: a request in a higher band runs
    // before lower-band work, FIFO within a band. A superseded request
    // is still dropped for free before it starts, and a decode already
    // running is never interrupted.
    void request(quint64 requestId, doc::SourcePtr source, const QSize &targetSize,
                 const QString &quality, const QString &cacheKey = {},
                 const QString &coalesceKey = {}, RequestBand band = RequestBand::Visible);

    // Stops the workers and drops pending requests. Must be called
    // before the documents the sources read from are closed.
    void shutdown();

    // Returns the workers' freed heap memory to the OS. Decode
    // transients (full-size decode buffers, scaled copies) live in the
    // workers' malloc arenas, which a trim from the UI thread cannot
    // reach.
    void releaseMemory();

signals:
    void levelReady(quint64 requestId, const QImage &image);
    void levelFailed(quint64 requestId);
    // The request was superseded by a newer one for the same item.
    void levelCancelled(quint64 requestId);

private:
    class Worker;
    struct Job
    {
        quint64 requestId = 0;
        doc::SourcePtr source;
        QSize targetSize;
        QString quality;
        QString cacheKey;
        QString coalesceKey;
        RequestBand band = RequestBand::Visible;
        // Insertion order, so a band keeps FIFO across workers.
        quint64 seq = 0;
    };

    struct Shared
    {
        QMutex mutex;
        // Newest request id per item, for coalescing.
        QHash<QString, quint64> latest;
        // Decoded-level LRU, shared by the pool. Image buffers are
        // shared with the installed levels, so holding one keeps its
        // pixels alive at no extra cost until the UI releases them.
        struct CachedLevel
        {
            QImage image;
            qint64 bytes = 0;
            quint64 lastUsed = 0;
        };
        QHash<QString, CachedLevel> ramCache;
        quint64 ramCacheUse = 0;
        qint64 ramCacheBudget = 0;
        qint64 ramCacheBytes = 0;
    };

    // Pops the next job (highest band, then oldest); false when stopping.
    bool takeJob(Job &job, const std::atomic<bool> &stop);
    // Drops a queued job for the same item, reporting it as cancelled.
    // Caller holds queueMutex_.
    void dropQueuedLocked(const QString &coalesceKey);
    // Runs one job: RAM cache, session cache, decode, publish.
    void runJob(const Job &job, Worker *worker);
    // Inserts a level into the LRU when caching is on.
    void publishRamCache(const Job &job, const QImage &image);
    // Recomputes the LRU cost and evicts to the budget.
    void updateRamCacheCost();

    mutable QMutex queueMutex_;
    QVector<Job> queue_;
    quint64 nextSeq_ = 1;
    QSemaphore work_;
    // Delayed RAM-cache cost pass on the UI thread: the cached buffer
    // only becomes resident once the caller released its copy.
    QTimer *cacheCostTimer_ = nullptr;

    QVector<QThread *> threads_;
    QVector<Worker *> workers_;
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::shared_ptr<cache::SessionCache> cache_;
    bool shutdown_ = false;
};

} // namespace ui
