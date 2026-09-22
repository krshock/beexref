#pragma once

#include "doc/item.h"

#include <QFont>
#include <QGraphicsItem>
#include <QImage>

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

    quint64 pendingRequest() const { return pendingRequest_; }
    void setPendingRequest(quint64 requestId) { pendingRequest_ = requestId; }

    bool levelUnavailable() const { return failed_; }
    void setLevelUnavailable();

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
    quint64 pendingRequest_ = 0;
    bool failed_ = false;
    QFont font_;
};

} // namespace ui
