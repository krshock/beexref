#pragma once

#include "levels.h"
#include "scene.h"
#include "selection_tools.h"

#include "doc/undo.h"

#include <QGraphicsView>
#include <QPoint>
#include <QPointF>
#include <QTransform>
#include <QVector>

#include <functional>

class QMimeData;

namespace ui {

class LevelLoader;
class LodManager;

// Canvas view: pan, zoom, fit, rubber-band selection and
// threshold-based moving of the selected items. Viewport changes drive
// the LOD manager; accepted drags are forwarded as mimeDropped.
class View : public QGraphicsView
{
    Q_OBJECT

public:
    using MimeFilter = std::function<bool(const QMimeData &)>;

    explicit View(QWidget *parent = nullptr);
    ~View() override;

    void setLevelLoader(LevelLoader *loader);
    void setBoardScene(Scene *scene);
    void setMimeFilter(MimeFilter filter) { mimeFilter_ = std::move(filter); }

    void setLodSettings(const LodSettings &settings);
    LodManager *lodManager() const { return lod_; }
    // Refreshes the scrollable area after the items changed shape, as
    // the reference does on every scene change.
    void refreshSceneRect() { recalculateSceneRect(); }
    // Commands for completed gestures (moves) are pushed here.
    void setUndoStack(doc::UndoStack *stack) { undoStack_ = stack; }

    void fitScene();
    void fitSelection();

    // delta is a wheel angleDelta step; anchor is in viewport pixels.
    void zoomAt(int delta, const QPoint &anchor);

signals:
    // A drop the filter accepted, with its position in scene
    // coordinates. The mime data stays owned by the event.
    void mimeDropped(const QMimeData *data, const QPointF &scenePos);
    // A gesture changed the document (a move); the window refreshes.
    void documentModified();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    // What a left-drag on the selection is doing.
    enum class Drag {
        None,
        Move,
        Scale,
        Rotate,
    };

    struct GestureEntry
    {
        SceneItem *view = nullptr;
        doc::ChangeItemCommand::State before;
        double startScale = 1.0;
        double startRotation = 0.0;
    };

    void panBy(const QPoint &delta);
    void updateViewState();
    // Items bbox expanded by one viewport per side: the scrollable
    // area that makes the canvas feel infinite.
    void recalculateSceneRect();
    // The selection outline and handles are drawn in drawForeground(),
    // whose output the scene's dirty regions do not track; moving them
    // needs an explicit repaint of where they were.
    QRectF selectionOverlayRegion() const;
    void refreshSelectionOverlay();
    void beginInteraction();
    void restoreSmoothing();
    double zoomExtent(bool maximum) const;

    // Transform gestures.
    bool beginScaleGesture(int corner, const QPointF &scenePos);
    bool beginRotateGesture(const QPointF &scenePos);
    void applyTransformGesture(const QPointF &scenePos, bool snap);
    void finishTransformGesture();
    void updateHoverCursor(const QPoint &viewportPos);
    void setGestureFrozen(bool frozen);
    QVector<SceneItem *> transformableSelection() const;

    LevelLoader *loader_ = nullptr;
    Scene *boardScene_ = nullptr;
    LodManager *lod_ = nullptr;
    doc::UndoStack *undoStack_ = nullptr;
    MimeFilter mimeFilter_;

    bool panning_ = false;
    QPoint panStart_;
    bool moving_ = false;
    bool moveStarted_ = false;
    Drag drag_ = Drag::None;
    // The view transform at press: gesture coordinates are mapped
    // through this, so a view shift mid-gesture (the scene rect grows
    // while dragging) cannot feed back into the item positions.
    QTransform gestureInverse_;

    // Last region the selection overlay was known to occupy.
    QRectF overlayRegion_;

    // Scale/rotate gesture state.
    QRectF gestureBounds_;
    QPointF gestureAnchor_;
    QPointF gesturePress_;
    double gestureStartAngle_ = 0;
    double gestureSnapBase_ = 0;
    int gestureCorner_ = -1;
    QVector<GestureEntry> gestureEntries_;

    QPoint pressPos_;
    QPointF pressScenePos_;
    struct MoveEntry
    {
        SceneItem *view = nullptr;
        QPointF startPosition;
        doc::ChangeItemCommand::State startState;
    };
    QVector<MoveEntry> moveStarts_;

    static constexpr double kMoveThreshold = 3.0; // viewport pixels
    static constexpr double kMaxZoomExtent = 10000000.0;
    static constexpr double kMinZoomExtent = 50.0;
};

} // namespace ui
