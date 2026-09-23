#pragma once

#include <QString>

class QWidget;

namespace ui::hud {

// Shows a short-lived notification at the top of the host, stacked with
// any others: the reference's toast stack (top margin 10, spacing 8, up
// to five visible, three seconds each). The full glass HUD styling
// arrives with the HUD slice; this is the notification layer the
// actions need.
void toast(QWidget *host, const QString &text, int timeoutMs = 3000);

} // namespace ui::hud
