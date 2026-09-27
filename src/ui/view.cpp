#include "view.h"

#include "color_swatch.h"
#include "cursors.h"
#include "lod_manager.h"
#include "move_handle.h"
#include "rendering.h"
#include "selection_ops.h"
#include "theme.h"

#include "doc/undo.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QGuiApplication>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
#include <QCursor>
#include <QKeyEvent>
#include <QWindow>
#include <QWheelEvent>

#include <cmath>
#include <memory>

namespace ui {

View::View(QWidget *parent)
    : QGraphicsView(parent)
{
    lod_ = new LodManager(this);

    // Move-window mode: the window follows the global cursor on a timer,
    // so it keeps moving when the pointer leaves the window (widget
    // mouse events stop at the edge, which used to strand the mode).
    moveWindowTimer_ = new QTimer(this);
    moveWindowTimer_->setInterval(10);
    connect(moveWindowTimer_, &QTimer::timeout, this, &View::moveWindowTick);

    // The corner move handle: shown only while the window's title bar
    // is disabled (see setMoveHandleVisible). Pressing it starts the
    // platform's interactive window move (a title-bar drag), falling
    // back to the timer-driven mode where the platform lacks it. It is
    // a HUD element: a child of the view, like the toasts, so it floats
    // above the viewport instead of living inside it.
    moveHandle_ = new MoveHandle(this);
    moveHandle_->hide();
    connect(moveHandle_, &MoveHandle::moveRequested, this, [this]() {
        QWindow *handle = window() ? window()->windowHandle() : nullptr;
        if (!handle || !handle->startSystemMove())
            enterMoveWindow();
    });
    positionMoveHandle();

    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setOptimizationFlags(QGraphicsView::DontSavePainterState
                         | QGraphicsView::DontAdjustForAntialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(theme::canvas);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    // The viewport is the widget under the pointer: without tracking
    // here, hover moves never reach mouseMoveEvent (the selection-handle
    // cursors rely on them).
    viewport()->setMouseTracking(true);
    setAcceptDrops(true);
    bindings_ = controls::Bindings::load();
}

View::~View()
{
    // The scene is usually a child of the view. During teardown the
    // base class deletes it while the viewport is already gone, and
    // its selectionChanged() would then reach a dead widget; cut the
    // connections while both are still alive.
    if (boardScene_)
        disconnect(boardScene_, nullptr, this, nullptr);
}

void View::setLevelLoader(LevelLoader *loader)
{
    loader_ = loader;
    lod_->setLoader(loader);
}

void View::setBoardScene(Scene *scene)
{
    if (boardScene_) {
        disconnect(boardScene_, nullptr, this, nullptr);
    }
    // The spotlight belongs to the old board's views.
    spotlighted_.clear();
    boardScene_ = scene;
    if (boardScene_) {
        // Drop scheduler state for a view before the scene deletes it,
        // so an in-flight decode can never touch freed memory.
        connect(boardScene_, &Scene::itemViewAboutToBeRemoved, this,
                [this](SceneItem *view) {
                    // Never keep a crop session or a spotlight pointing
                    // at a view the scene is about to delete.
                    if (view == cropItem_) {
                        cropItem_ = nullptr;
                        cropDrag_ = crop::Part::None;
                    }
                    if (view == textItem_)
                        cancelTextEdit();
                    spotlighted_.removeAll(view);
                    lod_->forgetItem(view);
                });
        // New or removed items change the scrollable area.
        connect(boardScene_, &Scene::itemsChanged, this, [this]() { recalculateSceneRect(); });
        // A pure selection change moves the overlay without changing
        // any item, so itemsChanged() does not fire for it.
        connect(boardScene_, &QGraphicsScene::selectionChanged, this, [this]() {
            refreshSelectionOverlay();
            // Selection is an LOD hint: the selected image moves to the
            // Selected band (highest decode priority). Coalesced, so a
            // multi-selection is one evaluation.
            lod_->schedule();
        });
        // The scene may be destroyed before the view (a window owns
        // both). Drop every reference while both are still intact.
        connect(boardScene_, &Scene::aboutToBeDestroyed, this, [this]() {
            disconnect(boardScene_, nullptr, this, nullptr);
            boardScene_ = nullptr;
            spotlighted_.clear();
            lod_->setScene(nullptr);
        });
    }
    lod_->setScene(scene);
    QGraphicsView::setScene(scene);
    updateViewState();
    recalculateSceneRect();
    lod_->schedule();
}

QRectF View::selectionOverlayRegion() const
{
    if (!boardScene_)
        return QRectF();
    const QRectF bounds = boardScene_->selectionBounds();
    if (bounds.isEmpty())
        return QRectF();
    const double scale = transform().m11();
    if (scale <= 0.0)
        return QRectF();

    // The outline sits on the bounds and the handle dots reach half a
    // handle size beyond them; the hover-only scale, rotation and flip
    // areas reach a little further, which this covers as well.
    const double margin = selection::kResizeSize / scale;
    return bounds.adjusted(-margin, -margin, margin, margin);
}

void View::refreshSelectionOverlay()
{
    const QRectF region = selectionOverlayRegion();
    if (region == overlayRegion_)
        return;

    // Repaint where the overlay was as well as where it is now.
    const QRectF dirty = overlayRegion_.isNull() ? region
        : region.isNull()                      ? overlayRegion_
                                               : overlayRegion_.united(region);
    overlayRegion_ = region;
    if (dirty.isNull() || !viewport())
        return;
    viewport()->update(mapFromScene(dirty).boundingRect().adjusted(-2, -2, 2, 2));
}

void View::recalculateSceneRect()
{
    refreshSelectionOverlay();
    // The scrollable area is the items' bounding box expanded by one
    // viewport on each side, so the canvas always offers room to pan
    // (the reference's "impression of an infinite canvas"). Clamping
    // the scene rect to the items instead makes panning impossible
    // when everything fits.
    if (!boardScene_)
        return;
    const QRectF items = boardScene_->itemsBoundingRect();
    if (items.isEmpty()) {
        boardScene_->setSceneRect(QRectF());
        return;
    }
    const QSize size = viewport()->size();
    const QPointF topLeft =
        mapToScene(mapFromScene(items.topLeft()) - QPoint(size.width(), size.height()));
    const QPointF bottomRight =
        mapToScene(mapFromScene(items.bottomRight()) + QPoint(size.width(), size.height()));
    const QRectF rect(topLeft, bottomRight);
    if (!std::isfinite(rect.x()) || !std::isfinite(rect.y()) || rect.width() > 1.0e9
        || rect.height() > 1.0e9) {
        return;
    }

    // Qt keeps the view anchored when the scene rect changes, so the
    // canvas does not jump; re-centring here would drift it instead
    // (scrollbar quantization, compounding over a gesture).
    boardScene_->setSceneRect(rect);
}

void View::setLodSettings(const LodSettings &settings)
{
    lod_->setSettings(settings);
}

void View::updateViewState()
{
    if (!scene())
        return;
    lod_->setViewState(mapToScene(viewport()->rect()).boundingRect(), transform().m11());
    // The overlay's margins are scaled by the zoom.
    refreshSelectionOverlay();
}

void View::beginInteraction()
{
    rendering::setSmoothingSuspended(true);
}

void View::restoreSmoothing()
{
    if (QApplication::mouseButtons() != Qt::NoButton) {
        // A drag is still in progress; the release event restores
        // smoothing.
        return;
    }
    rendering::setSmoothingSuspended(false);
    viewport()->update();
}

void View::panBy(const QPoint &delta)
{
    if (!scene() || scene()->items().isEmpty())
        return;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + delta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + delta.y());
}

double View::zoomExtent(bool maximum) const
{
    if (!scene())
        return 0;
    const QRectF rect = scene()->itemsBoundingRect();
    if (rect.isEmpty())
        return 0;
    const QPoint topLeft = mapFromScene(rect.topLeft());
    const QPoint bottomRight = mapFromScene(rect.bottomRight());
    const double width = bottomRight.x() - topLeft.x();
    const double height = bottomRight.y() - topLeft.y();
    return maximum ? qMax(width, height) : qMin(width, height);
}

void View::zoomAt(int delta, const QPoint &anchor)
{
    if (delta == 0 || !scene() || scene()->items().isEmpty())
        return;

    const double factor = 1.0 + std::abs(delta / 1000.0);
    const QPointF sceneAnchor = mapToScene(anchor);
    if (delta > 0) {
        if (zoomExtent(true) >= kMaxZoomExtent)
            return;
        scale(factor, factor);
    } else {
        if (zoomExtent(false) <= kMinZoomExtent)
            return;
        scale(1.0 / factor, 1.0 / factor);
    }
    panBy(mapFromScene(sceneAnchor) - anchor);
    updateViewState();
    recalculateSceneRect();
    // Keep the levels as they are while zooming; the evaluation runs
    // kZoomInhibitMs after the last zoom event.
    lod_->hold(kZoomInhibitMs);
}

void View::fitRect(const QRectF &rect)
{
    if (!scene() || rect.isEmpty())
        return;
    fitInView(rect, Qt::KeepAspectRatio);
    recalculateSceneRect();
    // Fitting a second time is more reliable: a changed scene rect can
    // mess up the first fitting, as the reference notes.
    fitInView(rect, Qt::KeepAspectRatio);
    updateViewState();
    lod_->evaluateNow();
}

void View::fitScene()
{
    fitRect(scene() ? scene()->itemsBoundingRect() : QRectF());
}

void View::fitSelection()
{
    fitRect(boardScene_ ? boardScene_->selectionBounds() : QRectF());
}

void View::toggleSpotlight()
{
    if (!boardScene_)
        return;
    const QVector<SceneItem *> selected = boardScene_->selectedItemViews();
    if (selected.isEmpty())
        return;

    // The action toggles: a selection that is already spotlighted
    // clears it, anything else replaces the spotlight.
    bool allSpotlighted = true;
    for (SceneItem *item : selected) {
        if (!spotlighted_.contains(item)) {
            allSpotlighted = false;
            break;
        }
    }
    if (allSpotlighted)
        clearSpotlight();
    else
        setSpotlight(selected);
}

void View::clearSpotlight()
{
    if (spotlighted_.isEmpty())
        return;
    spotlighted_.clear();
    viewport()->update();
}

SceneItem *View::itemAtPoint(const QPoint &viewportPos) const
{
    // Picking follows the drawn order: a spotlighted item looks on top,
    // so it takes the click first (topmost first; the list is
    // ascending).
    const QPointF scenePos = mapToScene(viewportPos);
    for (auto it = spotlighted_.crbegin(); it != spotlighted_.crend(); ++it) {
        SceneItem *item = *it;
        if (item->contains(item->mapFromScene(scenePos)))
            return item;
    }
    return dynamic_cast<SceneItem *>(itemAt(viewportPos));
}

void View::setSpotlight(const QVector<SceneItem *> &items)
{
    // Store the spotlight in the configured stacking order, so the draw
    // order is stable and the foreground pass never scans the scene.
    QVector<SceneItem *> next;
    next.reserve(items.size());
    for (QGraphicsItem *graphicsItem : boardScene_->items(Qt::AscendingOrder)) {
        auto *item = dynamic_cast<SceneItem *>(graphicsItem);
        if (item && items.contains(item))
            next.append(item);
    }
    if (next == spotlighted_)
        return;
    spotlighted_ = next;
    viewport()->update();
}

void View::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Only the left button: a right double-click would reach the scene
    // and clear the selection, like the right press does.
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }

    // The reference cancels active modes first: a text item enters edit
    // mode, anything else is selected and fitted.
    cancelCrop();
    if (SceneItem *item = itemAtPoint(event->position().toPoint())) {
        if (item->isText()) {
            if (!item->isSelected())
                item->setSelected(true);
            startTextEdit(item);
            event->accept();
            return;
        }
        if (!item->isSelected())
            item->setSelected(true);
        fitRect(item->sceneBoundingRect());
        emit itemDoubleClicked(item);
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void View::contextMenuEvent(QContextMenuEvent *event)
{
    // The window opens the main menu as one popup (the Go port's canvas
    // menu); the scene itself has no context menu.
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

void View::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    beginInteraction();
    QTimer::singleShot(150, this, [this]() { restoreSmoothing(); });

    // The configured wheel bindings decide what the wheel does; the
    // defaults are zoom (bare), pan_horizontal (Shift, which scrolls the
    // vertical bar, as the reference names it) and pan_vertical
    // (Shift+Ctrl). Inverted bindings flip the delta.
    const controls::Bindings::Match binding = bindings_.wheelAction(event->modifiers());
    int step = delta;
    if (binding.inverted)
        step *= -1;
    if (binding.valid && binding.group == QLatin1String("pan_horizontal")) {
        panBy(QPoint(0, qRound(0.5 * step)));
        updateViewState();
        lod_->schedule();
    } else if (binding.valid && binding.group == QLatin1String("pan_vertical")) {
        panBy(QPoint(qRound(0.5 * step), 0));
        updateViewState();
        lod_->schedule();
    } else if (binding.valid && binding.group == QLatin1String("zoom")) {
        zoomAt(step, event->position().toPoint());
    } else {
        QGraphicsView::wheelEvent(event);
        return;
    }
    event->accept();
}

void View::mousePressEvent(QMouseEvent *event)
{
    beginInteraction();
    lod_->evaluateNow();

    // While the window is following the pointer, any press ends the
    // mode, like the reference's movewin handling.
    if (movingWindow_) {
        exitMoveWindow();
        event->accept();
        return;
    }

    // The reference checks the configured mouse bindings before the
    // item interactions: pan, drag-zoom and move-window.
    const controls::Bindings::Match binding =
        bindings_.mouseAction(event->button(), event->modifiers());
    if (binding.valid && binding.group == QLatin1String("movewindow")) {
        enterMoveWindow();
        event->accept();
        return;
    }
    if (binding.valid && binding.group == QLatin1String("pan")) {
        panning_ = true;
        panStart_ = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (binding.valid && binding.group == QLatin1String("zoom")) {
        dragZoom_ = true;
        dragZoomInverted_ = binding.inverted;
        dragZoomStart_ = event->position().toPoint();
        dragZoomAnchor_ = dragZoomStart_;
        event->accept();
        return;
    }

    if (sampling_) {
        if (event->button() == Qt::LeftButton) {
            const QPoint viewportPos = event->position().toPoint();
            if (SceneItem *item = itemAtPoint(viewportPos)) {
                const QColor color = item->sampleColorAt(mapToScene(viewportPos));
                if (color.isValid())
                    emit colorSampled(color);
            }
        }
        // Any button leaves the mode, like the reference.
        cancelSampleColor();
        event->accept();
        return;
    }

    if (cropItem_ && event->button() == Qt::LeftButton) {
        const QPoint viewportPos = event->position().toPoint();
        const QPointF itemPos = cropItem_->mapFromScene(mapToScene(viewportPos));
        const crop::Part part = crop::hitTest(cropItem_->cropRect(), cropScale(), itemPos);
        if (part != crop::Part::None) {
            cropDrag_ = part;
            cropPressItem_ = itemPos;
            cropDragStartRect_ = cropItem_->cropRect();
        } else if (cropItem_->cropRect().contains(itemPos)) {
            // Clicking inside confirms, outside cancels, as the
            // reference's crop editor does.
            confirmCrop();
        } else {
            cancelCrop();
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        const QPoint viewportPos = event->position().toPoint();
        const QPointF scenePos = mapToScene(viewportPos);
        gestureInverse_ = viewportTransform().inverted();
        // The drag threshold measures from here, whatever gesture starts.
        pressPos_ = viewportPos;

        // Selection handles first: the rotation and flip areas lie
        // partly outside the items, so they are hit-tested against the
        // selection bounds rather than through itemAt().
        if (boardScene_) {
            const QRectF bounds = boardScene_->selectionBounds();
            if (!bounds.isEmpty()) {
                const selection::Hit hit =
                    selection::hitTest(bounds, transform().m11(), scenePos);
                switch (hit.part) {
                case selection::Part::Scale:
                    if (beginScaleGesture(hit.corner, scenePos)) {
                        event->accept();
                        return;
                    }
                    break;
                case selection::Part::Rotate:
                    if (beginRotateGesture(scenePos)) {
                        event->accept();
                        return;
                    }
                    break;
                case selection::Part::FlipHorizontal:
                case selection::Part::FlipVertical:
                    if (undoStack_) {
                        selection::flip(*boardScene_, *undoStack_,
                                        hit.part == selection::Part::FlipVertical);
                        if (const auto &document = boardScene_->document())
                            document->setModified(true);
                        recalculateSceneRect();
                        lod_->evaluateNow();
                        emit documentModified();
                        event->accept();
                        return;
                    }
                    break;
                case selection::Part::None:
                    break;
                }
            }
        }

        if (SceneItem *item = itemAtPoint(viewportPos)) {
            if (event->modifiers().testFlag(Qt::ControlModifier)) {
                item->setSelected(!item->isSelected());
            } else if (!item->isSelected()) {
                scene()->clearSelection();
                item->setSelected(true);
            }

            // Plain move.
            moving_ = true;
            moveStarted_ = false;
            pressScenePos_ = scenePos;
            moveStarts_.clear();
            for (QGraphicsItem *selected : scene()->selectedItems()) {
                if (auto *view = dynamic_cast<SceneItem *>(selected)) {
                    MoveEntry entry;
                    entry.view = view;
                    entry.startPosition = QPointF(view->item()->x, view->item()->y);
                    entry.startState = doc::ChangeItemCommand::State::capture(*view->item());
                    moveStarts_.append(entry);
                }
            }
            event->accept();
            return;
        }
    }
    // Only the left button reaches the scene: a right press makes it
    // clear the selection, and the context menu's actions act on that
    // selection (the Go port keeps it too).
    if (event->button() == Qt::LeftButton)
        QGraphicsView::mousePressEvent(event);
    else
        event->accept();
}

void View::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint position = event->position().toPoint();

    if (movingWindow_) {
        // The timer moves the window; keep the cursor and skip the
        // other interactions.
        event->accept();
        return;
    }

    if (panning_) {
        // Content follows the cursor: the reference pans by
        // (start - current), which is the negated scrollbar delta.
        panBy(panStart_ - position);
        panStart_ = position;
        updateViewState();
        lod_->schedule();
        event->accept();
        return;
    }

    if (dragZoom_) {
        // The reference zooms by the vertical drag, twenty times the
        // wheel step per pixel, anchored where the drag started.
        int delta = dragZoomStart_.y() - position.y();
        if (dragZoomInverted_)
            delta *= -1;
        dragZoomStart_ = position;
        zoomAt(delta * 20, dragZoomAnchor_);
        event->accept();
        return;
    }

    if (sampling_) {
        updateSampleSwatch(position);
        event->accept();
        return;
    }

    if (cropItem_) {
        if (cropDrag_ != crop::Part::None && (event->buttons() & Qt::LeftButton)) {
            const QPointF itemPos =
                cropItem_->mapFromScene(mapToScene(position));
            const QPointF delta = itemPos - cropPressItem_;
            cropItem_->setCropRect(crop::draggedRect(cropDragStartRect_, cropDrag_, delta,
                                                     cropItem_->imageBounds()));
        } else {
            updateCropHoverCursor(position);
        }
        event->accept();
        return;
    }

    if (drag_ == Drag::Scale || drag_ == Drag::Rotate) {
        // Wait for the drag threshold, like the Go port: a shaky click on
        // a handle must not scale or rotate the selection.
        if (!transformStarted_) {
            const QPoint delta = position - pressPos_;
            if (std::hypot(delta.x(), delta.y()) < kDragThreshold) {
                event->accept();
                return;
            }
            transformStarted_ = true;
        }
        const bool snap = event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::ShiftModifier);
        applyTransformGesture(gestureInverse_.map(QPointF(position)), snap);
        event->accept();
        return;
    }

    if (moving_) {
        if (!moveStarted_) {
            const QPoint delta = position - pressPos_;
            if (std::hypot(delta.x(), delta.y()) < kDragThreshold) {
                event->accept();
                return;
            }
        }
        if (!moveStarted_) {
            // The gesture freezes the levels of the items being moved.
            QSet<const doc::Item *> gesture;
            for (const MoveEntry &entry : moveStarts_)
                gesture.insert(entry.view->item().get());
            lod_->setGestureItems(gesture);
        }
        moveStarted_ = true;
        const QPointF delta = gestureInverse_.map(QPointF(position)) - pressScenePos_;
        for (const MoveEntry &entry : moveStarts_) {
            entry.view->setPos(entry.startPosition + delta);
            entry.view->update();
        }
        // The overlay moves with the item, so repaint where it was.
        // The scrollable rect is deliberately only recomputed when the
        // gesture ends: changing the scene rect makes Qt repaint the
        // whole viewport, and the reference's per-change refresh would
        // do that on every event.
        refreshSelectionOverlay();
        event->accept();
        return;
    }

    if (!(event->buttons() & Qt::LeftButton))
        updateHoverCursor(position);
    QGraphicsView::mouseMoveEvent(event);
}

void View::mouseReleaseEvent(QMouseEvent *event)
{
    restoreSmoothing();

    if (movingWindow_) {
        exitMoveWindow();
        event->accept();
        return;
    }

    // A release ends pan and drag-zoom, whatever button it is, like the
    // reference's PAN_MODE/ZOOM_MODE handling.
    if (panning_) {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }

    if (dragZoom_) {
        dragZoom_ = false;
        event->accept();
        return;
    }

    if (cropItem_) {
        if (event->button() == Qt::LeftButton)
            cropDrag_ = crop::Part::None;
        event->accept();
        return;
    }

    if ((drag_ == Drag::Scale || drag_ == Drag::Rotate) && event->button() == Qt::LeftButton) {
        finishTransformGesture();
        drag_ = Drag::None;
        transformStarted_ = false;
        event->accept();
        return;
    }

    if (moving_ && event->button() == Qt::LeftButton) {
        if (moveStarted_) {
            for (const MoveEntry &entry : moveStarts_)
                entry.view->syncPositionToModel();
            if (boardScene_ && boardScene_->document())
                boardScene_->document()->setModified(true);
            recalculateSceneRect();
            // One undo step for the whole gesture.
            if (undoStack_ && !moveStarts_.isEmpty()) {
                undoStack_->beginMacro(QStringLiteral("Move"));
                for (const MoveEntry &entry : moveStarts_) {
                    const doc::ChangeItemCommand::State after =
                        doc::ChangeItemCommand::State::capture(*entry.view->item());
                    undoStack_->push(std::make_unique<doc::ChangeItemCommand>(
                        entry.view->item(), entry.startState, after, QStringLiteral("Move")));
                }
                undoStack_->endMacro();
            }
            lod_->setGestureItems({});
            lod_->schedule();
            emit documentModified();
        }
        moving_ = false;
        moveStarted_ = false;
        moveStarts_.clear();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

QVector<SceneItem *> View::transformableSelection() const
{
    QVector<SceneItem *> items;
    if (!boardScene_)
        return items;
    for (SceneItem *view : boardScene_->selectedItemViews()) {
        if (!view->isError())
            items.append(view);
    }
    return items;
}

void View::setGestureFrozen(bool frozen)
{
    if (!frozen) {
        lod_->setGestureItems({});
        return;
    }
    QSet<const doc::Item *> gesture;
    for (SceneItem *view : transformableSelection())
        gesture.insert(view->item().get());
    lod_->setGestureItems(gesture);
}

bool View::beginScaleGesture(int corner, const QPointF &scenePos)
{
    const QVector<SceneItem *> items = transformableSelection();
    if (items.isEmpty())
        return false;

    gestureBounds_ = boardScene_->selectionBounds();
    gestureAnchor_ = selection::scaleAnchor(gestureBounds_, corner);
    gesturePress_ = scenePos;
    gestureCorner_ = corner;
    gestureEntries_.clear();
    for (SceneItem *view : items) {
        GestureEntry entry;
        entry.view = view;
        entry.before = doc::ChangeItemCommand::State::capture(*view->item());
        entry.startScale = view->item()->scale;
        entry.startRotation = view->item()->rotation;
        gestureEntries_.append(entry);
    }
    setGestureFrozen(true);
    drag_ = Drag::Scale;
    transformStarted_ = false;
    return true;
}

bool View::beginRotateGesture(const QPointF &scenePos)
{
    const QVector<SceneItem *> items = transformableSelection();
    if (items.isEmpty())
        return false;

    gestureBounds_ = boardScene_->selectionBounds();
    gestureAnchor_ = gestureBounds_.center();
    gesturePress_ = scenePos;
    gestureStartAngle_ = selection::rotationAngle(gestureAnchor_, scenePos);
    // The reference snaps against the selection owner's rotation, which
    // is the item itself for a single selection and zero otherwise.
    gestureSnapBase_ = items.size() == 1 ? items.first()->item()->rotation : 0.0;
    gestureEntries_.clear();
    for (SceneItem *view : items) {
        GestureEntry entry;
        entry.view = view;
        entry.before = doc::ChangeItemCommand::State::capture(*view->item());
        entry.startScale = view->item()->scale;
        entry.startRotation = view->item()->rotation;
        gestureEntries_.append(entry);
    }
    setGestureFrozen(true);
    drag_ = Drag::Rotate;
    transformStarted_ = false;
    return true;
}

void View::applyTransformGesture(const QPointF &scenePos, bool snap)
{
    if (drag_ == Drag::Scale) {
        const double factor = selection::scaleFactor(gestureBounds_, gesturePress_, scenePos);
        for (const GestureEntry &entry : gestureEntries_) {
            SceneItem *view = entry.view;
            const double scale = entry.startScale * factor;
            selection::transformAroundAnchor(
                view, gestureAnchor_, [view, scale]() { view->item()->scale = scale; });
        }
        return;
    }

    if (drag_ == Drag::Rotate) {
        double delta = selection::rotationAngle(gestureAnchor_, scenePos) - gestureStartAngle_;
        if (snap) {
            const double target = selection::snapAngle(gestureSnapBase_ + delta, 15.0);
            delta = target - gestureSnapBase_;
        }
        for (const GestureEntry &entry : gestureEntries_) {
            SceneItem *view = entry.view;
            // A flipped item's visual rotation runs the other way.
            const double rotation = entry.startRotation + delta * view->item()->flip;
            selection::transformAroundAnchor(
                view, gestureAnchor_, [view, rotation]() { view->item()->rotation = rotation; });
        }
    }
    // As with moves, the scrollable rect follows on release.
    refreshSelectionOverlay();
}

void View::finishTransformGesture()
{
    setGestureFrozen(false);
    if (!undoStack_ || gestureEntries_.isEmpty()) {
        gestureEntries_.clear();
        return;
    }

    const QString text = drag_ == Drag::Scale ? QStringLiteral("Scale items")
                                              : QStringLiteral("Rotate items");
    QVector<QPair<SceneItem *, doc::ChangeItemCommand::State>> changed;
    for (const GestureEntry &entry : gestureEntries_) {
        const doc::ChangeItemCommand::State after =
            doc::ChangeItemCommand::State::capture(*entry.view->item());
        if (after != entry.before)
            changed.append({entry.view, entry.before});
    }
    gestureEntries_.clear();
    if (changed.isEmpty())
        return;

    undoStack_->beginMacro(text);
    for (const auto &pair : changed) {
        const doc::ChangeItemCommand::State after =
            doc::ChangeItemCommand::State::capture(*pair.first->item());
        undoStack_->push(std::make_unique<doc::ChangeItemCommand>(pair.first->item(), pair.second,
                                                                  after, text));
    }
    undoStack_->endMacro();

    if (boardScene_ && boardScene_->document())
        boardScene_->document()->setModified(true);
    recalculateSceneRect();
    lod_->schedule();
    emit documentModified();
}

void View::updateHoverCursor(const QPoint &viewportPos)
{
    if (!boardScene_) {
        viewport()->unsetCursor();
        return;
    }
    const QRectF bounds = boardScene_->selectionBounds();
    if (bounds.isEmpty()) {
        viewport()->unsetCursor();
        return;
    }
    const selection::Hit hit = selection::hitTest(bounds, transform().m11(),
                                                  mapToScene(viewportPos));
    switch (hit.part) {
    case selection::Part::Scale: {
        double rotation = 0;
        bool flipped = false;
        const QVector<SceneItem *> items = transformableSelection();
        if (!items.isEmpty()) {
            rotation = items.first()->item()->rotation;
            flipped = items.first()->item()->flip < 0;
        }
        viewport()->setCursor(selection::scaleCursor(hit.corner, rotation, flipped));
        return;
    }
    case selection::Part::Rotate:
        viewport()->setCursor(cursors::rotate());
        return;
    case selection::Part::FlipHorizontal:
        viewport()->setCursor(cursors::flipHorizontal());
        return;
    case selection::Part::FlipVertical:
        viewport()->setCursor(cursors::flipVertical());
        return;
    case selection::Part::None:
        break;
    }
    viewport()->unsetCursor();
}

double View::cropScale() const
{
    if (cropItem_)
        return transform().m11() * cropItem_->item()->scale;
    return transform().m11();
}

void View::cropSelection()
{
    if (cropItem_)
        return;
    cancelSampleColor();
    SceneItem *target = nullptr;
    if (boardScene_) {
        const QVector<SceneItem *> selected = boardScene_->selectedItemViews();
        if (selected.size() == 1 && selected.first()->isPixmap() && !selected.first()->isError())
            target = selected.first();
    }
    if (!target)
        return;

    cropItem_ = target;
    cropDrag_ = crop::Part::None;
    cropItem_->enterCropMode();
    setFocus();
    recalculateSceneRect();
    refreshSelectionOverlay();
    updateViewState();
    lod_->evaluateNow();
}

void View::confirmCrop()
{
    if (!cropItem_)
        return;
    SceneItem *view = cropItem_;
    cropItem_ = nullptr;
    cropDrag_ = crop::Part::None;

    const QRectF current =
        view->item()->hasCrop() ? view->item()->crop() : view->imageBounds();
    const QRectF rect = view->cropRect();
    const bool changed = rect != current;
    if (!changed) {
        view->exitCropMode();
        finishCropSession(false);
        return;
    }

    const doc::ChangeItemCommand::State before =
        doc::ChangeItemCommand::State::capture(*view->item());
    view->commitCrop(rect);
    if (undoStack_) {
        undoStack_->push(std::make_unique<doc::ChangeItemCommand>(
            view->item(), before, doc::ChangeItemCommand::State::capture(*view->item()),
            QStringLiteral("Crop item")));
    }
    finishCropSession(true);
}

void View::cancelCrop()
{
    if (!cropItem_)
        return;
    cropItem_->exitCropMode();
    cropItem_ = nullptr;
    cropDrag_ = crop::Part::None;
    finishCropSession(false);
}

void View::cancelModes()
{
    cancelCrop();
    cancelSampleColor();
    commitTextEdit();
    exitMoveWindow();
}

void View::startTextEdit(SceneItem *item)
{
    if (!item || !item->isText())
        return;
    if (textItem_ == item) {
        if (textEditor_)
            textEditor_->setFocus();
        return;
    }
    // Commit whatever another item's editor still holds.
    if (textItem_)
        commitTextEdit();

    textItem_ = item;
    textBefore_ = item->item()->text();

    if (!textEditor_) {
        textEditor_ = new QTextEdit(viewport());
        textEditor_->setObjectName(QStringLiteral("textEditor"));
        textEditor_->setAcceptRichText(false);
        textEditor_->setFrameShape(QFrame::NoFrame);
        textEditor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        textEditor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        textEditor_->setStyleSheet(
            QStringLiteral("QTextEdit { background: %1; color: %2;"
                           " border: 1px dashed %3; padding: 2px; }")
                .arg(theme::canvas.name(), theme::text.name(), theme::selection.name()));
        textEditor_->installEventFilter(this);
        connect(textEditor_, &QTextEdit::textChanged, this, [this]() { positionTextEditor(); });
    }

    textEditor_->setPlainText(textBefore_);
    textEditor_->selectAll();
    item->setTextEditing(true);
    textEditor_->show();
    textEditor_->setFocus();
    positionTextEditor();
}

void View::commitTextEdit()
{
    finishTextEdit(true);
}

void View::cancelTextEdit()
{
    finishTextEdit(false);
}

void View::finishTextEdit(bool commit)
{
    if (!textItem_ || !textEditor_)
        return;

    SceneItem *item = textItem_;
    const QString text = textEditor_->toPlainText();

    // Cleared before hiding: hiding delivers a focus-out that would
    // otherwise re-enter this function.
    textItem_ = nullptr;
    textEditor_->hide();
    item->setTextEditing(false);

    if (!commit || text == textBefore_)
        return;

    const doc::ChangeItemCommand::State before =
        doc::ChangeItemCommand::State::capture(*item->item());
    item->setText(text);
    if (undoStack_) {
        undoStack_->push(std::make_unique<doc::ChangeItemCommand>(
            item->item(), before, doc::ChangeItemCommand::State::capture(*item->item()),
            QStringLiteral("Edit text")));
    }
    if (boardScene_ && boardScene_->document())
        boardScene_->document()->setModified(true);
}

void View::positionTextEditor()
{
    if (!textItem_ || !textEditor_ || !textEditor_->isVisible())
        return;

    // The text is drawn in item-local pixels, scaled by the item's
    // transform and the view; the editor's font must match that.
    const double totalScale = qMax(0.0001, transform().m11() * textItem_->item()->scale);
    QFont font = textItem_->font();
    if (font.pointSizeF() > 0)
        font.setPointSizeF(qMax(1.0, font.pointSizeF() * totalScale));
    else if (font.pixelSize() > 0)
        font.setPixelSize(qMax(1, qRound(font.pixelSize() * totalScale)));
    textEditor_->setFont(font);

    const QRect viewRect =
        mapFromScene(textItem_->mapToScene(textItem_->boundingRect())).boundingRect();

    // The same wrap width the item uses, so the committed layout matches
    // what was typed; never wider than the viewport allows.
    const int room = qMax(60, viewport()->width() - viewRect.x() - 4);
    const int width = qBound(60, qRound(SceneItem::kTextWrapWidth * totalScale), room);
    textEditor_->setFixedWidth(width);
    const int content =
        static_cast<int>(std::ceil(textEditor_->document()->size().height())) + 4;
    textEditor_->setFixedHeight(qMax(viewRect.height(), content));
    textEditor_->move(viewRect.topLeft());
}

void View::finishCropSession(bool changed)
{
    recalculateSceneRect();
    refreshSelectionOverlay();
    updateViewState();
    lod_->evaluateNow();
    if (!changed)
        return;
    if (boardScene_ && boardScene_->document())
        boardScene_->document()->setModified(true);
    emit documentModified();
}

void View::toggleMoveWindow()
{
    if (movingWindow_)
        exitMoveWindow();
    else
        enterMoveWindow();
}

QWidget *View::moveHandle() const
{
    return moveHandle_;
}

void View::setMoveHandleVisible(bool visible)
{
    if (!moveHandle_)
        return;
    if (visible) {
        positionMoveHandle();
        // The welcome overlay is a later sibling of the viewport; keep
        // the handle above it so it is usable on the start screen too.
        moveHandle_->raise();
    }
    moveHandle_->setVisible(visible);
}

void View::positionMoveHandle()
{
    if (!moveHandle_)
        return;
    moveHandle_->move(kMoveHandleMargin, kMoveHandleMargin);
}

void View::enterMoveWindow()
{
    if (movingWindow_)
        return;
    movingWindow_ = true;
    viewport()->setCursor(Qt::SizeAllCursor);
    moveWindowGlobal_ = QCursor::pos();
    // A drag ends when its button comes up; an armed mode (the action
    // from the keyboard or the menu) has no button and ends on the next
    // press or Esc instead.
    moveWindowPressed_ = QGuiApplication::mouseButtons() != Qt::NoButton;
    moveWindowWasActive_ = window() && window()->isActiveWindow();
    moveWindowTimer_->start();
}

void View::exitMoveWindow()
{
    if (!movingWindow_)
        return;
    movingWindow_ = false;
    moveWindowPressed_ = false;
    moveWindowWasActive_ = false;
    moveWindowTimer_->stop();
    viewport()->unsetCursor();
}

void View::moveWindowTick()
{
    if (!movingWindow_)
        return;
    // A drag that was released outside the window never reaches
    // mouseReleaseEvent; the global button state does.
    if (moveWindowPressed_ && QGuiApplication::mouseButtons() == Qt::NoButton) {
        exitMoveWindow();
        return;
    }
    // The armed mode should not keep following the cursor once another
    // application comes to the front (only when the window was active
    // when it was armed).
    if (!moveWindowPressed_ && moveWindowWasActive_ && window()
        && !window()->isActiveWindow()) {
        exitMoveWindow();
        return;
    }
    const QPointF global = QCursor::pos();
    const QPointF delta = global - moveWindowGlobal_;
    if (delta.isNull())
        return;
    moveWindowGlobal_ = global;
    if (QWidget *top = window())
        top->move(top->pos() + delta.toPoint());
}

void View::startSampleColor()
{
    // Only one tool runs at a time, like the reference's
    // cancel_active_modes().
    cancelCrop();
    sampling_ = true;
    viewport()->setCursor(Qt::CrossCursor);
    if (!swatch_)
        swatch_ = new ColorSwatch(viewport());

    // Show the colour under the pointer right away.
    const QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
    if (viewport()->rect().contains(pos))
        updateSampleSwatch(pos);
    setFocus();
}

void View::cancelSampleColor()
{
    if (!sampling_)
        return;
    sampling_ = false;
    if (viewport())
        viewport()->unsetCursor();
    if (swatch_)
        swatch_->hide();
}

QColor View::sampledColor() const
{
    return swatch_ ? swatch_->color() : QColor();
}

void View::updateSampleSwatch(const QPoint &viewportPos)
{
    if (!swatch_)
        return;
    QColor color;
    if (SceneItem *item = itemAtPoint(viewportPos))
        color = item->sampleColorAt(mapToScene(viewportPos));
    // Without a colour the swatch stays visible but transparent, like
    // the reference's NONE_COLOR.
    swatch_->setColor(color);
    swatch_->moveNear(viewportPos);
    swatch_->show();
}

void View::updateCropHoverCursor(const QPoint &viewportPos)
{
    if (!cropItem_) {
        viewport()->unsetCursor();
        return;
    }
    const QPointF itemPos = cropItem_->mapFromScene(mapToScene(viewportPos));
    const crop::Part part = crop::hitTest(cropItem_->cropRect(), cropScale(), itemPos);
    switch (part) {
    case crop::Part::None:
        viewport()->unsetCursor();
        return;
    case crop::Part::Top:
    case crop::Part::Left:
    case crop::Part::Bottom:
    case crop::Part::Right:
        viewport()->setCursor(crop::edgeCursor(part, cropItem_->item()->rotation));
        return;
    default:
        viewport()->setCursor(crop::handleCursor(part, cropItem_->item()->rotation,
                                                 cropItem_->item()->flip < 0));
        return;
    }
}

void View::keyPressEvent(QKeyEvent *event)
{
    if (movingWindow_) {
        exitMoveWindow();
        event->accept();
        return;
    }
    if (sampling_) {
        cancelSampleColor();
        event->accept();
        return;
    }
    if (cropItem_) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            confirmCrop();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            cancelCrop();
            event->accept();
            return;
        }
    }
    // Esc dismisses a spotlight when no mode above claimed it.
    if (event->key() == Qt::Key_Escape && !spotlighted_.isEmpty()) {
        clearSpotlight();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

bool View::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == textEditor_ && textEditor_) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) {
                cancelTextEdit();
                return true;
            }
            // Enter commits, Shift+Enter falls through to the editor's
            // newline, like the reference's text item.
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                && !key->modifiers().testFlag(Qt::ShiftModifier)) {
                commitTextEdit();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Clicking elsewhere in the canvas commits.
            commitTextEdit();
        }
    }
    return QGraphicsView::eventFilter(watched, event);
}

