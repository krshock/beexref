#include "color_sampler_tool.h"

#include "color_swatch.h"
#include "scene_item.h"
#include "view.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>

namespace ui {

ColorSamplerTool::ColorSamplerTool(View *view)
    : view_(view)
{
}

void ColorSamplerTool::start()
{
    // Only one tool runs at a time, like the reference's
    // cancel_active_modes().
    view_->cancelModes();
    active_ = true;
    view_->viewport()->setCursor(Qt::CrossCursor);
    if (!swatch_)
        swatch_ = new ColorSwatch(view_->viewport());

    // Show the colour under the pointer right away.
    const QPoint pos = view_->viewport()->mapFromGlobal(QCursor::pos());
    if (view_->viewport()->rect().contains(pos))
        updateSwatch(pos);
    view_->setFocus();
}

void ColorSamplerTool::cancel()
{
    if (!active_)
        return;
    active_ = false;
    if (view_->viewport())
        view_->viewport()->unsetCursor();
    if (swatch_)
        swatch_->hide();
}

QColor ColorSamplerTool::color() const
{
    return swatch_ ? swatch_->color() : QColor();
}

bool ColorSamplerTool::mousePress(QMouseEvent *event)
{
    if (!active_)
        return false;
    if (event->button() == Qt::LeftButton) {
        const QPoint viewportPos = event->position().toPoint();
        if (SceneItem *item = view_->itemAtPoint(viewportPos)) {
            const QColor color = item->sampleColorAt(view_->mapToScene(viewportPos));
            if (color.isValid())
                view_->reportColorSampled(color);
        }
    }
    // Any button leaves the mode, like the reference.
    cancel();
    return true;
}

bool ColorSamplerTool::mouseMove(QMouseEvent *event)
{
    if (!active_)
        return false;
    updateSwatch(event->position().toPoint());
    return true;
}

bool ColorSamplerTool::keyPress(QKeyEvent *event)
{
    Q_UNUSED(event);
    if (!active_)
        return false;
    cancel();
    return true;
}

void ColorSamplerTool::updateSwatch(const QPoint &viewportPos)
{
    if (!swatch_)
        return;
    QColor color;
    if (SceneItem *item = view_->itemAtPoint(viewportPos))
        color = item->sampleColorAt(view_->mapToScene(viewportPos));
    // Without a colour the swatch stays visible but transparent, like
    // the reference's NONE_COLOR.
    swatch_->setColor(color);
    swatch_->moveNear(viewportPos);
    swatch_->show();
}

} // namespace ui
