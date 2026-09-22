#include "scene.h"

#include <QImage>

#include <utility>

namespace ui {
namespace {

constexpr double kSceneMargin = 100.0;

} // namespace

Scene::Scene(QObject *parent)
    : QGraphicsScene(parent)
{
}

void Scene::setDocument(std::shared_ptr<doc::Document> document)
{
    document_ = std::move(document);
    rebuild();
}

void Scene::rebuild()
{
    clear();
    if (!document_)
        return;

    for (const doc::ItemPtr &item : document_->items()) {
        auto *view = new SceneItem(item);
        addItem(view);
        view->applyModelState();

        if (item->isPixmap()) {
            if (!item->floorData.isEmpty()) {
                const QImage floor = QImage::fromData(item->floorData);
                if (!floor.isNull()) {
                    view->setLevel(floor,
                                   item->floorFraction > 0 ? item->floorFraction : 1.0);
                }
            }
            if (!item->hasSource())
                view->setLevelUnavailable();
        }
    }
    updateSceneRect();
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

QRectF Scene::selectionBounds() const
{
    QRectF bounds;
    for (QGraphicsItem *item : selectedItems()) {
        bounds = bounds.isNull() ? item->sceneBoundingRect()
                                 : bounds.united(item->sceneBoundingRect());
    }
    return bounds;
}

void Scene::updateSceneRect()
{
    const QRectF bounds = itemsBoundingRect();
    if (bounds.isEmpty()) {
        setSceneRect(QRectF());
        return;
    }
    setSceneRect(bounds.adjusted(-kSceneMargin, -kSceneMargin, kSceneMargin, kSceneMargin));
}

} // namespace ui
