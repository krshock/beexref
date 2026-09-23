#pragma once

#include <QColor>

namespace ui::theme {

// Canvas colours from the ports' shared palette.
inline const QColor canvas(60, 60, 60);
inline const QColor selection(116, 234, 231);
inline const QColor text(200, 200, 200);
inline const QColor error(190, 80, 75);
inline const QColor placeholder(110, 110, 110);

// HUD palette from the ports' shared colours (toasts and panels).
inline const QColor hudBackground(20, 20, 20, 225);
inline const QColor hudSurface(30, 30, 30, 225);
inline const QColor hudBorder(255, 255, 255, 25);
inline const QColor hudForeground(230, 230, 230);
inline const QColor hudMuted(138, 138, 138);
inline const QColor hudEmphasis(255, 255, 255);
inline const QColor hudDanger(190, 80, 75);
inline const QColor hudWarning(185, 145, 60);
inline const QColor hudSuccess(95, 160, 100);
inline const QColor hudInfo(120, 120, 120);
inline const QColor hudHover(44, 44, 44, 225);
inline const QColor hudPressed(16, 16, 16, 225);
inline const QColor hudSelection(83, 167, 165);
inline const QColor hudFocus(90, 181, 179);

} // namespace ui::theme
