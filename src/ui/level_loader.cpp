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
    Worker()
    {
        // One heap trim shortly after the last decode of a burst, so a
        // run of culled images does not call malloc_trim per image.
        trimTimer_.setSingleShot(true);
        trimTimer_.setInterval(kTrimCoalesceMs);
        connect(&trimTimer_, &QTimer::timeout, this, []() { util::releaseFreeMemory(); });
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

private:
    // Long enough to span the decode of one burst on this thread, short
    // enough that transients do not linger until the idle release.
    static constexpr int kTrimCoalesceMs = 50;
    QTimer trimTimer_;
};

LevelLoader::LevelLoader(QObject *parent)
    : QObject(parent)
{
    worker_ = new Worker;
    worker_->moveToThread(&thread_);
    thread_.setObjectName(QStringLiteral("level-loader"));
    thread_.start();
}

LevelLoader::~LevelLoader()
{
    shutdown();
}

void LevelLoader::setLevelCache(std::shared_ptr<cache::SessionCache> cache)
{
    cache_ = std::move(cache);
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
            if (cacheable) {
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
    thread_.quit();
    thread_.wait();
    // Requests queued while the event loop was winding down are of no
    // interest anymore; drop them so the worker is not restarted.
    QCoreApplication::removePostedEvents(worker_);
    delete worker_;
    worker_ = nullptr;
}

} // namespace ui
