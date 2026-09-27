#pragma once

#include <QRectF>
#include <QString>

class QPainter;

namespace ui {

class SceneItem;

namespace item_types {

// The canvas behaviour of one document type. The handlers are SceneItem
// members, so they keep access to the item's level, crop and text state;
// a new document type adds one entry here plus its handler functions,
// and nothing else in the UI has to learn about it. The document stays
// generic (type plus JSON data), so the file format is untouched.
struct Traits {
    // Content bounds in local (original-pixel) coordinates.
    QRectF (SceneItem::*bounds)() const = nullptr;
    // Paints the content with no selection decoration.
    void (SceneItem::*paint)(QPainter *painter) = nullptr;
    // Whether the crop editor and colour sampling apply.
    bool crops = false;
    bool samples = false;
    // The SVG element for the scene export ("text" or "image"); empty
    // skips the item (error and unknown types).
    const char *svgElement = nullptr;
};

// The traits of a document type; unknown types get the error traits, so
// they render as an unsupported item and are not exported.
const Traits &forType(const QString &type);

} // namespace item_types

} // namespace ui
