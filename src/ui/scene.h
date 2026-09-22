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
    ~Scene() override;

    void setDocument(std::shared_ptr<doc::Document> document);
    const std::shared_ptr<doc::Document> &document() const { return document_; }

    // Recreates every canvas item from the document.
    void rebuild();

    // Brings the views in line with the document after commands ran:
    // removes views for dropped items, adds views for new ones, and
    // reapplies model state to the rest.
    void syncDocument();

    QVector<SceneItem *> itemViews() const;
    QVector<SceneItem *> pixmapItemViews() const;
    QVector<SceneItem *> selectedItemViews() const;
    SceneItem *itemViewFor(const doc::ItemPtr &item) const;

    // Bounding rect of the selected items, in scene coordinates.
    QRectF selectionBounds() const;

signals:
    // Emitted first thing during destruction, before QGraphicsScene
    // deletes the canvas items: observers drop their pointers while the
    // scene and its items are still intact.
    void aboutToBeDestroyed();
    // The set of canvas items changed (document replaced, items added or
    // removed by commands).
    void itemsChanged();
    // Emitted just before a view is deleted, so scheduler state can be
    // dropped before the pointer dangles.
    void itemViewAboutToBeRemoved(SceneItem *view);

private:
    // Applies the saved floor thumbnail, or the unavailable marker, to
    // a newly created view.
    void applyPlaceholder(SceneItem *view);

    std::shared_ptr<doc::Document> document_;
};

} // namespace ui
