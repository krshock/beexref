#include "level_loader.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImageReader>
#include <QMetaObject>

namespace ui {
namespace {

QImage decodeLevel(const doc::SourcePtr &source, const QSize &targetSize)
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

void LevelLoader::request(quint64 requestId, doc::SourcePtr source, const QSize &targetSize)
{
    if (shutdown_)
        return;
    QMetaObject::invokeMethod(
        &worker_,
        [this, requestId, source = std::move(source), targetSize]() {
            const QImage image = decodeLevel(source, targetSize);
            if (image.isNull())
                emit levelFailed(requestId);
            else
                emit levelReady(requestId, image);
        },
        Qt::QueuedConnection);
}

void LevelLoader::shutdown()
{
    if (shutdown_)
        return;
    shutdown_ = true;
    thread_.quit();
    thread_.wait();
    // Requests queued while the event loop was winding down are of no
    // interest anymore; drop them so the worker is not restarted.
    QCoreApplication::removePostedEvents(&worker_);
}

} // namespace ui
