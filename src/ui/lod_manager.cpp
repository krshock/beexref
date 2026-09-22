#include "lod_manager.h"

#include "cache/session_cache.h"
#include "level_loader.h"
#include "logging.h"
#include "scene.h"
#include "scene_item.h"
#include "util/memory.h"

#include <QImage>

#include <utility>

namespace ui {
namespace {

constexpr int kReleaseDelayMs = 1500;
constexpr double kBytesPerMB = 1024.0 * 1024.0;

} // namespace

LodManager::LodManager(QObject *parent)
    : QObject(parent)
{
    releaseTimer_.setSingleShot(true);
    releaseTimer_.setInterval(kReleaseDelayMs);
    connect(&releaseTimer_, &QTimer::timeout, this, &LodManager::releaseMemory);
}

void LodManager::setScene(Scene *scene)
{
    if (scene_ == scene)
        return;
    if (scene_)
        disconnect(scene_, nullptr, this, nullptr);
    scene_ = scene;
    if (scene_) {
        connect(scene_, &Scene::itemsChanged, this, &LodManager::reset);
        connect(scene_, &Scene::destroyed, this, [this]() { scene_ = nullptr; });
        // Before the scene deletes its items, forget every pointer to
        // them; reset() must not run here, it reads the scene's items.
        connect(scene_, &Scene::aboutToBeDestroyed, this, [this]() {
            inFlight_.clear();
            pending_.clear();
            gestureItems_.clear();
            scene_ = nullptr;
        });
    }
    reset();
}

void LodManager::setLoader(LevelLoader *loader)
{
    if (loader_)
        disconnect(loader_, nullptr, this, nullptr);
    loader_ = loader;
    if (loader_) {
        loader_->setLevelCache(levelCache_);
        connect(loader_, &LevelLoader::levelReady, this, &LodManager::onLevelReady);
        connect(loader_, &LevelLoader::levelFailed, this, &LodManager::onLevelFailed);
        connect(loader_, &LevelLoader::levelCancelled, this, &LodManager::onLevelCancelled);
    }
}

void LodManager::setSettings(const LodSettings &settings)
{
    settings_ = normalized(settings);
    reset();
}

void LodManager::setLevelCache(std::shared_ptr<cache::SessionCache> cache)
{
    levelCache_ = std::move(cache);
    if (loader_)
        loader_->setLevelCache(levelCache_);
}

QString LodManager::cacheKey(const SceneItem *item, double fraction) const
{
    if (!levelCache_ || !levelCache_->isAvailable())
        return {};
    // Stable identity, independent of the board row: a level stays
    // cached across saves and reopens.
    const QString uuid = item->item()->ensureUuid();
    if (uuid.isEmpty())
        return {};
    return QStringLiteral("%1|%2|%3").arg(uuid, settings_.quality,
                                          QString::number(fraction, 'g', 17));
}

void LodManager::setViewState(const QRectF &visibleSceneRect, double viewScale)
{
    visibleRect_ = visibleSceneRect;
    viewScale_ = viewScale > 0 ? viewScale : 1.0;
}

void LodManager::reset()
{
    inFlight_.clear();
    pending_.clear();
    deferUpgrades_ = true;
    if (!scene_)
        return;

    for (SceneItem *item : scene_->pixmapItemViews()) {
        const doc::ItemPtr &model = item->item();
        const QSize original = model->originalSize();
        const double floorFraction =
            !model->floorData.isEmpty() && model->floorFraction > 0 ? model->floorFraction : 0.0;
        item->setLevels(buildLevels(settings_, original, floorFraction));
        // Keep the saved thumbnail when it is the ladder's floor, so
        // culling back to it never needs a decode.
        item->rememberCoarsestLevel();
        item->bumpGeneration();
        item->setRequeue(false);
    }
    schedule();
}

void LodManager::schedule()
{
    if (evalScheduled_)
        return;
    evalScheduled_ = true;
    QTimer::singleShot(0, this, [this]() {
        evalScheduled_ = false;
        evaluate();
    });
}

void LodManager::evaluateNow()
{
    deferUpgrades_ = false;
    evaluate();
}

void LodManager::setGestureItems(const QSet<const doc::Item *> &items)
{
    gestureItems_ = items;
}

void LodManager::forgetItem(SceneItem *item)
{
    const auto flight = inFlight_.find(item);
    if (flight != inFlight_.end()) {
        pending_.remove(flight.value());
        inFlight_.erase(flight);
    }
}

bool LodManager::transforming(const SceneItem *item) const
{
    return gestureItems_.contains(item->item().get());
}

bool LodManager::visible(const SceneItem *item, double margin) const
{
    if (visibleRect_.isEmpty())
        return false;
    const QRectF rect = item->sceneBoundingRect();
    const double mx = visibleRect_.width() * margin;
    const double my = visibleRect_.height() * margin;
    return rect.left() - mx < visibleRect_.right() && rect.right() + mx > visibleRect_.left()
        && rect.top() - my < visibleRect_.bottom() && rect.bottom() + my > visibleRect_.top();
}

double LodManager::desiredFraction(const SceneItem *item) const
{
    const doc::ItemPtr &model = item->item();
    const QSize original = model->originalSize();
    const double neededWidth =
        original.width() * model->scale * viewScale_ * kLevelOverdraw;
    const double neededHeight =
        original.height() * model->scale * viewScale_ * kLevelOverdraw;
    return desiredLevelFraction(item->levels(), neededWidth, neededHeight);
}

void LodManager::applyRAMBudget(QHash<SceneItem *, double> &desired, qint64 budget)
{
    qint64 total = 0;
    for (auto it = desired.cbegin(); it != desired.cend(); ++it)
        total += it.key()->levelBytesFor(it.value());

    while (total > budget) {
        SceneItem *best = nullptr;
        double bestFraction = 0;
        qint64 bestSaved = 0;
        for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
            SceneItem *item = it.key();
            if (transforming(item))
                continue;
            const auto coarser = item->coarserFraction(it.value());
            if (!coarser)
                continue;
            const qint64 saved = item->levelBytesFor(it.value()) - item->levelBytesFor(*coarser);
            if (saved <= 0)
                continue;
            if (!best || saved > bestSaved
                || (saved == bestSaved && item->lastUsed() > best->lastUsed())) {
                best = item;
                bestFraction = *coarser;
                bestSaved = saved;
            }
        }
        if (!best)
            break;
        total -= bestSaved;
        desired[best] = bestFraction;
    }
}

