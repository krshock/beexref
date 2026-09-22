#include "view.h"

#include "theme.h"

#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

namespace ui {

View::View(QWidget *parent)
    : QGraphicsView(parent)
{
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
}

void View::setLevelLoader(LevelLoader *loader)
{
    if (loader_)
        disconnect(loader_, nullptr, this, nullptr);
    loader_ = loader;
    if (!loader_)
        return;

    connect(loader_, &LevelLoader::levelReady, this, [this](quint64 requestId, const QImage &image) {
        if (!boardScene_)
            return;
        for (SceneItem *view : boardScene_->pixmapItemViews()) {
            if (view->pendingRequest() != requestId)
                continue;
            view->setPendingRequest(0);
            const QSize original = view->item()->originalSize();
            const double fraction =
                original.width() > 0 ? static_cast<double>(image.width()) / original.width()
                                     : 1.0;
            view->setLevel(image, fraction);
            scheduleLevelRequest();
            return;
        }
    });
    connect(loader_, &LevelLoader::levelFailed, this, [this](quint64 requestId) {
        if (!boardScene_)
            return;
        for (SceneItem *view : boardScene_->pixmapItemViews()) {
            if (view->pendingRequest() != requestId)
                continue;
            view->setPendingRequest(0);
            view->setLevelUnavailable();
            return;
        }
    });
}

void View::setBoardScene(Scene *scene)
{
    boardScene_ = scene;
    QGraphicsView::setScene(scene);
    scheduleLevelRequest();
}

void View::scheduleLevelRequest()
{
    if (levelRequestScheduled_)
        return;
    levelRequestScheduled_ = true;
    QTimer::singleShot(0, this, [this]() { requestVisibleLevels(); });
}

void View::requestVisibleLevels()
{
    levelRequestScheduled_ = false;
    if (!loader_ || !boardScene_)
        return;

    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const double viewScale = transform().m11();
    for (SceneItem *view : boardScene_->pixmapItemViews()) {
        if (view->levelUnavailable() || view->pendingRequest() != 0)
            continue;
        if (!view->sceneBoundingRect().intersects(visible))
            continue;
        const QSize original = view->item()->originalSize();
        if (!original.isValid() || original.isEmpty())
            continue;

        const double needed = qBound(0.0, view->item()->scale * viewScale, 1.0);
        if (needed <= 0)
            continue;
        if (view->levelFraction() >= needed * 0.99)
            continue;

        const double fraction = qMax(needed, 0.05);
        const QSize target(qMax(1, qRound(original.width() * fraction)),
                           qMax(1, qRound(original.height() * fraction)));
        const quint64 requestId = nextRequestId_++;
        view->setPendingRequest(requestId);
        loader_->request(requestId, view->item()->source, target);
    }
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
    scheduleLevelRequest();
}

void View::fitScene()
{
    if (!scene() || scene()->items().isEmpty())
        return;
    const QRectF rect = scene()->itemsBoundingRect();
    fitInView(rect, Qt::KeepAspectRatio);
    fitInView(rect, Qt::KeepAspectRatio);
    scheduleLevelRequest();
}

void View::fitSelection()
{
    if (!boardScene_)
        return;
    const QRectF rect = boardScene_->selectionBounds();
    if (rect.isEmpty())
        return;
    fitInView(rect, Qt::KeepAspectRatio);
    fitInView(rect, Qt::KeepAspectRatio);
    scheduleLevelRequest();
}

void View::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    // The reference maps Shift (pan_horizontal) to the vertical
    // scrollbar and Shift+Ctrl (pan_vertical) to the horizontal one,
    // and pans by half the wheel delta. Kept for parity.
    if (modifiers.testFlag(Qt::ShiftModifier)
        && modifiers.testFlag(Qt::ControlModifier)) {
        panBy(QPoint(qRound(0.5 * delta), 0));
    } else if (modifiers.testFlag(Qt::ShiftModifier)) {
        panBy(QPoint(0, qRound(0.5 * delta)));
    } else {
        zoomAt(delta, event->position().toPoint());
    }
    event->accept();
}

void View::mousePressEvent(QMouseEvent *event)
{
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
                if (auto *view = dynamic_cast<SceneItem *>(selected))
                    moveStarts_.append({view, QPointF(view->item()->x, view->item()->y)});
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
        scheduleLevelRequest();
        event->accept();
        return;
    }

    if (moving_) {
        const QPoint position = event->position().toPoint();
        if (!moveStarted_ && (position - pressPos_).manhattanLength() < kMoveThreshold) {
            event->accept();
            return;
        }
        moveStarted_ = true;
        const QPointF delta = mapToScene(position) - pressScenePos_;
        for (const auto &entry : moveStarts_) {
            entry.first->setPos(entry.second + delta);
            entry.first->update();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void View::mouseReleaseEvent(QMouseEvent *event)
{
    if (panning_ && event->button() == Qt::MiddleButton) {
        panning_ = false;
        event->accept();
        return;
    }

    if (moving_ && event->button() == Qt::LeftButton) {
        if (moveStarted_) {
            for (const auto &entry : moveStarts_)
                entry.first->syncPositionToModel();
            if (boardScene_ && boardScene_->document()) {
                boardScene_->document()->setModified(true);
                boardScene_->updateSceneRect();
            }
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
    scheduleLevelRequest();
}

void View::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    scheduleLevelRequest();
}

} // namespace ui
