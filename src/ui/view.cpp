#include "view.h"

#include "lod_manager.h"
#include "rendering.h"
#include "theme.h"

#include "doc/undo.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>
#include <memory>

namespace ui {

View::View(QWidget *parent)
    : QGraphicsView(parent)
{
    lod_ = new LodManager(this);

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
    setAcceptDrops(true);
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
    boardScene_ = scene;
    if (boardScene_) {
        // Drop scheduler state for a view before the scene deletes it,
        // so an in-flight decode can never touch freed memory.
        connect(boardScene_, &Scene::itemViewAboutToBeRemoved, this,
                [this](SceneItem *view) { lod_->forgetItem(view); });
        // New or removed items change the scrollable area.
        connect(boardScene_, &Scene::itemsChanged, this, [this]() { recalculateSceneRect(); });
    }
    lod_->setScene(scene);
    QGraphicsView::setScene(scene);
    updateViewState();
    recalculateSceneRect();
    lod_->schedule();
}

void View::recalculateSceneRect()
{
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
        sceneRectValid_ = false;
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

    // Changing the scene rect re-maps the scrollbars, which would jump
    // the view; keep looking at the same scene point.
    const QPointF centre = mapToScene(viewport()->rect().center());
    const bool keepCentre = sceneRectValid_;
    boardScene_->setSceneRect(rect);
    sceneRectValid_ = true;
    if (keepCentre)
        centerOn(centre);
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
    lod_->evaluateNow();
}

void View::fitScene()
{
    if (!scene() || scene()->items().isEmpty())
        return;
    const QRectF rect = scene()->itemsBoundingRect();
    fitInView(rect, Qt::KeepAspectRatio);
    recalculateSceneRect();
    fitInView(rect, Qt::KeepAspectRatio);
    updateViewState();
    lod_->evaluateNow();
}

void View::fitSelection()
{
    if (!boardScene_)
        return;
    const QRectF rect = boardScene_->selectionBounds();
    if (rect.isEmpty())
        return;
    fitInView(rect, Qt::KeepAspectRatio);
    recalculateSceneRect();
    fitInView(rect, Qt::KeepAspectRatio);
    updateViewState();
    lod_->evaluateNow();
}

void View::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    beginInteraction();
    QTimer::singleShot(150, this, [this]() { restoreSmoothing(); });
    // The reference maps Shift (pan_horizontal) to the vertical
    // scrollbar and Shift+Ctrl (pan_vertical) to the horizontal one,
    // and pans by half the wheel delta. Kept for parity.
    if (modifiers.testFlag(Qt::ShiftModifier)
        && modifiers.testFlag(Qt::ControlModifier)) {
        panBy(QPoint(qRound(0.5 * delta), 0));
        updateViewState();
        lod_->schedule();
    } else if (modifiers.testFlag(Qt::ShiftModifier)) {
        panBy(QPoint(0, qRound(0.5 * delta)));
        updateViewState();
        lod_->schedule();
    } else {
        zoomAt(delta, event->position().toPoint());
    }
    event->accept();
}

void View::mousePressEvent(QMouseEvent *event)
{
    beginInteraction();
    lod_->evaluateNow();

    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        panStart_ = event->position().toPoint();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (auto *item = dynamic_cast<SceneItem *>(itemAt(event->position().toPoint()))) {
            moving_ = true;
            moveStarted_ = false;
            pressPos_ = event->position().toPoint();
            pressScenePos_ = mapToScene(pressPos_);

            if (event->modifiers().testFlag(Qt::ControlModifier)) {
                item->setSelected(!item->isSelected());
            } else if (!item->isSelected()) {
                scene()->clearSelection();
                item->setSelected(true);
            }

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
    QGraphicsView::mousePressEvent(event);
}

void View::mouseMoveEvent(QMouseEvent *event)
{
    if (panning_) {
        const QPoint position = event->position().toPoint();
        // Content follows the cursor: the reference pans by
        // (start - current), which is the negated scrollbar delta.
        panBy(panStart_ - position);
        panStart_ = position;
        updateViewState();
        lod_->schedule();
        event->accept();
        return;
    }

    if (moving_) {
        const QPoint position = event->position().toPoint();
        if (!moveStarted_ && (position - pressPos_).manhattanLength() < kMoveThreshold) {
            event->accept();
            return;
        }
        if (!moveStarted_) {
            // The gesture freezes the levels of the items being moved.
            QSet<const doc::Item *> gesture;
            for (const MoveEntry &entry : moveStarts_)
                gesture.insert(entry.view->item().get());
            lod_->setGestureItems(gesture);
        }
        moveStarted_ = true;
        const QPointF delta = mapToScene(position) - pressScenePos_;
        for (const MoveEntry &entry : moveStarts_) {
            entry.view->setPos(entry.startPosition + delta);
            entry.view->update();
        }
        // Dragging an item past the current area grows the scrollable
        // rect, as the reference does on every scene change.
        recalculateSceneRect();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void View::mouseReleaseEvent(QMouseEvent *event)
{
    restoreSmoothing();

    if (panning_ && event->button() == Qt::MiddleButton) {
        panning_ = false;
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

void View::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    updateViewState();
    recalculateSceneRect();
    lod_->schedule();
}

void View::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
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
