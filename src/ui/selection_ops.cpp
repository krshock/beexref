#include "selection_ops.h"

#include "constants.h"

#include "scene.h"
#include "scene_item.h"

#include <memory>

namespace ui::selection {
namespace {

QVector<SceneItem *> actionItems(const Scene &scene)
{
    return selectionItems(scene);
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

QVector<SceneItem *> selectionItems(const Scene &scene)
{
    QVector<SceneItem *> items;
    for (SceneItem *view : scene.selectedItemViews()) {
        if (view->isError())
            continue;
        items.append(view);
    }
    return items;
}

QVector<SceneItem *> imageSelection(const Scene &scene)
{
    QVector<SceneItem *> items;
    for (SceneItem *view : scene.selectedItemViews()) {
        if (view->isPixmap() && !view->isError())
            items.append(view);
    }
    return items;
}

void applyOpacity(const Scene &scene, double opacity)
{
    for (SceneItem *view : imageSelection(scene)) {
        view->item()->setOpacity(opacity);
        view->applyModelState();
    }
}

namespace {

// Applies a data change (opacity, grayscale) to the selected images and
// records one undo step per changed item, collected in a macro. An
// action that changes nothing leaves no history entry.
void applyDataChange(const Scene &scene, doc::UndoStack &stack, const QString &text,
                     const std::function<void(doc::Item &)> &mutate)
{
    const QVector<SceneItem *> items = imageSelection(scene);
    if (items.isEmpty())
        return;

    QVector<doc::ChangeItemCommand::State> before;
    before.reserve(items.size());
    for (SceneItem *view : items)
        before.append(doc::ChangeItemCommand::State::capture(*view->item()));

    QVector<QPair<SceneItem *, doc::ChangeItemCommand::State>> changed;
    for (qsizetype i = 0; i < items.size(); ++i) {
        mutate(*items.at(i)->item());
        const doc::ChangeItemCommand::State after =
            doc::ChangeItemCommand::State::capture(*items.at(i)->item());
        if (after != before.at(i))
            changed.append({items.at(i), before.at(i)});
    }

    if (!changed.isEmpty()) {
        stack.beginMacro(text);
        for (const auto &pair : changed) {
            const doc::ChangeItemCommand::State after =
                doc::ChangeItemCommand::State::capture(*pair.first->item());
            stack.push(std::make_unique<doc::ChangeItemCommand>(
                pair.first->item(), pair.second, after, text));
        }
        stack.endMacro();
    }

    // The models are already in their final state; bring the views in
    // line (the grayscale copy is rebuilt here).
    for (SceneItem *view : items)
        view->applyModelState();
}

} // namespace

void setOpacity(const Scene &scene, doc::UndoStack &stack, double opacity)
{
    applyDataChange(scene, stack, QStringLiteral("Change opacity"),
                    [opacity](doc::Item &item) { item.setOpacity(opacity); });
}

void setGrayscale(const Scene &scene, doc::UndoStack &stack, bool grayscale)
{
    applyDataChange(scene, stack, QStringLiteral("Grayscale"),
                    [grayscale](doc::Item &item) { item.setGrayscale(grayscale); });
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
    // One history entry for the whole selection, like the reference's
    // single ResetScale command.
    stack.beginMacro(QStringLiteral("Reset scale"));
    for (SceneItem *view : items) {
        const QPointF anchor = itemAnchor(view);
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*view->item());
        transformAroundAnchor(view, anchor, [view]() { view->item()->scale = 1.0; });
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Reset scale")));
    }
    stack.endMacro();
}

void resetRotation(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    stack.beginMacro(QStringLiteral("Reset rotation"));
    for (SceneItem *view : items) {
        const QPointF anchor = itemAnchor(view);
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*view->item());
        transformAroundAnchor(view, anchor, [view]() { view->item()->rotation = 0.0; });
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Reset rotation")));
    }
    stack.endMacro();
}

void resetFlip(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    // Items that are not flipped are skipped; if that leaves nothing to
    // do, endMacro() records no entry at all.
    stack.beginMacro(QStringLiteral("Reset flip"));
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
    stack.endMacro();
}

namespace {

// Applies one z delta to the whole selection as a single history step.
void changeZ(doc::UndoStack &stack, const QVector<SceneItem *> &items, double delta,
             const QString &text)
{
    QVector<doc::ChangeItemCommand::State> before;
    before.reserve(items.size());
    for (SceneItem *view : items)
        before.append(doc::ChangeItemCommand::State::capture(*view->item()));

    stack.beginMacro(text);
    for (qsizetype i = 0; i < items.size(); ++i) {
        items.at(i)->item()->z += delta;
        items.at(i)->applyModelState();
        const doc::ChangeItemCommand::State after =
            doc::ChangeItemCommand::State::capture(*items.at(i)->item());
        if (after != before.at(i))
            stack.push(std::make_unique<doc::ChangeItemCommand>(items.at(i)->item(), before.at(i),
                                                                after, text));
    }
    stack.endMacro();
}

} // namespace

void raiseToTop(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    if (items.isEmpty())
        return;

    // delta = scene maximum + step - the selection's minimum, so the
    // selection keeps its internal order and ends up above everything.
    double maxZ = 0;
    bool first = true;
    for (SceneItem *view : scene.itemViews()) {
        maxZ = first ? view->item()->z : qMax(maxZ, view->item()->z);
        first = false;
    }
    double minSelected = 0;
    first = true;
    for (SceneItem *view : items) {
        minSelected = first ? view->item()->z : qMin(minSelected, view->item()->z);
        first = false;
    }
    changeZ(stack, items, maxZ + constants::kZStep - minSelected,
            QStringLiteral("Raise to top"));
}

void lowerToBottom(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    if (items.isEmpty())
        return;

    double minZ = 0;
    bool first = true;
    for (SceneItem *view : scene.itemViews()) {
        minZ = first ? view->item()->z : qMin(minZ, view->item()->z);
        first = false;
    }
    double maxSelected = 0;
    first = true;
    for (SceneItem *view : items) {
        maxSelected = first ? view->item()->z : qMax(maxSelected, view->item()->z);
        first = false;
    }
    changeZ(stack, items, minZ - constants::kZStep - maxSelected,
            QStringLiteral("Lower to bottom"));
}

void resetCrop(const Scene &scene, doc::UndoStack &stack)
{
    applyDataChange(scene, stack, QStringLiteral("Reset crop"), [](doc::Item &item) {
        const QSize original = item.originalSize();
        item.setCrop(original.isValid() && !original.isEmpty()
                         ? QRectF(0, 0, original.width(), original.height())
                         : QRectF());
    });
}

void resetTransforms(const Scene &scene, doc::UndoStack &stack)
{
    const QVector<SceneItem *> items = actionItems(scene);
    // One history entry for the whole selection, like the reference's
    // single ResetTransforms command.
    stack.beginMacro(QStringLiteral("Reset transformations"));
    for (SceneItem *view : items) {
        const doc::ChangeItemCommand::State before =
            doc::ChangeItemCommand::State::capture(*view->item());
        // The reference resets the crop first and anchors the other
        // transforms at the centre that leaves.
        if (view->isPixmap()) {
            const QSize original = view->item()->originalSize();
            view->setModelCrop(original.isValid() && !original.isEmpty()
                                   ? QRectF(0, 0, original.width(), original.height())
                                   : QRectF());
        }
        const QPointF anchor = itemAnchor(view);
        transformAroundAnchor(view, anchor, [view]() {
            view->item()->scale = 1.0;
            view->item()->rotation = 0.0;
            view->item()->flip = 1.0;
        });
        stack.push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Reset transformations")));
    }
    stack.endMacro();
}

} // namespace ui::selection
