#include "level_loader.h"

#include "cache/session_cache.h"
#include "doc/image_io.h"
#include "util/memory.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QHash>
#include <QImageReader>
#include <QMetaObject>
#include <QMutex>
#include <QTimer>

#include <utility>

namespace ui {
namespace {

// The worker executes these events in its own thread's event loop; the
// queue is that event queue, so the band maps onto the event priority.
class QueueEvent : public QEvent
{
public:
    static constexpr QEvent::Type kType = QEvent::Type(QEvent::User + 1);
    explicit QueueEvent(std::function<void()> run)
        : QEvent(kType)
        , run(std::move(run))
    {
    }
    std::function<void()> run;
};

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
    // Stored bytes are upright: the reference bakes orientation when an
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
    // matching the reference's smooth downscale.
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

// The worker object: it only exists to have its event() run request
// events in the loader thread.
class LevelLoader::Worker : public QObject
{
public:
    Worker(std::shared_ptr<Shared> shared, QThread *thread_ptr)
        : shared_(std::move(shared))
    {
        // Move onto the loader thread: a member timer only starts in the
        // thread it lives in, and moveToThread() does not move children
        // (the timers are moved explicitly below).
        moveToThread(thread_ptr);
        // One heap trim shortly after the last decode of a burst, so a
        // run of culled images does not call malloc_trim per image.
        trimTimer_.setSingleShot(true);
        trimTimer_.setInterval(kTrimCoalesceMs);
        connect(&trimTimer_, &QTimer::timeout, this, []() { util::releaseFreeMemory(); });

        // Delayed, and re-armed by the caller once a result is installed:
        // a cached image shares its pixels with the installed level, so
        // it only becomes a resident cost after the UI dropped its copy.
        cacheCostTimer_.setSingleShot(true);
        cacheCostTimer_.setInterval(kCacheCostDelayMs);
        connect(&cacheCostTimer_, &QTimer::timeout, this, [this]() { updateRamCacheCost(); });

        trimTimer_.moveToThread(thread_ptr);
        cacheCostTimer_.moveToThread(thread_ptr);
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QueueEvent::kType) {
            auto *request = static_cast<QueueEvent *>(event);
            if (request->run)
                request->run();
            return true;
        }
        return QObject::event(event);
    }

    // Restarts the timer, so a burst coalesces into a single trim.
    void scheduleTrim() { trimTimer_.start(); }

    // Schedules a cache-cost recomputation (see updateRamCacheCost).
    void scheduleCacheCost()
    {
        cacheCostTimer_.start();
    }

    // Drops cached levels until the cache fits its budget.
    void evictToBudget()
    {
        qint64 budget = 0;
        {
            QMutexLocker locker(&shared_->mutex);
            budget = shared_->ramCacheBudget;
        }
        if (budget <= 0) {
            if (!cache_.isEmpty()) {
                cache_.clear();
                publishCacheBytes(0);
            }
            return;
        }
        while (ramCacheBytes_ > budget && !cache_.isEmpty()) {
            const QString *oldest = nullptr;
            for (auto it = cache_.cbegin(); it != cache_.cend(); ++it) {
                if (!oldest || it->lastUsed < cache_.value(*oldest).lastUsed)
                    oldest = &it.key();
            }
            if (!oldest)
                break;
            ramCacheBytes_ -= cache_.value(*oldest).bytes;
            cache_.remove(*oldest);
        }
        publishCacheBytes(ramCacheBytes_);
    }

    // Recomputes the cache cost, evicting if it now exceeds the budget.
    void updateRamCacheCost()
    {
        qint64 total = 0;
        for (auto it = cache_.cbegin(); it != cache_.cend(); ++it)
            total += it->image.sizeInBytes();
        ramCacheBytes_ = total;
        publishCacheBytes(ramCacheBytes_);
        evictToBudget();
    }

private:
    void publishCacheBytes(qint64 bytes)
    {
        QMutexLocker locker(&shared_->mutex);
        shared_->ramCacheBytes = bytes;
    }

public:
    // Long enough to span the decode of one burst on this thread, short
    // enough that transients do not linger until the idle release.
    static constexpr int kTrimCoalesceMs = 50;
    // Short delay so the UI thread is done with the result it just
    // received before the reference count is checked.
    static constexpr int kCacheCostDelayMs = 200;

    struct CachedLevel
    {
        QImage image; // holds the level its caller installed
        qint64 bytes = 0;
        quint64 lastUsed = 0;
    };

