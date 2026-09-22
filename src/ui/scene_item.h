#pragma once

#include "doc/item.h"
#include "levels.h"

#include <QDateTime>
#include <QFont>
#include <QGraphicsItem>
#include <QImage>
#include <QVector>

#include <optional>

namespace ui {

// Canvas representation of one document item, painted from a QImage
// level rather than a QPixmap: no X11 server-side copy, and the memory
// stays visible to the process. Local coordinates are original image
// pixels, so the level can be coarser than the item without changing
// its geometry.
class SceneItem : public QGraphicsItem
{
public:
    explicit SceneItem(doc::ItemPtr item, QGraphicsItem *parent = nullptr);

    const doc::ItemPtr &item() const { return item_; }
    bool isPixmap() const { return item_->isPixmap(); }
    bool isText() const { return item_->isText(); }
    bool isError() const { return failed_ || item_->isError(); }

    const QImage &level() const { return level_; }
    double levelFraction() const { return levelFraction_; }
    void setLevel(const QImage &image, double fraction);

    bool levelUnavailable() const { return failed_; }
    void setLevelUnavailable();

    // --- LOD state, owned by LodManager ---------------------------------
    const QVector<Level> &levels() const { return levels_; }
    void setLevels(QVector<Level> levels) { levels_ = std::move(levels); }

    int generation() const { return generation_; }
    void bumpGeneration() { ++generation_; }

    int failures() const { return failures_; }
    void noteDecodeFailure();
    bool retryBlocked() const;

    bool wasVisible() const { return wasVisible_; }
    void setWasVisible(bool visible) { wasVisible_ = visible; }

    QDateTime lastUsed() const { return lastUsed_; }
    void noteUsed() { lastUsed_ = QDateTime::currentDateTime(); }

    bool requeue() const { return requeue_; }
    void setRequeue(bool requeue) { requeue_ = requeue; }

    double coarsestFraction() const;
    std::optional<double> coarserFraction(double fraction) const;
    qint64 levelBytesFor(double fraction) const;
    QSize levelSizeFor(double fraction) const;
    // Bytes of the currently displayed level (0 when none).
    qint64 displayedLevelBytes() const;

    // The coarsest level is kept once decoded: culling an item back to
    // it then costs no decode and no full-size transient, only the few
    // KB of the copy itself.
    void rememberCoarsestLevel();
    bool hasCoarsestCopy() const;
    void applyCoarsestCopy();

    // Applies position, transform and opacity from the document.
    void applyModelState();
    // Writes the scene position back into the document item.
    void syncPositionToModel();

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

private:
    QSize imageSize() const;
    QRectF imageBounds() const;
    QString errorText() const;

    doc::ItemPtr item_;
    QImage level_;
    double levelFraction_ = 0;
    bool failed_ = false;
    QFont font_;

    QVector<Level> levels_;
    QImage coarsestLevel_;
    double coarsestLevelFraction_ = 0;
    int generation_ = 0;
    int failures_ = 0;
    QDateTime lastFailed_;
    QDateTime lastUsed_;
    bool wasVisible_ = false;
    bool requeue_ = false;
};

} // namespace ui

