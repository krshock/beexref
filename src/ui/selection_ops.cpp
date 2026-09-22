#include "selection_ops.h"

#include "scene.h"
#include "scene_item.h"

#include <memory>

namespace ui::selection {
namespace {

QVector<SceneItem *> actionItems(const Scene &scene)
{
    QVector<SceneItem *> items;
    for (SceneItem *view : scene.selectedItemViews()) {
        if (view->isError())
            continue;
        items.append(view);
    }
    return items;
}

// Captures the before state, applies `mutate` to the model of each item
// around `anchor` (in scene coordinates), and pushes one undo step.
void applyAction(const Scene &scene, doc::UndoStack &stack, const QString &text,
                 const QPointF &anchorScene, const std::function<void(SceneItem *)> &mutate)
{
    const QVector<SceneItem *> items = actionItems(scene);
    if (items.isEmpty())
        return;

    QVector<doc::ChangeItemCommand::State> before;
    before.reserve(items.size());
    for (SceneItem *view : items)
        before.append(doc::ChangeItemCommand::State::capture(*view->item()));

    anchorUnused:
    for (SceneItem *view : items) {
        transformAroundAnchor(view, anchorScene, [&]() { mutate(view); });
    }

    stack.beginMacro(text);
    for (qsizetype i = 0; i < items.size(); ++i) {
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            items.at(i)->item(), before.at(i),
            doc::ChangeItemCommand::State::capture(*items.at(i)->item()), text));
    }
    stack.endMacro();
}

QPointF itemAnchor(const SceneItem *view)
{
    return view->mapToScene(view->boundingRect().center());
}

} // namespace

void transformAroundAnchor(SceneItem *view, const QPointF &anchorScene,
                           const std::function<void()> &mutate)
{
    const QPointF anchorItem = view->mapFromScene(anchorScene);
    const QPointF before = view->mapToScene(anchorItem);
    mutate();
    view->applyModelState();
    const QPointF diff = view->mapToScene(anchorItem) - before;
    view->setPos(view->pos() - diff);
    view->syncPositionToModel();
}

void flip(const Scene &scene, doc::UndoStack &stack, bool vertical)
{
    const QRectF bounds = scene.selectionBounds();
    if (bounds.isEmpty())
        return;
    applyAction(scene, stack, vertical ? QStringLiteral("Flip vertically")
                                       : QStringLiteral("Flip horizontally"),
                bounds.center(), [vertical](SceneItem *view) {
                    view->item()->flip = -view->item()->flip;
                    if (vertical)
                        view->item()->rotation += 180.0;
                });
}

void resetScale(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    for (SceneItem *view : items) {
        const QPointF anchor = itemAnchor(view);
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*view->item());
        transformAroundAnchor(view, anchor, [view]() { view->item()->scale = 1.0; });
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Reset scale")));
    }
}

void resetRotation(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    for (SceneItem *view : items) {
        const QPointF anchor = itemAnchor(view);
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*view->item());
        transformAroundAnchor(view, anchor, [view]() { view->item()->rotation = 0.0; });
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Reset rotation")));
    }
}

void resetFlip(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    for (SceneItem *view : items) {
        if (view->item()->flip > 0)
            continue;
        const QPointF anchor = itemAnchor(view);
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*view->item());
        transformAroundAnchor(view, anchor, [view]() { view->item()->flip = 1.0; });
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Reset flip")));
    }
}

void resetTransforms(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    for (SceneItem *view : items) {
        const QPointF anchor = itemAnchor(view);
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*view->item());
        transformAroundAnchor(view, anchor, [view]() {
            view->item()->scale = 1.0;
            view->item()->rotation = 0.0;
            view->item()->flip = 1.0;
        });
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Reset transformations")));
    }
}

} // namespace ui::selection
