#include "scene_item.h"

#include "crop_tools.h"
#include "grayscale.h"
#include "rendering.h"
#include "theme.h"

#include <QFontMetricsF>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionGraphicsItem>

#include <cmath>
#include <utility>

namespace ui {
namespace {

constexpr int kPlaceholderWidth = 200;
constexpr int kPlaceholderHeight = 150;
constexpr int kErrorWidth = 220;
constexpr double kSelectionLineWidth = 2.0;

} // namespace

SceneItem::SceneItem(doc::ItemPtr item, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , item_(std::move(item))
{
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    displayBounds_ = computedDisplayBounds();
}

QSize SceneItem::imageSize() const
{
    const QSize original = item_->originalSize();
    if (original.isValid() && !original.isEmpty())
        return original;
    if (!level_.isNull() && levelFraction_ > 0) {
        return QSize(qMax(1, qRound(level_.width() / levelFraction_)),
                     qMax(1, qRound(level_.height() / levelFraction_)));
    }
    if (!level_.isNull())
        return level_.size();
    return QSize(kPlaceholderWidth, kPlaceholderHeight);
}

QRectF SceneItem::imageBounds() const
{
    return QRectF(QPointF(0, 0), QSizeF(imageSize()));
}

QColor SceneItem::sampleColorAt(const QPointF &scenePos) const
{
    if (!isPixmap() || level_.isNull())
        return {};
    const QPointF local = mapFromScene(scenePos);
    if (!displayBounds().contains(local))
        return {};

    const QImage &image = displayLevel();
    if (image.isNull())
        return {};

    // Local coordinates are original image pixels; the level is the
    // same image at the current fraction. Clamp rather than reporting
    // no colour when a truncated downscale falls just short of the far
    // edge.
    const double fraction = levelFraction_ > 0 ? levelFraction_ : 1.0;
    const int x = qBound(0, int(local.x() * fraction), image.width() - 1);
    const int y = qBound(0, int(local.y() * fraction), image.height() - 1);
    const QColor color = image.pixelColor(x, y);
    if (color.alpha() == 0)
        return {};
    return color;
}

QString SceneItem::errorText() const
{
    return item_->text().isEmpty() ? QStringLiteral("Cannot load image") : item_->text();
}

QRectF SceneItem::computedDisplayBounds() const
{
    if (isPixmap() && !failed_ && item_->hasCrop())
        return item_->crop();
    return imageBounds();
}

QRectF SceneItem::boundingRect() const
{
    if (isPixmap() && !failed_)
        return cropMode_ ? imageBounds() : displayBounds_;
    if (isText()) {
        const QFontMetricsF metrics(font_);
        const QRectF bounds =
            metrics.boundingRect(QRectF(0, 0, 320, 10000), Qt::TextWordWrap, item_->text());
        return QRectF(0, 0, qMax(bounds.width(), 40.0), qMax(bounds.height(), 20.0));
    }
    const QFontMetricsF metrics(font_);
    const QRectF text =
        metrics.boundingRect(QRectF(0, 0, kErrorWidth - 12, 10000), Qt::TextWordWrap, errorText());
    return QRectF(0, 0, kErrorWidth, qMax(48.0, text.height() + 12));
}

void SceneItem::setLevel(const QImage &image, double fraction)
{
    prepareGeometryChange();
    level_ = image;
    levelFraction_ = fraction > 0 ? fraction : 1.0;
    failed_ = false;
    failures_ = 0;
    lastFailed_ = QDateTime();
    grayscaleCached_ = false;
    updateGrayscaleLevel();
    update();
}

void SceneItem::setLevelUnavailable()
{
    prepareGeometryChange();
    failed_ = true;
    level_ = QImage();
    grayscaleLevel_ = QImage();
    grayscaleCached_ = false;
    update();
}

void SceneItem::noteDecodeFailure()
{
    ++failures_;
    lastFailed_ = QDateTime::currentDateTime();
}

bool SceneItem::retryBlocked() const
{
    static constexpr int kMaxFailures = 3;
    static constexpr qint64 kRetryCooldownMs = 5000;
    if (failures_ < kMaxFailures || !lastFailed_.isValid())
        return false;
    return lastFailed_.msecsTo(QDateTime::currentDateTime()) < kRetryCooldownMs;
}

double SceneItem::coarsestFraction() const
{
    if (!levels_.isEmpty())
        return levels_.first().fraction;
    return levelFraction_ > 0 ? levelFraction_ : 1.0;
}

std::optional<double> SceneItem::coarserFraction(double fraction) const
{
    for (qsizetype i = 0; i < levels_.size(); ++i) {
        if (qFuzzyCompare(levels_.at(i).fraction, fraction) && i > 0)
            return levels_.at(i - 1).fraction;
    }
    return std::nullopt;
}

qint64 SceneItem::levelBytesFor(double fraction) const
{
    for (const Level &level : levels_) {
        if (qFuzzyCompare(level.fraction, fraction))
            return level.decodedBytes();
    }
    if (qFuzzyCompare(levelFraction_, fraction) && !level_.isNull())
        return qint64(level_.width()) * level_.height() * 4;
    return 0;
}

QSize SceneItem::levelSizeFor(double fraction) const
{
    for (const Level &level : levels_) {
        if (qFuzzyCompare(level.fraction, fraction))
            return level.size;
    }
    return scaledLevelSize(imageSize(), fraction);
}

qint64 SceneItem::displayedLevelBytes() const
{
    if (level_.isNull())
        return 0;
    qint64 bytes = qint64(level_.width()) * level_.height() * 4;
    // The grayscale copy is one byte per pixel and lives as long as
    // the level it was derived from.
    if (grayscaleCached_)
        bytes += qint64(grayscaleLevel_.width()) * grayscaleLevel_.height();
    return bytes;
}

qint64 SceneItem::residentLodBytes() const
{
    qint64 bytes = displayedLevelBytes();
    // The retained coarsest level shares its pixels with the displayed
    // level while it is the one shown; only a detached copy adds bytes.
    if (!coarsestLevel_.isNull() && coarsestLevel_.constBits() != level_.constBits())
        bytes += qint64(coarsestLevel_.width()) * coarsestLevel_.height() * 4;
    return bytes;
}

void SceneItem::rememberCoarsestLevel()
{
    if (level_.isNull() || levels_.isEmpty())
        return;
    if (levelFraction_ > 0 && qFuzzyCompare(levelFraction_, levels_.first().fraction)) {
        coarsestLevel_ = level_;
        coarsestLevelFraction_ = levelFraction_;
    }
}

bool SceneItem::hasCoarsestCopy() const
{
    return !coarsestLevel_.isNull() && coarsestLevelFraction_ > 0
        && qFuzzyCompare(coarsestLevelFraction_, coarsestFraction());
}

void SceneItem::applyCoarsestCopy()
{
    if (hasCoarsestCopy())
        setLevel(coarsestLevel_, coarsestLevelFraction_);
}

const QImage &SceneItem::displayLevel() const
{
    return grayscaleCached_ ? grayscaleLevel_ : level_;
}

void SceneItem::updateGrayscaleLevel()
{
    const bool wanted = isPixmap() && grayscaleOn_ && !level_.isNull();
    if (!wanted) {
        // Release the copy (it is a byte per pixel) and repaint only if
        // something was on screen.
        if (!grayscaleLevel_.isNull()) {
            grayscaleLevel_ = QImage();
            update();
        }
        grayscaleCached_ = false;
        return;
    }
    if (grayscaleCached_)
        return;
    grayscaleLevel_ = grayscaleImage(level_);
    grayscaleCached_ = !grayscaleLevel_.isNull();
    if (grayscaleCached_)
        update();
}

void SceneItem::applyModelState()
{
    // The crop can arrive from outside (undo, board reload): announce
    // the geometry change before the cached bounds move.
    const QRectF bounds = computedDisplayBounds();
    if (bounds != displayBounds_) {
        prepareGeometryChange();
        displayBounds_ = bounds;
        setTransformOriginPoint(displayBounds_.center());
    }

    setPos(item_->x, item_->y);
    setZValue(item_->z);
    setTransformOriginPoint(displayBounds().center());
    QTransform transform;
    transform.scale(item_->flip * item_->scale, item_->scale);
    transform.rotate(item_->rotation);
    setTransform(transform);
    setOpacity(qBound(0.0, item_->opacity(), 1.0));

    if (item_->grayscale() != grayscaleOn_)
        grayscaleOn_ = item_->grayscale();
    updateGrayscaleLevel();
}

void SceneItem::enterCropMode()
{
    if (cropMode_)
        return;
    prepareGeometryChange();
    cropMode_ = true;
    cropRect_ = item_->hasCrop() ? item_->crop() : imageBounds();
    update();
}

void SceneItem::exitCropMode()
{
    if (!cropMode_)
        return;
    prepareGeometryChange();
    cropMode_ = false;
    update();
}

void SceneItem::setCropRect(const QRectF &rect)
{
    if (cropRect_ == rect)
        return;
    cropRect_ = rect;
    update();
}

void SceneItem::setModelCrop(const QRectF &rect)
{
    prepareGeometryChange();
    item_->setCrop(rect);
    displayBounds_ = computedDisplayBounds();
    setTransformOriginPoint(displayBounds_.center());
    update();
}

void SceneItem::commitCrop(const QRectF &rect)
{
    setModelCrop(rect);
    cropMode_ = false;
    update();
}

double SceneItem::viewportScale() const
{
    const QList<QGraphicsView *> views =
        scene() ? scene()->views() : QList<QGraphicsView *>();
    if (views.isEmpty())
        return 1.0;
    return std::abs(views.first()->transform().m11());
}

void SceneItem::syncPositionToModel()
{
    item_->x = pos().x();
    item_->y = pos().y();
}

void SceneItem::paintContent(QPainter *painter)
{
    // Stated unconditionally: painter state leaks between items in a
    // paint pass. Smoothing is off during interactions (repaints skip
    // the filter) and when zoomed in past 2x, so pixel sprites stay
    // crisp, as in the reference.
    const bool smooth = rendering::smoothPixmaps() && !rendering::smoothingSuspended()
        && std::abs(painter->combinedTransform().m11()) < 2.0;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, smooth);

    if (isPixmap() && !failed_) {
        if (cropMode_) {
            paintCropMode(painter);
            return;
        }
        const QRectF bounds = imageBounds();
        if (level_.isNull()) {
            painter->setPen(QPen(theme::placeholder, 0, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(bounds);
        } else {
            const QRectF crop = item_->hasCrop() ? item_->crop() : bounds;
            const double fraction = levelFraction_ > 0 ? levelFraction_ : 1.0;
            const QRectF source(crop.x() * fraction, crop.y() * fraction,
                                crop.width() * fraction, crop.height() * fraction);
            painter->drawImage(crop, displayLevel(), source);
        }
    } else if (isText()) {
        painter->setPen(theme::text);
        painter->drawText(boundingRect(), Qt::TextWordWrap, item_->text());
    } else {
        const QRectF bounds = boundingRect();
        QColor background = theme::error;
        background.setAlpha(40);
        painter->setPen(QPen(theme::error, 1));
        painter->setBrush(background);
        painter->drawRoundedRect(bounds, 4, 4);
        painter->setPen(theme::text);
        painter->drawText(bounds.adjusted(6, 4, -6, -4), Qt::TextWordWrap, errorText());
    }
}

void SceneItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    paintContent(painter);

    // The crop editor's frame already marks the item; an outline would
    // only get in the way (and the overlay never spotlights it).
    if (cropMode_)
        return;

    if (option && (option->state & QStyle::State_Selected)) {
        QPen pen(theme::selection, kSelectionLineWidth);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
    }
}

void SceneItem::paintCropMode(QPainter *painter)
{
    // The whole image, so the user sees what is being cut away.
    const QRectF image = imageBounds();
    if (!level_.isNull()) {
        const double fraction = levelFraction_ > 0 ? levelFraction_ : 1.0;
        const QRectF source(image.x() * fraction, image.y() * fraction,
                            image.width() * fraction, image.height() * fraction);
        painter->drawImage(image, displayLevel(), source);
    } else {
        painter->setPen(QPen(theme::placeholder, 0, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(image);
    }

    // Darken everything outside the editable rectangle: the reference
    // fills the image shape plus the crop rectangle with the odd-even
    // rule, which leaves exactly that ring.
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    path.addRect(image);
    path.addRect(cropRect_);
    QColor dim(0, 0, 0);
    dim.setAlpha(100);
    painter->fillPath(path, dim);

    // The rectangle and its handles: a solid white outline with a black
    // dotted line on top of it, both cosmetic (screen-sized).
    const double scale = viewportScale() * item_->scale;
    const crop::Part handles[] = {crop::Part::TopLeft, crop::Part::BottomLeft,
                                  crop::Part::BottomRight, crop::Part::TopRight};
    painter->setBrush(Qt::NoBrush);

    QPen pen(QColor(255, 255, 255), 2);
    pen.setCosmetic(true);
    painter->setPen(pen);
    for (crop::Part part : handles)
        painter->drawRect(crop::handleRect(cropRect_, part, scale));
    painter->drawRect(cropRect_);

    pen.setColor(QColor(0, 0, 0));
    pen.setStyle(Qt::DotLine);
    painter->setPen(pen);
    for (crop::Part part : handles)
        painter->drawRect(crop::handleRect(cropRect_, part, scale));
    painter->drawRect(cropRect_);
}

} // namespace ui