#pragma once

#include "crop_tools.h"
#include "tool.h"

#include <QPointF>
#include <QRectF>

class QKeyEvent;
class QMouseEvent;

namespace ui {

class SceneItem;
class View;

// The reference's CropEditor, driven by the view: the single selected
// image shows its whole bitmap with an editable rectangle on top; the
// model is untouched until confirm(). Return confirms and Esc cancels,
// clicking inside the rectangle confirms and outside cancels.
class CropTool : public Tool
{
public:
    explicit CropTool(View *view);

    QString id() const override { return QStringLiteral("crop"); }
    bool active() const override { return item_ != nullptr; }
    void cancel() override;

    // Enters crop mode on the single selected image (the Crop action).
    void start();
    // Applies the rectangle as one undo step.
    void confirm();
    // The scene is about to delete item: drop the session.
    void cancelIfItem(SceneItem *item);

    bool mousePress(QMouseEvent *event) override;
    bool mouseMove(QMouseEvent *event) override;
    bool mouseRelease(QMouseEvent *event) override;
    bool keyPress(QKeyEvent *event) override;

private:
    // The view scale times the item's scale: the reference's
    // fixed_length_for_viewport denominator.
    double scale() const;
    void updateHoverCursor(const QPoint &viewportPos);

    View *view_ = nullptr;
    SceneItem *item_ = nullptr;
    crop::Part drag_ = crop::Part::None;
    QPointF pressItem_;
    QRectF dragStartRect_;
};

} // namespace ui
