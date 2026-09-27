#pragma once

#include "tool.h"

#include <QColor>
#include <QPoint>

class QKeyEvent;
class QMouseEvent;

namespace ui {

class ColorSwatch;
class View;

// The Sample Color mode: a crosshair and a swatch follow the
// pointer; a press reports the colour under it and ends the mode.
class ColorSamplerTool : public Tool
{
public:
    explicit ColorSamplerTool(View *view);

    QString id() const override { return QStringLiteral("sample_color"); }
    bool active() const override { return active_; }
    void cancel() override;

    // Starts the mode: ends whatever else is running, shows the
    // crosshair and the swatch under the pointer.
    void start();

    // The colour currently shown in the swatch; invalid when none.
    QColor color() const;

    bool mousePress(QMouseEvent *event) override;
    bool mouseMove(QMouseEvent *event) override;
    bool keyPress(QKeyEvent *event) override;

private:
    void updateSwatch(const QPoint &viewportPos);

    View *view_ = nullptr;
    ColorSwatch *swatch_ = nullptr;
    bool active_ = false;
};

} // namespace ui
