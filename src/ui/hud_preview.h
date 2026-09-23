#pragma once

#include "hud.h"

namespace ui {

class View;

// The reference's runtime HUD style preview (Ctrl+Shift+H, no menu
// entry): a glass panel at the top right of the canvas with sample
// buttons and the semantic colour tokens.
class HudPreview : public hud::HudPanel
{
    Q_OBJECT

public:
    explicit HudPreview(View *view);

    // Places the panel at the top right of the view.
    void moveToTopRight(int margin = 0);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    View *view_ = nullptr;
};

} // namespace ui
