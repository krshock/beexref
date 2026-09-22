#pragma once

#include "doc/source.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QThread>

namespace ui {

// Decodes item levels on a dedicated worker thread. Requests carry the
// item's immutable source, never the item, so the UI thread may keep
// editing (or delete the item) while a decode runs. Results are
// delivered on the loader's thread (the UI thread) through queued
// signal delivery.
//
// This is the seam the memory phase grows into the full LOD manager:
// the policy here is deliberately simple (visible items, one level
// each, re-requested when the zoom needs more).
class LevelLoader : public QObject
{
    Q_OBJECT

public:
    explicit LevelLoader(QObject *parent = nullptr);
    ~LevelLoader() override;

    // requestId is the caller's token; it is echoed back with the
    // result. targetSize is the wanted pixel size (aspect preserved).
    void request(quint64 requestId, doc::SourcePtr source, const QSize &targetSize);

    // Stops the worker thread and drops pending requests. Must be
    // called before the documents the sources read from are closed.
    void shutdown();

signals:
    void levelReady(quint64 requestId, const QImage &image);
    void levelFailed(quint64 requestId);

private:
    QThread thread_;
    QObject worker_;
    bool shutdown_ = false;
};

} // namespace ui