void LodManager::evaluate()
{
    if (!scene_)
        return;
    ++evals_;

    QHash<SceneItem *, double> desired;
    const QVector<SceneItem *> items = scene_->pixmapItemViews();
    for (SceneItem *item : items) {
        const doc::ItemPtr &model = item->item();
        if (!model->hasSource() || item->levels().isEmpty())
            continue;

        if (transforming(item)) {
            // A level swap would disturb the gesture: keep the current
            // level for the budget but queue nothing.
            cancelLevel(item);
            desired.insert(item, item->levelFraction());
            continue;
        }

        const bool visibleNow = visible(item, kLevelVisibilityMargin);
        double fraction = 0;
        if (visibleNow) {
            fraction = desiredFraction(item);
        } else if (item->wasVisible()) {
            fraction = item->levelFraction() > 0 ? item->levelFraction()
                                                 : item->coarsestFraction();
        } else {
            fraction = item->coarsestFraction();
        }
        item->setWasVisible(visibleNow);
        desired.insert(item, fraction);
    }

    const qint64 budget = levelBudgetBytes(settings_);
    if (budget > 0)
        applyRAMBudget(desired, budget);

    for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
        SceneItem *item = it.key();
        const double fraction = it.value();
        if (item->retryBlocked())
            continue;
        if (deferUpgrades_ && fraction > item->coarsestFraction())
            continue;

        const auto flightIt = inFlight_.find(item);
        const bool inFlight = flightIt != inFlight_.end();
        const double pendingFraction =
            inFlight ? pending_.value(flightIt.value()).fraction : 0.0;

        // Culling back to the floor uses the kept copy instead of a
        // decode (the copy is a few KB; a decode would read the whole
        // blob and allocate the full-size image).
        if (qFuzzyCompare(fraction, item->coarsestFraction()) && item->hasCoarsestCopy()
            && !qFuzzyCompare(fraction, item->levelFraction())) {
            cancelLevel(item);
            item->applyCoarsestCopy();
            continue;
        }

        // Already displaying the wanted level: cancel a stale decode for
        // another level instead of re-decoding what we have.
        const bool currentIsWanted =
            item->levelFraction() > 0 && qFuzzyCompare(fraction, item->levelFraction());
        if (currentIsWanted && !item->requeue()) {
            cancelLevel(item);
            continue;
        }
        if (inFlight && qFuzzyCompare(fraction, pendingFraction) && !item->requeue())
            continue;

        item->setRequeue(false);
        requestLevel(item, fraction);
    }
}

void LodManager::requestLevel(SceneItem *item, double fraction)
{
    if (!loader_)
        return;
    ++requests_;
    item->noteUsed();
    item->bumpGeneration();

    const quint64 requestId = nextRequestId_++;
    Pending pending;
    pending.item = item;
    pending.fraction = fraction;
    pending.generation = item->generation();
    pending_.insert(requestId, pending);
    inFlight_.insert(item, requestId);

    loader_->request(requestId, item->item()->source, item->levelSizeFor(fraction),
                     settings_.quality, cacheKey(item, fraction),
                     item->item()->ensureUuid());
}

