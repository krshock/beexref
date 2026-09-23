#pragma once

#include "controls.h"
#include "crop_tools.h"
#include "levels.h"
#include "scene.h"
#include "selection_tools.h"

#include "doc/undo.h"

#include <QColor>
#include <QGraphicsView>
#include <QPoint>
#include <QPointF>
#include <QTransform>
#include <QVector>

#include <functional>

class QKeyEvent;
class QMimeData;

namespace ui {

class ColorSwatch;
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
    // The keyboard/mouse bindings, reloaded after the controls editor
    // changes them.
    void setBindings(const controls::Bindings &bindings) { bindings_ = bindings; }

    // Move-window mode (the reference's movewin): while active the
    // window follows the pointer; any press, release or key ends it.
    // Entered with Ctrl+M or the bound mouse combination.
    void toggleMoveWindow();
    void enterMoveWindow();
    void exitMoveWindow();
    bool movingWindow() const { return movingWindow_; }
    // Refreshes the scrollable area after the items changed shape, as
    // the reference does on every scene change.
    void refreshSceneRect() { recalculateSceneRect(); }

    // Enters crop mode on the single selected image, as the reference's
    // Crop action does; a second crop cannot start while one is active.
    void cropSelection();
    // Leaves crop mode without applying anything; the reference cancels
    // active modes before undo/redo and before a scene is replaced.
    void cancelCrop();
    bool cropActive() const { return cropItem_ != nullptr; }

    // Sample colour mode: a crosshair and a swatch follow the pointer;
    // the next click reports the colour under it and leaves the mode
    // (the reference's Sample Color action).
    void startSampleColor();
    void cancelSampleColor();
    bool samplingColor() const { return sampling_; }
    // The colour currently shown in the swatch; invalid when none.
    QColor sampledColor() const;
    // Commands for completed gestures (moves) are pushed here.
    void setUndoStack(doc::UndoStack *stack) { undoStack_ = stack; }

    void fitScene();
    void fitSelection();

    // delta is a wheel angleDelta step; anchor is in viewport pixels.
    void zoomAt(int delta, const QPoint &anchor);

signals:
    // A colour was sampled and the mode ended; the window copies it.
    void colorSampled(const QColor &color);
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
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
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
    // Fits the view to a scene rectangle: the reference's fit_rect
    // without its toggle-back behaviour, which is not ported.
    void fitRect(const QRectF &rect);

    // Transform gestures.
    bool beginScaleGesture(int corner, const QPointF &scenePos);
    bool beginRotateGesture(const QPointF &scenePos);
    void applyTransformGesture(const QPointF &scenePos, bool snap);
    void finishTransformGesture();
    void updateHoverCursor(const QPoint &viewportPos);

    // Crop session (the reference's CropEditor, driven by the view).
    void confirmCrop();
    void finishCropSession(bool changed);
    void updateCropHoverCursor(const QPoint &viewportPos);
    void updateSampleSwatch(const QPoint &viewportPos);
    // View scale times the item's scale: the reference's
    // fixed_length_for_viewport denominator.
    double cropScale() const;
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
    // Drag-zoom (the reference's ZOOM_MODE): a bound button drag zooms.
    bool dragZoom_ = false;
    bool movingWindow_ = false;
    QPointF moveWindowGlobal_;
    bool dragZoomInverted_ = false;
    QPoint dragZoomStart_;
    QPoint dragZoomAnchor_;
    controls::Bindings bindings_;
    Drag drag_ = Drag::None;
    // The view transform at press: gesture coordinates are mapped
    // through this, so a view shift mid-gesture (the scene rect grows
    // while dragging) cannot feed back into the item positions.
    QTransform gestureInverse_;

    // Last region the selection overlay was known to occupy.
    QRectF overlayRegion_;

    // Sample colour mode.
    bool sampling_ = false;
    ColorSwatch *swatch_ = nullptr;

    // Crop session state.
    SceneItem *cropItem_ = nullptr;
    crop::Part cropDrag_ = crop::Part::None;
    QPointF cropPressItem_;
    QRectF cropDragStartRect_;

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
