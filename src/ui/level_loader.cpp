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

#include <utility>

namespace ui {
namespace {

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

LevelLoader::LevelLoader(QObject *parent)
    : QObject(parent)
{
    worker_.moveToThread(&thread_);
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
                          const QString &coalesceKey)
{
    if (shutdown_)
        return;
    if (!coalesceKey.isEmpty()) {
        QMutexLocker locker(&shared_->mutex);
        shared_->latest.insert(coalesceKey, requestId);
    }
    const auto cache = cache_;
    const auto shared = shared_;
    QMetaObject::invokeMethod(
        &worker_,
        [this, requestId, source = std::move(source), targetSize, quality, cacheKey, coalesceKey,
         cache, shared]() {
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
            // Drop the decode transients right away: they live in this
            // thread's arena and are the largest churn the app creates.
            util::releaseFreeMemory();
        },
        Qt::QueuedConnection);
}

void LevelLoader::releaseMemory()
{
    if (shutdown_)
        return;
    QMetaObject::invokeMethod(&worker_, []() { util::releaseFreeMemory(); },
                              Qt::QueuedConnection);
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
    QCoreApplication::removePostedEvents(&worker_);
}

} // namespace ui
