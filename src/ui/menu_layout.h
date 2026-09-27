#pragma once

#include <QString>
#include <QVector>

namespace ui {

// One entry in a menu bar menu:
//  * an action id (id non-empty),
//  * a separator (everything empty),
//  * a submenu (submenuTitle non-empty, submenuIds its actions),
//  * the dynamic Open Recent submenu (recent: its actions are rebuilt
//    from the settings whenever it is shown).
struct MenuEntry
{
    QString id;
    QString submenuTitle;
    QVector<QString> submenuIds;
    bool recent = false;
};

struct MenuDef
{
    QString title;
    QVector<MenuEntry> entries;
};

// The menu structure as data: putting a
// command in a menu means registering it in MainWindow::buildActions()
// and naming its id here, so the two cannot drift apart. The context
// menu is copied from the built menu bar, so it follows this table too.
const QVector<MenuDef> &menuLayout();

} // namespace ui