    // The decoded-level LRU, owned by this thread. Image buffers are
    // shared with the installed levels, so holding one keeps its pixels
    // alive at no extra cost until the UI releases them.
    QHash<QString, CachedLevel> cache_;
    qint64 ramCacheBytes_ = 0;
    quint64 useCounter_ = 0;

private:
    std::shared_ptr<Shared> shared_;
    QTimer trimTimer_;
    QTimer cacheCostTimer_;
};

LevelLoader::LevelLoader(QObject *parent)
    : QObject(parent)
{
    thread_.setObjectName(QStringLiteral("level-loader"));
    thread_.start();
    // The worker moves itself (and its timers) onto the thread once it
    // is running; posting events to it from this point on is safe,
    // because they queue until its loop runs.
    worker_ = new Worker(shared_, &thread_);
}

LevelLoader::~LevelLoader()
{
    shutdown();
}

void LevelLoader::setLevelCache(std::shared_ptr<cache::SessionCache> cache)
{
    cache_ = std::move(cache);
}

void LevelLoader::setRamCacheBudget(qint64 bytes)
{
    {
        QMutexLocker locker(&shared_->mutex);
        shared_->ramCacheBudget = qMax(qint64(0), bytes);
    }
    // The worker recomputes its cost once the UI is done with the levels
    // it just received.
    enqueueRamCacheBudget();
}

qint64 LevelLoader::ramCacheBytes() const
{
    QMutexLocker locker(&shared_->mutex);
    return shared_->ramCacheBytes;
}

void LevelLoader::reconsiderRamCache()
{
    if (shutdown_ || !worker_)
        return;
    Worker *const worker = worker_;
    enqueue([worker]() { worker->scheduleCacheCost(); }, RequestBand::Deferred);
}

void LevelLoader::enqueueRamCacheBudget()
{
    if (shutdown_ || !worker_)
        return;
    Worker *const worker = worker_;
    enqueue([worker]() { worker->updateRamCacheCost(); }, RequestBand::Deferred);
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
    const auto cache = cache_;
    const auto shared = shared_;
    Worker *const worker = worker_;
    enqueue(
        [this, requestId, source = std::move(source), targetSize, quality, cacheKey, coalesceKey,
         cache, shared, worker]() {
            // A newer request for the same item supersedes this one; it
            // is dropped before any decode work happens.
            if (!coalesceKey.isEmpty()) {
                QMutexLocker locker(&shared->mutex);
                if (shared->latest.value(coalesceKey) != requestId) {
                    emit levelCancelled(requestId);
                    return;
                }
            }
            QImage image;
            const bool cacheable =
                cache && cache->isAvailable() && !cacheKey.isEmpty() && targetSize.isValid()
                && !targetSize.isEmpty();

            // Cache first, lightest to heaviest: the worker's RAM LRU
            // (a reference to a buffer that may already be alive), then
            // the session cache (a PNG read), then a decode.
            if (!cacheKey.isEmpty() && worker) {
                const auto hit = worker->cache_.constFind(cacheKey);
                if (hit != worker->cache_.cend() && hit->image.size() == targetSize) {
                    image = hit->image;
                    worker->cache_[cacheKey].lastUsed = ++worker->useCounter_;
                }
            }
            if (image.isNull() && cacheable) {
                if (auto cached = cache->get(QStringLiteral("lod"), cacheKey)) {
                    const QImage fromCache = QImage::fromData(*cached);
                    if (!fromCache.isNull() && fromCache.size() == targetSize) {
                        image = fromCache;
                    } else {
                        // A stale or corrupt entry: drop it.
                        cache->remove(QStringLiteral("lod"), cacheKey);
                    }
                }
            }
            if (image.isNull()) {
                image = decodeLevel(source, targetSize, quality);
                if (!image.isNull() && cacheable) {
                    cache->put(QStringLiteral("lod"), cacheKey, QStringLiteral("png"),
                               doc::encodePng(image));
                }
            }
            // Keep the freshly produced level in the RAM LRU, bounded by
            // its own budget; the bytes are shared with the installed
            // level, so this costs nothing while it is displayed.
            if (!image.isNull() && !cacheKey.isEmpty() && worker) {
                qint64 budget = 0;
                {
                    QMutexLocker locker(&shared->mutex);
                    budget = shared->ramCacheBudget;
                }
                if (budget > 0 && (image.size().isEmpty() || !cacheable
                                   || image.size() == targetSize)) {
                    if (!cacheable || image.size() != targetSize)
                        worker->cache_.remove(cacheKey);
                    Worker::CachedLevel entry;
                    entry.image = image;
                    entry.bytes = image.sizeInBytes();
                    entry.lastUsed = ++worker->useCounter_;
                    worker->cache_.insert(cacheKey, entry);
                    worker->scheduleCacheCost();
                }
            }
            if (image.isNull())
                emit levelFailed(requestId);
            else
                emit levelReady(requestId, image);
            // Drop the decode transients. Coalesced: a burst of decodes
            // trims the heap once, not once per image.
            if (worker)
                worker->scheduleTrim();
        },
        band);
}

void LevelLoader::enqueue(std::function<void()> run, RequestBand band)
{
    if (shutdown_ || !worker_)
        return;
    const Qt::EventPriority priority = band == RequestBand::Selected ? Qt::HighEventPriority
        : band == RequestBand::Visible                                 ? Qt::NormalEventPriority
                                                                      : Qt::LowEventPriority;
    QCoreApplication::postEvent(worker_, new QueueEvent(std::move(run)), priority);
}

void LevelLoader::releaseMemory()
{
    if (shutdown_)
        return;
    enqueue([]() { util::releaseFreeMemory(); }, RequestBand::Deferred);
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
    // Delete the worker on its own thread, so its timers are stopped
    // from the thread that owns them.
    QMetaObject::invokeMethod(
        worker_,
        [this]() {
            delete worker_;
            worker_ = nullptr;
        },
        Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
    // Requests queued while the event loop was winding down are of no
    // interest anymore.
    QCoreApplication::removePostedEvents(worker_);
    QMutexLocker locker(&shared_->mutex);
    shared_->ramCacheBytes = 0;
}

} // namespace ui
