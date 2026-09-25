#pragma once

#include "levels.h"

#include <QDateTime>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QTimer>

#include <QObject>

#include <memory>

namespace cache {
class SessionCache;
}

namespace doc {
class Item;
}

namespace ui {

class LevelLoader;
class Scene;
class SceneItem;

// LOD scheduler and memory policy, ported from the reference manager:
//   * evaluations are debounced to one per event-loop turn;
//   * each item has one level ladder (fractions above the saved floor)
//     and at most one decode in flight, guarded by a generation;
//   * items in an active gesture keep their level until the input
//     settles;
//   * failed decodes back off after a few tries;
//   * ram_budget downgrades levels over the cap, biggest saving first,
//     never a level in a gesture;
//   * idle release returns freed memory to the OS.
class LodManager : public QObject
{
    Q_OBJECT

public:
    explicit LodManager(QObject *parent = nullptr);

    void setScene(Scene *scene);
    void setLoader(LevelLoader *loader);
    void setSettings(const LodSettings &settings);
    const LodSettings &settings() const { return settings_; }
    // Decoded levels are cached in this session cache when set.
    void setLevelCache(std::shared_ptr<cache::SessionCache> cache);

    // Viewport state the policy runs against: the visible rect in scene
    // coordinates and the view's transform scale.
    void setViewState(const QRectF &visibleSceneRect, double viewScale);

    // The consumption hints: how the manager orders its requests before
    // the loader consumes them.
    //
    //   * every request gets a RequestHint (selected, visible, distance,
    //     band);
    //   * hintOrder() applies one comparator per hint method, stable and
    //     least significant first, so earlier methods dominate;
    //   * the band is the coarse half the loader consumes (selected,
    //     visible, deferred), the rest orders within the batch.
    //
    // Decodes are ordered by distance from a scene point: selected
    // images first, then visible ones nearest first, then off-screen.
    // Unset (the default) means the visible rect's centre.
    void setOrderOrigin(const QPointF &scenePoint);
    QPointF orderOrigin() const;

    // Rebuilds every item's ladder (document replaced or settings
    // changed) and evaluates. Upgrades stay deferred to the floor until
    // the next user interaction, as after a file load.
    void reset();

    // Debounced evaluation, coalesced to one per event-loop turn.
    void schedule();
    // Immediate evaluation; also ends the deferred-upgrade phase.
    void evaluateNow();

    // Holds every LOD action (evaluations, level swaps, decodes) for the
    // given window; each call restarts the window, so a zoom burst is
    // held until the given milliseconds after the last event, when one
    // evaluation runs.
    void hold(int milliseconds);
    bool holding() const { return holdTimer_.isActive(); }

    // Items in an active gesture keep their displayed level.
    void setGestureItems(const QSet<const doc::Item *> &items);

    // Drops all scheduler state for a view that is about to be deleted.
    void forgetItem(SceneItem *item);

    struct Stats
    {
        double levelMB = 0;
        double encodedMB = 0;
        double cacheMB = 0;
        // Our own accounting of decoded LOD buffers, independent of RSS:
        // bytes installed on the items, and bytes reserved by decodes
        // that are still in flight.
        double lodCommittedMB = 0;
        double lodReservedMB = 0;
        double lodBudgetMB = 0;
        // Bytes the loader's decoded-level LRU holds in RAM.
        double lodRamCacheMB = 0;
        // Worker threads decoding levels.
        int lodThreads = 0;
        int items = 0;
        int decodes = 0;
        int requests = 0;
        int evals = 0;
        int releases = 0;
        int pending = 0;
        int cancelled = 0;
        // Requests skipped because the primary budget had no headroom.
        int admissionRejects = 0;
    };
    Stats stats() const;

    // One Debug audit line with RSS, level and encoded bytes plus the
    // counters, deltas against the previous audit.
    void logAudit(const QString &label);

signals:
    // A level was installed; the scene repaints it.
    void levelsChanged();

private:
    // The hint inputs of one image, and the ordered plan built from
    // them.
    struct RequestHint
    {
        bool selected = false;
        bool visible = false;
        double distance = 0;
        RequestBand band = RequestBand::Deferred;
        // Byte accounting for the hint: what the requested level costs,
        // what the item already holds, and the difference (negative
        // frees RAM). Deferred requests are ordered by the most freed
        // first.
        qint64 targetBytes = 0;
        qint64 currentBytes = 0;
        qint64 deltaBytes = 0;
    };
    QPointF effectiveOrderOrigin() const;
    QVector<SceneItem *> hintOrder(const QVector<SceneItem *> &items,
                                   const QHash<SceneItem *, RequestHint> &hints) const;

    struct Pending
    {
        SceneItem *item = nullptr;
        double fraction = 1.0;
        int generation = 0;
        // Bytes the requested level will add once installed; held as a
        // reservation until the decode is resolved.
        qint64 bytes = 0;
    };

    void evaluate();
    // The band decides the loader's queue order (see RequestBand).
    void requestLevel(SceneItem *item, double fraction,
                      RequestBand band = RequestBand::Visible);
    void cancelLevel(SceneItem *item);
    void applyRAMBudget(QHash<SceneItem *, double> &desired, qint64 budget);
    // The cap on decoded LOD bytes: the primary budget, tightened by the
    // ram_budget method's budget. 0 means unlimited.
    qint64 effectiveBudgetBytes() const;
    double desiredFraction(const SceneItem *item) const;
    bool visible(const SceneItem *item, double margin) const;
    bool transforming(const SceneItem *item) const;
    QString cacheKey(const SceneItem *item, double fraction) const;
    // Bytes of the decoded levels the visible items hold; the settle
    // never releases these.
    qint64 pinnedLodBytes() const;
    void scheduleRelease();
    void releaseMemory();
    void onLevelReady(quint64 requestId, const QImage &image);
    void onLevelFailed(quint64 requestId);
    void onLevelCancelled(quint64 requestId);

    Scene *scene_ = nullptr;
    LevelLoader *loader_ = nullptr;
    std::shared_ptr<cache::SessionCache> levelCache_;
    LodSettings settings_ = normalized(LodSettings());

    QRectF visibleRect_;
    // Invalid means "use the visible rect's centre".
    QPointF orderOrigin_;
    double viewScale_ = 1.0;

    bool evalScheduled_ = false;
    bool deferUpgrades_ = true;

    QHash<SceneItem *, quint64> inFlight_;
    QHash<quint64, Pending> pending_;
    // Bytes reserved by the pending decodes above.
    qint64 reservedBytes_ = 0;
    QSet<const doc::Item *> gestureItems_;

    QTimer releaseTimer_;
    QTimer holdTimer_;
    quint64 nextRequestId_ = 1;
    int decodes_ = 0;
    int requests_ = 0;
    int evals_ = 0;
    int releases_ = 0;
    int cancelled_ = 0;
    int admissionRejects_ = 0;
};

} // namespace ui