void LodManager::cancelLevel(SceneItem *item)
{
    const auto it = inFlight_.find(item);
    if (it == inFlight_.end())
        return;
    pending_.remove(it.value());
    inFlight_.erase(it);
    // The in-flight decode result is now unknown to us, so it is
    // ignored when it arrives.
    item->bumpGeneration();
}

void LodManager::onLevelReady(quint64 requestId, const QImage &image)
{
    const auto it = pending_.find(requestId);
    if (it == pending_.end())
        return;
    const Pending pending = it.value();
    pending_.erase(it);
    inFlight_.remove(pending.item);

    // A newer request superseded this decode.
    if (pending.generation != pending.item->generation())
        return;

    pending.item->setLevel(image, pending.fraction);
    ++decodes_;
    emit levelsChanged();
    scheduleRelease();
}

void LodManager::onLevelFailed(quint64 requestId)
{
    const auto it = pending_.find(requestId);
    if (it == pending_.end())
        return;
    const Pending pending = it.value();
    pending_.erase(it);
    inFlight_.remove(pending.item);

    if (pending.generation != pending.item->generation())
        return;
    pending.item->noteDecodeFailure();
    if (pending.item->failures() >= 3)
        logging::debug(QStringLiteral("Level decode failed repeatedly"),
                       {{QStringLiteral("item"), pending.item->item()->id},
                        {QStringLiteral("fraction"), pending.fraction}});
}

void LodManager::onLevelCancelled(quint64 requestId)
{
    // Superseded by a newer request for the same item: drop the state
    // without counting a failure.
    const auto it = pending_.find(requestId);
    if (it == pending_.end())
        return;
    const Pending pending = it.value();
    pending_.erase(it);
    inFlight_.remove(pending.item);
    ++cancelled_;
}

void LodManager::scheduleRelease()
{
    releaseTimer_.start();
}

void LodManager::releaseMemory()
{
    // Both arenas: the UI thread's and the decode worker's.
    const bool released = util::releaseFreeMemory();
    if (loader_)
        loader_->releaseMemory();
    ++releases_;
    logAudit(QStringLiteral("idle"));
    if (released) {
        logging::trace(QStringLiteral("Released free heap memory"),
                       {{QStringLiteral("rss_mb"),
                         util::processRssBytes() / kBytesPerMB}});
    }
}

LodManager::Stats LodManager::stats() const
{
    Stats stats;
    stats.decodes = decodes_;
    stats.requests = requests_;
    stats.evals = evals_;
    stats.releases = releases_;
    stats.pending = static_cast<int>(pending_.size());
    stats.cancelled = cancelled_;
    stats.items = scene_ ? scene_->pixmapItemViews().size() : 0;
    if (!scene_)
        return stats;

    for (SceneItem *item : scene_->pixmapItemViews()) {
        stats.levelMB += item->displayedLevelBytes() / kBytesPerMB;
        const doc::SourcePtr &source = item->item()->source;
        if (source)
            stats.encodedMB += source->residentBytes() / kBytesPerMB;
    }
    if (levelCache_)
        stats.cacheMB = levelCache_->fileBytes() / kBytesPerMB;
    return stats;
}

void LodManager::logAudit(const QString &label)
{
    static double previousRssMB = 0;
    static double previousLevelMB = 0;
    static QDateTime previousAt;

    const Stats sample = stats();
    const double rssMB = util::processRssBytes() / kBytesPerMB;
    const QDateTime now = QDateTime::currentDateTime();

    logging::Attrs attrs{
        {QStringLiteral("label"), label},
        {QStringLiteral("rss_mb"), QString::number(rssMB, 'f', 1)},
        {QStringLiteral("level_mb"), QString::number(sample.levelMB, 'f', 1)},
        {QStringLiteral("encoded_mb"), QString::number(sample.encodedMB, 'f', 1)},
        {QStringLiteral("cache_mb"), QString::number(sample.cacheMB, 'f', 1)},
        {QStringLiteral("items"), sample.items},
        {QStringLiteral("decodes"), sample.decodes},
        {QStringLiteral("requests"), sample.requests},
        {QStringLiteral("evals"), sample.evals},
        {QStringLiteral("releases"), sample.releases},
        {QStringLiteral("pending"), sample.pending},
        {QStringLiteral("cancelled"), sample.cancelled},
    };
    if (previousAt.isValid()) {
        attrs.append({QStringLiteral("since_ms"),
                      previousAt.msecsTo(now)});
        attrs.append({QStringLiteral("d_rss_mb"),
                      QString::number(rssMB - previousRssMB, 'f', 1)});
        attrs.append({QStringLiteral("d_level_mb"),
                      QString::number(sample.levelMB - previousLevelMB, 'f', 1)});
    }
    logging::debug(QStringLiteral("MEM audit"), attrs);

    previousRssMB = rssMB;
    previousLevelMB = sample.levelMB;
    previousAt = now;
}

} // namespace ui
