#pragma once

#include "doc/source.h"

#include <QImage>
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
    void request(quint64 requestId, doc::SourcePtr source, const QSize &targetSize,
                 const QString &quality, const QString &cacheKey = {});

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

private:
    QThread thread_;
    QObject worker_;
    std::shared_ptr<cache::SessionCache> cache_;
    bool shutdown_ = false;
};

} // namespace ui
