#include "item_types.h"

#include "doc/item.h"
#include "scene_item.h"

namespace ui::item_types {
namespace {

const Traits kPixmapTraits{&SceneItem::boundsPixmap, &SceneItem::paintPixmap, true, true, "image"};
const Traits kTextTraits{&SceneItem::boundsText, &SceneItem::paintTextItem, false, false, "text"};
const Traits kErrorTraits{&SceneItem::boundsError, &SceneItem::paintErrorItem, false, false,
                          nullptr};

} // namespace

const Traits &forType(const QString &type)
{
    if (type == QLatin1String(doc::kTypePixmap))
        return kPixmapTraits;
    if (type == QLatin1String(doc::kTypeText))
        return kTextTraits;
    return kErrorTraits;
}

} // namespace ui::item_types
