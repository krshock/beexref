#pragma once

#include "doc/undo.h"

#include <QPointF>

#include <functional>

namespace ui {

class Scene;
class SceneItem;

namespace selection {

// Applies a model change to one canvas item while keeping the given
// scene point fixed: the reference's with_anchor behaviour. The caller
// mutates the model inside `mutate`; the item's position is corrected
// afterwards and written back to the model.
void transformAroundAnchor(SceneItem *view, const QPointF &anchorScene,
                           const std::function<void()> &mutate);

// Selection actions; each is one undo step.
void flip(const Scene &scene, doc::UndoStack &stack, bool vertical);
void resetScale(const Scene &scene, doc::UndoStack &stack);
void resetRotation(const Scene &scene, doc::UndoStack &stack);
void resetFlip(const Scene &scene, doc::UndoStack &stack);
void resetCrop(const Scene &scene, doc::UndoStack &stack);
// Z-order: the reference's raise_to_top/lower_to_bottom maths (above the
// highest / below the lowest item by one step) as one undo step.
void raiseToTop(const Scene &scene, doc::UndoStack &stack);
void lowerToBottom(const Scene &scene, doc::UndoStack &stack);
void resetTransforms(const Scene &scene, doc::UndoStack &stack);

// The selected canvas items, error items excluded: the reference's
// selectedItems(user_only=True).
QVector<SceneItem *> selectionItems(const Scene &scene);

// The images (pixmap items) of the current selection: the reference's
// ChangeOpacity and ToggleGrayscale only act on those.
QVector<SceneItem *> imageSelection(const Scene &scene);

// Live opacity preview for the dialog: model and view, no undo step.
void applyOpacity(const Scene &scene, double opacity);

// Opacity and grayscale, applied to the selected images; the caller
// pushes nothing itself, these record one step when something changed.
void setOpacity(const Scene &scene, doc::UndoStack &stack, double opacity);
void setGrayscale(const Scene &scene, doc::UndoStack &stack, bool grayscale);

} // namespace selection
} // namespace ui
