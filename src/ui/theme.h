#pragma once

#include <QColor>
#include <QString>
#include <Qt>

namespace ui::theme {

// Canvas colours from the ports' shared palette. They do not follow the
// theme mode: the board stays dark in light mode too.
inline const QColor canvas(60, 60, 60);
inline const QColor selection(116, 234, 231);
inline const QColor text(200, 200, 200);
inline const QColor error(190, 80, 75);
inline const QColor placeholder(110, 110, 110);

// HUD palette from the ports' shared colours (toasts and panels). Like
// the canvas it stays dark in every mode: the glass look is the app's
// own.
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

// The window colours. System follows the desktop's light or dark
// preference; Dark and Light are this app's own palettes.
enum class Mode
{
    System,
    Dark,
    Light,
};

// The mode stored in View/theme; a missing or unknown value is System.
Mode modeForSetting(const QString &setting);

// What a mode means with the colour scheme the platform reports. System
// follows it; a platform that reports nothing falls back to Dark, the
// look the canvas and the HUD already have.
Mode resolve(Mode mode, Qt::ColorScheme scheme);

// Remembers the style and palette the application started with, so
// System can hand them back. The first call wins; main.cpp calls it
// before anything applies a mode.
void captureStartup();

// Applies a mode: Fusion and an explicit palette for Dark and Light;
// for System the palette the platform theme provides, and when the
// platform reports no scheme, the dark look resolve() falls back to.
void apply(Mode mode, Qt::ColorScheme scheme);

// The window's entry point: captures the startup look on the first call
// and applies the View/theme setting.
void applyFromSettings();

} // namespace ui::theme
