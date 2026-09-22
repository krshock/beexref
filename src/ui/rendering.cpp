#include "rendering.h"

namespace ui::rendering {
namespace {

bool g_smoothPixmaps = true;
bool g_smoothingSuspended = false;

} // namespace

bool smoothPixmaps()
{
    return g_smoothPixmaps;
}

void setSmoothPixmaps(bool smooth)
{
    g_smoothPixmaps = smooth;
}

bool smoothingSuspended()
{
    return g_smoothingSuspended;
}

void setSmoothingSuspended(bool suspended)
{
    g_smoothingSuspended = suspended;
}

} // namespace ui::rendering
