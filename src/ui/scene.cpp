#include "scene.h"

#include <QImage>
#include <QSet>

#include <utility>

namespace ui {
namespace {

} // namespace

Scene::Scene(QObject *parent)
    : QGraphicsScene(parent)
{
}

Scene::~Scene()
{
    emit aboutToBeDestroyed();
}

void Scene::setDocument(std::shared_ptr<doc::Document> document)
{
    document_ = std::move(document);
    rebuild();
}

void Scene::rebuild()
{
    // Invalidate scheduler state before views are deleted.
    emit itemsChanged();
    clear();
    syncDocument();
}

void Scene::syncDocument()
{
    if (!document_) {
        emit itemsChanged();
        clear();
        return;
    }

    QSet<const doc::Item *> live;
    for (const doc::ItemPtr &item : document_->items())
        live.insert(item.get());

    const QVector<SceneItem *> views = itemViews();
    for (SceneItem *view : views) {
        if (!live.contains(view->item().get())) {
            emit itemViewAboutToBeRemoved(view);
            removeItem(view);
            delete view;
        }
    }

    for (const doc::ItemPtr &item : document_->items()) {
        SceneItem *view = itemViewFor(item);
        if (!view) {
            view = new SceneItem(item);
            addItem(view);
            applyPlaceholder(view);
        }
        view->applyModelState();
    }
    emit itemsChanged();
}

void Scene::applyPlaceholder(SceneItem *view)
{
    const doc::ItemPtr &item = view->item();
    if (!item->isPixmap())
        return;
    if (!item->floorData.isEmpty()) {
        const QImage floor = QImage::fromData(item->floorData);
        if (!floor.isNull()) {
            view->setLevel(floor, item->floorFraction > 0 ? item->floorFraction : 1.0);
            return;
        }
    }
    if (!item->hasSource())
        view->setLevelUnavailable();
}

QVector<SceneItem *> Scene::itemViews() const
{
    QVector<SceneItem *> views;
    for (QGraphicsItem *item : items()) {
        if (auto *view = dynamic_cast<SceneItem *>(item))
            views.append(view);
    }
    return views;
}

QVector<SceneItem *> Scene::pixmapItemViews() const
{
    QVector<SceneItem *> views;
    for (SceneItem *view : itemViews()) {
        if (view->isPixmap())
            views.append(view);
    }
    return views;
}

QVector<SceneItem *> Scene::selectedItemViews() const
{
    QVector<SceneItem *> views;
    for (QGraphicsItem *item : selectedItems()) {
        if (auto *view = dynamic_cast<SceneItem *>(item))
            views.append(view);
    }
    return views;
}

SceneItem *Scene::itemViewFor(const doc::ItemPtr &item) const
{
    for (SceneItem *view : itemViews()) {
        if (view->item() == item)
            return view;
    }
    return nullptr;
}

QRectF Scene::selectionBounds() const
{
    QRectF bounds;
    for (QGraphicsItem *item : selectedItems()) {
        bounds = bounds.isNull() ? item->sceneBoundingRect()
                                 : bounds.united(item->sceneBoundingRect());
    }
    return bounds;
}

} // namespace ui
