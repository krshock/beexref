#pragma once

#include "doc/source.h"
#include "levels.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QThread>

#include <memory>

namespace cache {
class SessionCache;
}

namespace ui {

// Decodes item levels on a dedicated worker thread. Requests carry the
// item's immutable source, never the item, so the UI thread may keep
// editing (or delete the item) while a decode runs. Results are
// delivered on the loader's thread (the UI thread) through queued
// signal delivery.
//
// When a session cache is attached, decoded levels are read from it
// first and written back after a decode, so repeat levels (culling
// back to the floor, reopening the same board) cost a small read
// instead of a full decode.
class LevelLoader : public QObject
{
    Q_OBJECT

public:
    explicit LevelLoader(QObject *parent = nullptr);
    ~LevelLoader() override;

    void setLevelCache(std::shared_ptr<cache::SessionCache> cache);

    // requestId is the caller's token; it is echoed back with the
    // result. targetSize is the wanted pixel size (aspect preserved);
    // quality is "fast" (single step) or "smooth" (progressive halving).
    // cacheKey names the level in the session cache; empty disables it.
    // coalesceKey identifies the item: a newer request for the same key
    // supersedes a queued one, so a zoom burst decodes only the latest
    // fraction, as the reference worker does.
    //
    // The band decides the queue order: a request in a higher band runs
    // before lower-band work, FIFO within a band. A superseded request
    // is still dropped for free when it reaches the front, and a decode
    // already running is never interrupted.
    void request(quint64 requestId, doc::SourcePtr source, const QSize &targetSize,
                 const QString &quality, const QString &cacheKey = {},
                 const QString &coalesceKey = {}, RequestBand band = RequestBand::Visible);

    // Stops the worker thread and drops pending requests. Must be
    // called before the documents the sources read from are closed.
    void shutdown();

    // Returns the worker thread's freed heap memory to the OS. Decode
    // transients (full-size decode buffers, scaled copies) live in the
    // worker's malloc arena, which a trim from the UI thread cannot
    // release.
    void releaseMemory();

signals:
    void levelReady(quint64 requestId, const QImage &image);
    void levelFailed(quint64 requestId);
    // The request was superseded by a newer one for the same item.
    void levelCancelled(quint64 requestId);

private:
    // Posts one request to the worker's event loop with the band's
    // priority.
    void enqueue(std::function<void()> run, RequestBand band);

    struct Shared
    {
        QMutex mutex;
        QHash<QString, quint64> latest;
    };

    QThread thread_;
    class Worker;
    Worker *worker_ = nullptr;
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::shared_ptr<cache::SessionCache> cache_;
    bool shutdown_ = false;
};

} // namespace ui
