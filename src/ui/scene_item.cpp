#include "scene_item.h"

#include "rendering.h"
#include "theme.h"

#include <QFontMetricsF>
#include <QPainter>
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

QString SceneItem::errorText() const
{
    return item_->text().isEmpty() ? QStringLiteral("Cannot load image") : item_->text();
}

QRectF SceneItem::boundingRect() const
{
    if (isPixmap() && !failed_)
        return imageBounds();
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
    update();
}

void SceneItem::setLevelUnavailable()
{
    prepareGeometryChange();
    failed_ = true;
    level_ = QImage();
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
    return qint64(level_.width()) * level_.height() * 4;
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

void SceneItem::applyModelState()
{
    setPos(item_->x, item_->y);
    setZValue(item_->z);
    setTransformOriginPoint(imageBounds().center());
    QTransform transform;
    transform.scale(item_->flip * item_->scale, item_->scale);
    transform.rotate(item_->rotation);
    setTransform(transform);
    setOpacity(qBound(0.0, item_->opacity(), 1.0));
}

void SceneItem::syncPositionToModel()
{
    item_->x = pos().x();
    item_->y = pos().y();
}

void SceneItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    // Stated unconditionally: painter state leaks between items in a
    // paint pass. Smoothing is off during interactions (repaints skip
    // the filter) and when zoomed in past 2x, so pixel sprites stay
    // crisp, as in the reference.
    const bool smooth = rendering::smoothPixmaps() && !rendering::smoothingSuspended()
        && std::abs(painter->combinedTransform().m11()) < 2.0;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, smooth);

    if (isPixmap() && !failed_) {
        const QRectF bounds = imageBounds();
        if (level_.isNull()) {
            painter->setPen(QPen(theme::placeholder, 0, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(bounds);
        } else {
            const QRectF crop = item_->crop().isValid() ? item_->crop() : bounds;
            const double fraction = levelFraction_ > 0 ? levelFraction_ : 1.0;
            const QRectF source(crop.x() * fraction, crop.y() * fraction,
                                crop.width() * fraction, crop.height() * fraction);
            painter->drawImage(crop, level_, source);
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

    if (option && (option->state & QStyle::State_Selected)) {
        QPen pen(theme::selection, kSelectionLineWidth);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
    }
}

} // namespace ui
