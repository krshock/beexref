#include "level_loader.h"

#include "cache/session_cache.h"
#include "doc/image_io.h"
#include "util/memory.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <utility>

namespace ui {
namespace {

// Short delay before the RAM-cache cost is recomputed, so the UI thread
// is done with the result it just received and the reference count
// reflects what is really resident.
constexpr int kCacheCostDelayMs = 200;

// The evicted levels waiting for the cache writer are capped, so a burst
// of evictions cannot hold a large share of RAM hostage for the disk.
constexpr qint64 kMaxPendingCacheWriteBytes = 64 * 1024 * 1024;

QImage decodeLevel(const doc::SourcePtr &source, const QSize &targetSize, const QString &quality)
{
    if (!source || !source->isValid())
        return {};

    const QByteArray bytes = source->bytes();
    if (bytes.isEmpty())
        return {};

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};

    QImageReader reader(&buffer);
    // Stored bytes are upright: the app bakes orientation when an
    // image enters the app, not when a level is decoded.
    reader.setAutoTransform(false);
    if (targetSize.isValid() && !targetSize.isEmpty())
        reader.setScaledSize(targetSize);

    QImage image = reader.read();
    if (image.isNull())
        return {};

    if (!targetSize.isValid() || targetSize.isEmpty() || image.size() == targetSize)
        return image;

