#include "crop_tool.h"

#include "scene.h"
#include "scene_item.h"
#include "view.h"

#include "doc/undo.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <memory>

namespace ui {

CropTool::CropTool(View *view)
    : view_(view)
{
}

void CropTool::start()
{
    if (item_)
        return;
    // Only one tool runs at a time.
    view_->cancelModes();
    SceneItem *target = nullptr;
    if (Scene *scene = view_->boardScene()) {
        const QVector<SceneItem *> selected = scene->selectedItemViews();
        if (selected.size() == 1 && selected.first()->isPixmap() && !selected.first()->isError())
            target = selected.first();
    }
    if (!target)
        return;

    item_ = target;
    drag_ = crop::Part::None;
    item_->enterCropMode();
    // The crop editor must not be hidden behind other items: the item
    // goes into the spotlight (a view-only raise), which also leaves it
    // raised once the crop is confirmed.
    view_->setSpotlight({item_});
    view_->setFocus();
    view_->sessionFinished(false);
}

void CropTool::cancel()
{
    if (!item_)
        return;
    item_->exitCropMode();
    item_ = nullptr;
    drag_ = crop::Part::None;
    view_->sessionFinished(false);
}

void CropTool::confirm()
{
    if (!item_)
        return;
    SceneItem *item = item_;
    item_ = nullptr;
    drag_ = crop::Part::None;

    const QRectF current =
        item->item()->hasCrop() ? item->item()->crop() : item->imageBounds();
    const QRectF rect = item->cropRect();
    const bool changed = rect != current;
    if (!changed) {
        item->exitCropMode();
        view_->sessionFinished(false);
        return;
    }

    const doc::ChangeItemCommand::State before =
        doc::ChangeItemCommand::State::capture(*item->item());
    item->commitCrop(rect);
    if (doc::UndoStack *stack = view_->undoStack()) {
        stack->push(std::make_unique<doc::ChangeItemCommand>(
            item->item(), before, doc::ChangeItemCommand::State::capture(*item->item()),
            QStringLiteral("Crop item")));
    }
    view_->sessionFinished(true);
}

void CropTool::cancelIfItem(SceneItem *item)
{
    if (item_ != item)
        return;
    // The scene is about to delete it: only drop the reference.
    item_ = nullptr;
    drag_ = crop::Part::None;
}

bool CropTool::mousePress(QMouseEvent *event)
{
    if (!item_ || event->button() != Qt::LeftButton)
        return false;
    const QPoint viewportPos = event->position().toPoint();
    const QPointF itemPos = item_->mapFromScene(view_->mapToScene(viewportPos));
    const crop::Part part = crop::hitTest(item_->cropRect(), view_->scaleFor(item_), itemPos);
    if (part != crop::Part::None) {
        drag_ = part;
        pressItem_ = itemPos;
        dragStartRect_ = item_->cropRect();
    } else if (item_->cropRect().contains(itemPos)) {
        // Clicking inside confirms, outside cancels.
        confirm();
    } else {
        cancel();
    }
    return true;
}

bool CropTool::mouseMove(QMouseEvent *event)
{
    if (!item_)
        return false;
    const QPoint position = event->position().toPoint();
    if (drag_ != crop::Part::None && (event->buttons() & Qt::LeftButton)) {
        const QPointF itemPos = item_->mapFromScene(view_->mapToScene(position));
        const QPointF delta = itemPos - pressItem_;
        item_->setCropRect(
            crop::draggedRect(dragStartRect_, drag_, delta, item_->imageBounds()));
    } else {
        updateHoverCursor(position);
    }
    return true;
}

bool CropTool::mouseRelease(QMouseEvent *event)
{
    if (!item_)
        return false;
    if (event->button() == Qt::LeftButton)
        drag_ = crop::Part::None;
    return true;
}

bool CropTool::keyPress(QKeyEvent *event)
{
    if (!item_)
        return false;
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        confirm();
        return true;
    }
    if (event->key() == Qt::Key_Escape) {
        cancel();
        return true;
    }
    return false;
}

void CropTool::updateHoverCursor(const QPoint &viewportPos)
{
    if (!item_) {
        view_->viewport()->unsetCursor();
        return;
    }
    const QPointF itemPos = item_->mapFromScene(view_->mapToScene(viewportPos));
    const crop::Part part = crop::hitTest(item_->cropRect(), view_->scaleFor(item_), itemPos);
    switch (part) {
    case crop::Part::None:
        view_->viewport()->unsetCursor();
        return;
    case crop::Part::Top:
    case crop::Part::Left:
    case crop::Part::Bottom:
    case crop::Part::Right:
        view_->viewport()->setCursor(crop::edgeCursor(part, item_->item()->rotation));
        return;
    default:
        view_->viewport()->setCursor(
            crop::handleCursor(part, item_->item()->rotation, item_->item()->flip < 0));
        return;
    }
}

} // namespace ui
