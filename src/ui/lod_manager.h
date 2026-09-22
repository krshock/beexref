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

    // Rebuilds every item's ladder (document replaced or settings
    // changed) and evaluates. Upgrades stay deferred to the floor until
    // the next user interaction, as after a file load.
    void reset();

    // Debounced evaluation, coalesced to one per event-loop turn.
    void schedule();
    // Immediate evaluation; also ends the deferred-upgrade phase.
    void evaluateNow();

    // Items in an active gesture keep their displayed level.
    void setGestureItems(const QSet<const doc::Item *> &items);

    // Drops all scheduler state for a view that is about to be deleted.
    void forgetItem(SceneItem *item);

    struct Stats
    {
        double levelMB = 0;
        double encodedMB = 0;
        double cacheMB = 0;
        int items = 0;
        int decodes = 0;
        int requests = 0;
        int evals = 0;
        int releases = 0;
        int pending = 0;
    };
    Stats stats() const;

    // One Debug audit line with RSS, level and encoded bytes plus the
    // counters, deltas against the previous audit.
    void logAudit(const QString &label);

signals:
    // A level was installed; the scene repaints it.
    void levelsChanged();

private:
    struct Pending
    {
        SceneItem *item = nullptr;
        double fraction = 1.0;
        int generation = 0;
    };

    void evaluate();
    void requestLevel(SceneItem *item, double fraction);
    void cancelLevel(SceneItem *item);
    void applyRAMBudget(QHash<SceneItem *, double> &desired, qint64 budget);
    double desiredFraction(const SceneItem *item) const;
    bool visible(const SceneItem *item, double margin) const;
    bool transforming(const SceneItem *item) const;
    QString cacheKey(const SceneItem *item, double fraction) const;
    void scheduleRelease();
    void releaseMemory();
    void onLevelReady(quint64 requestId, const QImage &image);
    void onLevelFailed(quint64 requestId);

    Scene *scene_ = nullptr;
    LevelLoader *loader_ = nullptr;
    std::shared_ptr<cache::SessionCache> levelCache_;
    LodSettings settings_ = normalized(LodSettings());

    QRectF visibleRect_;
    double viewScale_ = 1.0;

    bool evalScheduled_ = false;
    bool deferUpgrades_ = true;

    QHash<SceneItem *, quint64> inFlight_;
    QHash<quint64, Pending> pending_;
    QSet<const doc::Item *> gestureItems_;

    QTimer releaseTimer_;
    quint64 nextRequestId_ = 1;
    int decodes_ = 0;
    int requests_ = 0;
    int evals_ = 0;
    int releases_ = 0;
};

} // namespace ui