    if (quality == QLatin1String("fast")) {
        return image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Progressive halving keeps every bilinear step within a 2x ratio,
    // matching the smooth downscale.
    int width = image.width();
    int height = image.height();
    while (width / 2 >= targetSize.width() && height / 2 >= targetSize.height()) {
        width /= 2;
        height /= 2;
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (image.size() != targetSize)
        image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace

// A pool member: its own thread, its own decode arena and trim timer.
// The queue and the level LRU are shared across the pool.
class LevelLoader::Worker : public QObject
{
public:
    Worker(LevelLoader *loader)
        : loader_(loader)
    {
    }

    // Marks this worker for retirement; it stops after its current job.
    void retire() { retire_.store(true); }

    // Runs in the worker thread until shutdown or retirement.
    void run()
    {
        trimTimer_ = new QTimer(this);
        trimTimer_->setSingleShot(true);
        trimTimer_->setInterval(kTrimCoalesceMs);
        connect(trimTimer_, &QTimer::timeout, this, []() { util::releaseFreeMemory(); });

        while (!retire_.load()) {
            // Run any posted calls (the trim timer's control events)
            // before blocking again.
            QCoreApplication::processEvents();
            Job job;
            if (!loader_->takeJob(job, retire_))
                break;
            loader_->runJob(job, this);
        }
        // Leave the thread cleanly: the timer is stopped and deleted on
        // the thread that owns it, before run() returns.
        trimTimer_->stop();
        delete trimTimer_;
        trimTimer_ = nullptr;
        QThread::currentThread()->quit();
    }

    // Restarts the timer, so a burst coalesces into a single trim. The
    // caller posts this to the worker's thread.
    void scheduleTrim()
    {
        if (trimTimer_)
            trimTimer_->start();
    }

    // Long enough to span the decode of one burst on this thread, short
    // enough that transients do not linger until the idle release.
    static constexpr int kTrimCoalesceMs = 50;

private:
    LevelLoader *loader_ = nullptr;
    QTimer *trimTimer_ = nullptr;
    std::atomic<bool> retire_{false};
};

LevelLoader::LevelLoader(QObject *parent)
    : QObject(parent)
{
    // Lives on the UI thread. A shared image only becomes a resident
    // cost after the caller dropped its copy, hence the delay.
    cacheCostTimer_ = new QTimer(this);
    cacheCostTimer_->setSingleShot(true);
    cacheCostTimer_->setInterval(kCacheCostDelayMs);
    connect(cacheCostTimer_, &QTimer::timeout, this, [this]() { updateRamCacheCost(); });
    // Idle settle: after the configured quiet period, release part of the
    // off-screen cache and keep doing so while the app stays idle.
    settleTimer_ = new QTimer(this);
    settleTimer_->setSingleShot(true);
    connect(settleTimer_, &QTimer::timeout, this, [this]() { settleStep(); });
    // The default pool size; LodManager applies the user's setting.
    setThreads(3);
}

LevelLoader::~LevelLoader()
{
    shutdown();
}

void LevelLoader::setThreads(int threads)
{
    threads = qMax(1, threads);
    if (shutdown_)
        return;
    // Retire the surplus workers: each finishes its current decode, sees
    // the flag and leaves; the thread and object are then reclaimed.
    while (threads_.size() > threads) {
        QThread *thread = threads_.takeLast();
        Worker *worker = workers_.takeLast();
        // Retirement is seen within the queue poll interval.
        worker->retire();
        thread->wait();
        delete worker;
        delete thread;
    }
    while (threads_.size() < threads) {
        auto *thread = new QThread;
        thread->setObjectName(QStringLiteral("level-loader"));
        auto *worker = new Worker(this);
        worker->moveToThread(thread);
        connect(thread, &QThread::started, worker, &Worker::run);
        threads_.append(thread);
        workers_.append(worker);
        thread->start();
        // The new worker waits on the shared semaphore like the rest.
        work_.release();
    }
}

int LevelLoader::threads() const
{
    QMutexLocker locker(&queueMutex_);
    return threads_.size();
}

void LevelLoader::setLevelCache(std::shared_ptr<cache::SessionCache> cache)
{
    QMutexLocker locker(&queueMutex_);
    cache_ = std::move(cache);
}

void LevelLoader::setRamCacheBudget(qint64 bytes)
{
    {
        QMutexLocker locker(&shared_->mutex);
        shared_->ramCacheBudget = qMax(qint64(0), bytes);
    }
    reconsiderRamCache();
}

qint64 LevelLoader::ramCacheBytes() const
{
    QMutexLocker locker(&shared_->mutex);
    return shared_->ramCacheBytes;
}

void LevelLoader::reconsiderRamCache()
{
    if (shutdown_)
        return;
    // Delayed on the UI thread: the cached buffer only becomes resident
    // once the caller released its copy. Re-armed, so a burst costs one
    // pass.
    cacheCostTimer_->start();
    noteActivity();
}

void LevelLoader::setSettlePolicy(int seconds, int percent, qint64 pinnedBytes)
{
    settleSeconds_ = qMax(0, seconds);
    settlePercent_ = qBound(0, percent, 50);
    pinnedBytes_ = qMax(qint64(0), pinnedBytes);
    if (settleSeconds_ <= 0 || settlePercent_ <= 0) {
        settleTimer_->stop();
        return;
    }
    settleTimer_->setInterval(settleSeconds_ * 1000);
    settleTimer_->start();
}

void LevelLoader::noteActivity()
{
    // New activity: stop any pending step and start the quiet period
    // again.
    if (settleTimer_ && settleSeconds_ > 0 && settlePercent_ > 0)
        settleTimer_->start();
}

void LevelLoader::settleStep()
{
    if (shutdown_ || settlePercent_ <= 0)
        return;
    qint64 bytes = 0;
    qint64 budget = 0;
    {
        QMutexLocker locker(&shared_->mutex);
        bytes = shared_->ramCacheBytes;
        budget = shared_->ramCacheBudget;
    }
    (void)budget;
    // The evictable part is everything above the pinned levels.
    const qint64 evictable = qMax(qint64(0), bytes - pinnedBytes_);
    if (evictable <= 0)
        return;
    const qint64 release = evictable * settlePercent_ / 100;
    if (release <= 0)
        return;
    evictToBytes(bytes - release, pinnedBytes_);
    // Return the freed pages to the OS, then schedule the next step.
    util::releaseFreeMemory();
    settleTimer_->start();
}

void LevelLoader::request(quint64 requestId, doc::SourcePtr source, const QSize &targetSize,
                          const QString &quality, const QString &cacheKey,
                          const QString &coalesceKey, RequestBand band)
{
    if (shutdown_)
        return;
    if (!coalesceKey.isEmpty()) {
        QMutexLocker locker(&shared_->mutex);
        shared_->latest.insert(coalesceKey, requestId);
    }
    {
        QMutexLocker locker(&queueMutex_);
        if (!coalesceKey.isEmpty())
            dropQueuedLocked(coalesceKey);
        Job job;
        job.requestId = requestId;
        job.source = std::move(source);
        job.targetSize = targetSize;
        job.quality = quality;
        job.cacheKey = cacheKey;
        job.coalesceKey = coalesceKey;
        job.band = band;
        job.seq = nextSeq_++;
        // Keep the queue in priority order, so any worker can take the
        // head without scanning.
        const auto at = std::lower_bound(
            queue_.cbegin(), queue_.cend(), job,
            [](const Job &a, const Job &b) {
                if (a.band != b.band)
                    return static_cast<int>(a.band) < static_cast<int>(b.band);
                return a.seq < b.seq;
            });
        queue_.insert(at - queue_.cbegin(), std::move(job));
    }
    work_.release();
    noteActivity();
}

bool LevelLoader::takeJob(Job &job, const std::atomic<bool> &stop)
{
    while (!shutdown_ && !stop.load()) {
        if (!work_.tryAcquire(1, 50))
            continue;
        QMutexLocker locker(&queueMutex_);
        if (shutdown_ || stop.load()) {
            work_.release();
            return false;
        }
        if (queue_.isEmpty())
            continue;
        job = queue_.takeFirst();
        return true;
    }
    return false;
}

void LevelLoader::dropQueuedLocked(const QString &coalesceKey)
{
    for (qsizetype i = queue_.size() - 1; i >= 0; --i) {
        if (queue_.at(i).coalesceKey == coalesceKey) {
            const quint64 id = queue_.at(i).requestId;
            queue_.removeAt(i);
            work_.tryAcquire(1);
            QMetaObject::invokeMethod(
                this, [this, id]() { emit levelCancelled(id); }, Qt::QueuedConnection);
            // One queued request per item.
            break;
        }
    }
}

bool LevelLoader::isCurrentRequest(const Job &job) const
{
    if (job.coalesceKey.isEmpty())
        return true;
    QMutexLocker locker(&shared_->mutex);
    return shared_->latest.value(job.coalesceKey) == job.requestId;
}

void LevelLoader::runJob(const Job &job, Worker *worker)
{
    // A newer request for the same item supersedes this one; it is
    // dropped before any decode work happens.
    if (!isCurrentRequest(job)) {
        emit levelCancelled(job.requestId);
        if (worker)
            worker->scheduleTrim();
        return;
    }

    std::shared_ptr<cache::SessionCache> cache;
    {
        QMutexLocker locker(&queueMutex_);
        cache = cache_;
    }
    const bool cacheable = cache && cache->isAvailable() && !job.cacheKey.isEmpty()
        && job.targetSize.isValid() && !job.targetSize.isEmpty();

    QImage image;
    // Cache first, lightest to heaviest: the pool's RAM LRU (a reference
    // to a buffer that may already be alive), the session cache (a PNG
    // read), then a decode.
    if (!job.cacheKey.isEmpty()) {
        QMutexLocker locker(&shared_->mutex);
        const auto hit = shared_->ramCache.constFind(job.cacheKey);
        if (hit != shared_->ramCache.cend() && hit->image.size() == job.targetSize) {
            image = hit->image;
            shared_->ramCache[job.cacheKey].lastUsed = ++shared_->ramCacheUse;
        }
    }
    if (image.isNull() && cacheable) {
        if (auto cached = cache->get(QStringLiteral("lod"), job.cacheKey)) {
            const QImage fromCache = QImage::fromData(*cached);
            if (!fromCache.isNull() && fromCache.size() == job.targetSize) {
                image = fromCache;
            } else {
                // A stale or corrupt entry: drop it.
                cache->remove(QStringLiteral("lod"), job.cacheKey);
            }
        }
    }
    if (image.isNull()) {
        image = decodeLevel(job.source, job.targetSize, job.quality);
        // With the RAM LRU in play, the disk copy is written when the
        // level is evicted (see evictToBytes), so a level that stays in
        // RAM never touches the disk. With the LRU off nothing ever
        // evicts, so the disk is the cache and gets the level now.
        if (!image.isNull() && cacheable && ramCacheDisabled()) {
            cache->put(QStringLiteral("lod"), job.cacheKey, QStringLiteral("png"),
                       doc::encodePng(image));
        }
    }
    if (image.isNull()) {
        emit levelFailed(job.requestId);
    } else {
        // The level is valid for the item whatever happens next, so it
        // enters the LRU and can serve the newer request; but only the
        // newest request gets an answer.
        publishRamCache(job, image);
        if (isCurrentRequest(job))
            emit levelReady(job.requestId, image);
        else
            emit levelCancelled(job.requestId);
    }
    if (worker)
        worker->scheduleTrim();
}

void LevelLoader::publishRamCache(const Job &job, const QImage &image)
{
    QMutexLocker locker(&shared_->mutex);
    if (shared_->ramCacheBudget <= 0 || job.cacheKey.isEmpty())
        return;
    Shared::CachedLevel entry;
    entry.image = image;
    entry.bytes = image.sizeInBytes();
    entry.lastUsed = ++shared_->ramCacheUse;
    shared_->ramCache.insert(job.cacheKey, entry);
}

void LevelLoader::updateRamCacheCost()
{
    qint64 budget = 0;
    {
        QMutexLocker locker(&shared_->mutex);
        budget = shared_->ramCacheBudget;
        if (budget <= 0) {
            shared_->ramCache.clear();
            shared_->ramCacheBytes = 0;
            return;
        }
        qint64 total = 0;
        for (auto it = shared_->ramCache.cbegin(); it != shared_->ramCache.cend(); ++it)
            total += it->image.sizeInBytes();
        shared_->ramCacheBytes = total;
    }
    // Nothing pinned here: eviction to the full budget.
    evictToBytes(budget, 0);
}

void LevelLoader::evictToBytes(qint64 target, qint64 floor)
{
    target = qMax(target, floor);
    // Collect the evicted levels under the lock, then hand them to a
    // worker: only levels that fall out of RAM reach the disk cache, and
    // the encoding and the write never run on the UI thread.
    QVector<QPair<QString, QImage>> evicted;
    {
        QMutexLocker locker(&shared_->mutex);
        while (shared_->ramCacheBytes > target) {
            auto oldest = shared_->ramCache.cend();
            for (auto it = shared_->ramCache.cbegin(); it != shared_->ramCache.cend(); ++it) {
                if (oldest == shared_->ramCache.cend() || it->lastUsed < oldest->lastUsed)
                    oldest = it;
            }
            if (oldest == shared_->ramCache.cend())
                break;
            shared_->ramCacheBytes -= oldest->bytes;
            evicted.append({oldest.key(), oldest->image});
            shared_->ramCache.erase(oldest);
        }
    }
    queueCacheWrites(std::move(evicted));
}

bool LevelLoader::ramCacheDisabled() const
{
    QMutexLocker locker(&shared_->mutex);
    return shared_->ramCacheBudget <= 0;
}

void LevelLoader::queueCacheWrites(QVector<QPair<QString, QImage>> entries)
{
    if (entries.isEmpty())
        return;
    std::shared_ptr<cache::SessionCache> cache;
    Worker *worker = nullptr;
    {
        QMutexLocker locker(&queueMutex_);
        cache = cache_;
        if (!workers_.isEmpty())
            worker = workers_.first();
    }
    if (!cache || !cache->isAvailable() || !worker)
        return;

    {
        QMutexLocker locker(&cacheWriteMutex_);
        for (auto &entry : entries) {
            // Best-effort and bounded: the disk copy must never hold a
            // large share of RAM hostage while waiting for the writer.
            if (pendingCacheWriteBytes_ >= kMaxPendingCacheWriteBytes)
                break;
            pendingCacheWriteBytes_ += entry.second.sizeInBytes();
            pendingCacheWrites_.append(std::move(entry));
        }
    }
    // The worker polls its queue every 50 ms, so a posted call runs even
    // while it is idle.
    QMetaObject::invokeMethod(worker, [this]() { flushCacheWrites(); }, Qt::QueuedConnection);
}

void LevelLoader::flushCacheWrites()
{
    QVector<QPair<QString, QImage>> entries;
    {
        QMutexLocker locker(&cacheWriteMutex_);
        entries.swap(pendingCacheWrites_);
        pendingCacheWriteBytes_ = 0;
    }
    if (entries.isEmpty())
        return;

    std::shared_ptr<cache::SessionCache> cache;
    {
        QMutexLocker locker(&queueMutex_);
        cache = cache_;
    }
    if (!cache || !cache->isAvailable())
        return;
    for (const auto &entry : entries)
        cache->put(QStringLiteral("lod"), entry.first, QStringLiteral("png"),
                   doc::encodePng(entry.second));
}

void LevelLoader::releaseMemory()
{
    if (shutdown_)
        return;
    QVector<Worker *> workers;
    {
        QMutexLocker locker(&queueMutex_);
        workers = workers_;
    }
    for (Worker *worker : workers) {
        QMetaObject::invokeMethod(
            worker, [worker]() { util::releaseFreeMemory(); }, Qt::QueuedConnection);
    }
}

void LevelLoader::shutdown()
{
    if (shutdown_)
        return;
    shutdown_ = true;
    {
        QMutexLocker locker(&shared_->mutex);
        shared_->latest.clear();
    }
    {
        QMutexLocker locker(&queueMutex_);
        queue_.clear();
    }
    // Retire every worker: each sees the flag after its current job and
    // leaves its loop, then quits its own thread.
    for (Worker *worker : std::as_const(workers_))
        worker->retire();
    for (QThread *thread : std::as_const(threads_))
        thread->wait();
    for (Worker *worker : std::as_const(workers_)) {
        QCoreApplication::removePostedEvents(worker);
        delete worker;
    }
    for (QThread *thread : std::as_const(threads_))
        delete thread;
    threads_.clear();
    workers_.clear();
    QMutexLocker locker(&shared_->mutex);
    shared_->ramCache.clear();
    shared_->ramCacheBytes = 0;
}

} // namespace ui
