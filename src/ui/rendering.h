#pragma once

namespace ui::rendering {

// The user setting: bilinear smoothing for image drawing.
bool smoothPixmaps();
void setSmoothPixmaps(bool smooth);

// Transient: raised during canvas interactions (drag, pan, wheel zoom)
// so repaints during motion skip the filter, and restored when idle.
bool smoothingSuspended();
void setSmoothingSuspended(bool suspended);

} // namespace ui::rendering
