#pragma once

#include "doc/document.h"
#include "scene_item.h"

#include <QGraphicsScene>

#include <memory>

namespace ui {

// The canvas scene: one SceneItem per document item. Selection is
// Qt-driven (ItemIsSelectable plus the view's rubber band); the scene
// only exposes the queries the view and window need.
class Scene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit Scene(QObject *parent = nullptr);

    void setDocument(std::shared_ptr<doc::Document> document);
    const std::shared_ptr<doc::Document> &document() const { return document_; }

    // Recreates every canvas item from the document.
    void rebuild();

    QVector<SceneItem *> itemViews() const;
    QVector<SceneItem *> pixmapItemViews() const;

    // Bounding rect of the selected items, in scene coordinates.
    QRectF selectionBounds() const;

    // Keeps the scene rect in sync with the item bounds.
    void updateSceneRect();

private:
    std::shared_ptr<doc::Document> document_;
};

} // namespace ui
