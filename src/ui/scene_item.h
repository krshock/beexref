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

    // The visible part in local coordinates: the crop when one is set,
    // the whole image otherwise. Cached, so a model change never moves
    // the item's geometry without prepareGeometryChange().
    QRectF displayBounds() const { return displayBounds_; }

    // The whole image in local coordinates.
    QRectF imageBounds() const;

    // The text font (only meaningful for text items); scene export needs
    // it to build the SVG styles.
    const QFont &font() const { return font_; }

    // Crop mode (the reference's CropEditor): the item shows the whole
    // image with the editable rectangle on top; the model is untouched
    // until commitCrop().
    bool cropMode() const { return cropMode_; }
    void enterCropMode();
    void exitCropMode();
    QRectF cropRect() const { return cropRect_; }
    void setCropRect(const QRectF &rect);
    // Applies a crop to the model with the geometry bookkeeping Qt
    // needs; commitCrop() also leaves crop mode.
    void setModelCrop(const QRectF &rect);
    void commitCrop(const QRectF &rect);

    // The colour of the displayed pixel under a scene position, or an
    // invalid colour when there is none (outside the item, no level
    // loaded, or a fully transparent pixel), like the reference's
    // sample_color_at.
    QColor sampleColorAt(const QPointF &scenePos) const;

    const QImage &level() const { return level_; }
    // The image actually painted: the grayscale copy when the document
    // item asks for grayscale, the colour level otherwise.
    const QImage &displayLevel() const;
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
    // Bytes of every decoded LOD buffer this item holds: the displayed
    // level, its grayscale copy, and the retained coarsest copy when it
    // is a separate buffer. Counted once even though QImage sharing can
    // make the displayed and coarsest level the same pixels.
    qint64 residentLodBytes() const;

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
    // The item's content without any selection decoration, in local
    // coordinates; the view's peek overlay paints the item again with
    // it, above everything.
    void paintContent(QPainter *painter);

private:
    QSize imageSize() const;
    QRectF computedDisplayBounds() const;
    QString errorText() const;
    void paintCropMode(QPainter *painter);
    // The current view scale, for the screen-sized crop handles.
    double viewportScale() const;
    // Keeps the grayscale copy in step with the level and the flag; a
    // no-op while both are unchanged.
    void updateGrayscaleLevel();

    doc::ItemPtr item_;
    QImage level_;
    double levelFraction_ = 0;
    bool failed_ = false;
    QImage grayscaleLevel_;
    bool grayscaleCached_ = false;
    bool grayscaleOn_ = false;
    bool cropMode_ = false;
    QRectF cropRect_;
    QRectF displayBounds_;
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