void View::leaveEvent(QEvent *event)
{
    if (drag_ == Drag::None && !panning_ && !sampling_ && !movingWindow_)
        viewport()->unsetCursor();
    QGraphicsView::leaveEvent(event);
}

void View::drawSpotlight(QPainter *painter) const
{
    // The list is kept in ascending stacking order, so the lowest
    // spotlighted item is drawn first and the topmost of them stays on
    // top, exactly as if the whole group were really raised.
    for (SceneItem *item : spotlighted_) {
        painter->save();
        painter->setTransform(item->sceneTransform(), true);
        painter->setOpacity(item->opacity());
        item->paintContent(painter);
        // A dashed marker at full opacity, so a view-only spotlight
        // cannot be mistaken for a real Raise to Top.
        painter->setOpacity(1.0);
        QPen pen(theme::selection, 0);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(item->boundingRect());
        painter->restore();
    }
}

void View::drawForeground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect);
    if (!boardScene_)
        return;
    // The crop editor brings its own frame; spotlights and handles would
    // only get in the way. A spotlight is suspended, not cleared, while
    // cropping, so it comes back when the crop session ends.
    if (cropItem_)
        return;
    if (!spotlighted_.isEmpty())
        drawSpotlight(painter);

    const QRectF bounds = boardScene_->selectionBounds();
    if (bounds.isEmpty())
        return;

    painter->save();
    QPen pen(theme::selection, selection::kLineWidth);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bounds);

    // Corner handles, drawn as round dots; cosmetic pen widths keep
    // them at a constant screen size at any zoom.
    pen.setWidthF(selection::kHandleSize);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    for (int index = 0; index < 4; ++index)
        painter->drawPoint(selection::corner(bounds, index));
    painter->restore();
}


void View::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    positionMoveHandle();
    positionTextEditor();
    updateViewState();
    recalculateSceneRect();
    lod_->schedule();
}

void View::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    positionTextEditor();
    updateViewState();
    lod_->schedule();
}

void View::dragEnterEvent(QDragEnterEvent *event)
{
    if (mimeFilter_ && mimeFilter_(*event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void View::dragMoveEvent(QDragMoveEvent *event)
{
    if (mimeFilter_ && mimeFilter_(*event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void View::dropEvent(QDropEvent *event)
{
    if (!mimeFilter_ || !mimeFilter_(*event->mimeData())) {
        event->ignore();
        return;
    }
    emit mimeDropped(event->mimeData(), mapToScene(event->position().toPoint()));
    event->acceptProposedAction();
}

} // namespace ui
