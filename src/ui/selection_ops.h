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
void resetTransforms(const Scene &scene, doc::UndoStack &stack);

} // namespace selection
} // namespace ui
