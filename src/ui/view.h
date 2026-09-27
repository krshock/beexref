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
#include <memory>

class QKeyEvent;
class QMimeData;
class QTextEdit;
class QTimer;

namespace ui {

class ColorSamplerTool;
class LevelLoader;
class LodManager;
class MoveHandle;
class MoveWindowTool;
class TextEditTool;
class ToolController;

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
    bool movingWindow() const;
    // The canvas corner's move handle (tests and the smoke harness);
    // pressing it starts the platform window move.
    // Defined in the .cpp, where MoveHandle is a complete type.
    QWidget *moveHandle() const;
    // Shows the corner move handle; the window shows it only while its
    // title bar is disabled, since then nothing else can drag it.
    void setMoveHandleVisible(bool visible);
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

    // In-place text editing: started by a double-click on a text item or
    // by Insert > Text. Enter or a click elsewhere commits (one undo
    // step), Esc cancels, Shift+Enter inserts a newline.
    void startTextEdit(SceneItem *item);
    void commitTextEdit();
    void cancelTextEdit();
    bool textEditing() const;

    // The reference's cancel_active_modes(): ends every mode that owns
    // the interaction (crop, colour sampling, text editing, move
    // window). Undo, IO and scene actions call it before touching the
    // items; a new mode must be added here once, not at every call site.
    void cancelModes();

    // The tools report their results through the view's signals, and
    // hit-test through the same order the painting uses.
    void reportColorSampled(const QColor &color) { emit colorSampled(color); }
    SceneItem *itemAtPoint(const QPoint &viewportPos) const;

    // Sample colour mode: a crosshair and a swatch follow the pointer;
    // the next click reports the colour under it and leaves the mode
    // (the reference's Sample Color action).
    void startSampleColor();
    void cancelSampleColor();
    bool samplingColor() const;
    // The colour currently shown in the swatch; invalid when none.
    QColor sampledColor() const;
    // Commands for completed gestures (moves) are pushed here.
    void setUndoStack(doc::UndoStack *stack) { undoStack_ = stack; }
    doc::UndoStack *undoStack() const { return undoStack_; }
    // Tools report changes through the view: the document is marked
    // modified, so closing asks to save.
    void markDocumentModified();

    void fitScene();
    void fitSelection();

    // Spotlight: a view-only raise. The selected items are painted
    // above everything in the foreground pass, without touching their
    // configured z or the document. toggleSpotlight() spotlights the
    // selection, or clears when the selection is already spotlighted;
    // setSpotlight() replaces it with the given items; clearSpotlight()
    // ends it (Esc). Deleted items drop out and a new board clears it.
    void toggleSpotlight();
    void setSpotlight(const QVector<SceneItem *> &items);
    void clearSpotlight();
    bool hasSpotlight() const { return !spotlighted_.isEmpty(); }

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
    // A right click on the canvas, with the global position for the
    // window's context menu.
    void contextMenuRequested(const QPoint &globalPos);
    // A left double-click selected and fitted an item; the window may
    // spotlight it (the Items/double_click_spotlight setting).
    void itemDoubleClicked(SceneItem *item);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    // Puts the move handle in the canvas' top-left corner.
    void positionMoveHandle();

    // Spotlight state: the view-only raised items.
    void drawSpotlight(QPainter *painter) const;

    // Crop session (the reference's CropEditor, driven by the view).
    void confirmCrop();
    void finishCropSession(bool changed);
    void updateCropHoverCursor(const QPoint &viewportPos);
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
    bool transformStarted_ = false;
    // Drag-zoom (the reference's ZOOM_MODE): a bound button drag zooms.
    bool dragZoom_ = false;
    QTimer *moveWindowTimer_ = nullptr;
    MoveHandle *moveHandle_ = nullptr;
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

    // The view-only raised items (spotlight), in ascending stacking
    // order: exactly the order the foreground pass draws them in. Never
    // part of the document.
    QVector<SceneItem *> spotlighted_;

    // The interactive modes, in dispatch order; each tool owns its
    // session state.
    std::unique_ptr<ToolController> tools_;
    ColorSamplerTool *sampler_ = nullptr;
    MoveWindowTool *moveWindowTool_ = nullptr;
    TextEditTool *textEditTool_ = nullptr;

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

    // The Go port's drag threshold: a click with a shaky hand must not
    // disturb the selection, so moving, scaling and rotating only start
    // once the pointer has travelled this many viewport pixels.
    static constexpr double kDragThreshold = 20.0;
    // The corner move handle sits this far in from the canvas corner.
    static constexpr int kMoveHandleMargin = 2;
    // Zoom bursts hold LOD work for this long after the last zoom event,
    // so level decodes happen once the view has settled.
    static constexpr int kZoomInhibitMs = 300;
    static constexpr double kMaxZoomExtent = 10000000.0;
    static constexpr double kMinZoomExtent = 50.0;
};

} // namespace ui
