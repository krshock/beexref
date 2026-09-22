#pragma once

#include "doc/undo.h"

#include <QString>
#include <QVector>

namespace ui {

class Scene;
class SceneItem;

namespace layout {

enum class Normalize {
    Height,
    Width,
    Size, // equal area, like the reference's "Size"
};

enum class Arrange {
    Horizontal,
    Vertical,
    Square,
};

// The reference's sort_by_filename: items with a filename first (in
// filename order), then items with a save id (in id order), then the
// rest in insertion order.
QVector<SceneItem *> orderedSelection(const Scene &scene);

// The Items/arrange_default setting picks an arrangement style;
// "optimal" (and anything unknown) falls back to square, because the
// optimal packing is not ported yet.
Arrange arrangeModeFromSetting(const QString &value);

// Scale the selected items so their scene bounding boxes share the
// average height, width or area, each around its own centre. One undo
// step; fewer than two items is a no-op.
void normalize(const Scene &scene, doc::UndoStack &stack, Normalize mode);

// Lay the selected items out centred on their current common centre:
// a row or column compacted along the current positions (ties keep the
// filename order), or a grid of ceil(sqrt(n)) rows in filename order.
// One undo step; fewer than two items is a no-op.
void arrange(const Scene &scene, doc::UndoStack &stack, Arrange mode, int gap);

} // namespace layout
} // namespace ui
