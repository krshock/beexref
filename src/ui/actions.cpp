#include "actions.h"

#include <QMenu>

namespace ui {
namespace {

bool groupEnabled(ActionGroup group, const ActionState &state)
{
    switch (group) {
    case ActionGroup::Always:
        return true;
    case ActionGroup::ItemsInScene:
        return state.itemsInScene;
    case ActionGroup::Selection:
        return state.selection;
    case ActionGroup::SingleImage:
        return state.singleImage;
    case ActionGroup::CanUndo:
        return state.canUndo;
    case ActionGroup::CanRedo:
        return state.canRedo;
    }
    return true;
}

} // namespace

ActionRegistry::ActionRegistry(QObject *parent)
    : QObject(parent)
{
}

QAction *ActionRegistry::add(const QString &id, const QString &text, const QKeySequence &shortcut,
                             ActionGroup group, Run run, bool checkable, Checked isChecked)
{
    auto *action = new QAction(text, this);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    action->setCheckable(checkable);
    connect(action, &QAction::triggered, this, [run](bool checked) {
        if (run)
            run(checked);
    });

    Entry entry;
    entry.action = action;
    entry.group = group;
    entry.isChecked = std::move(isChecked);
    entries_.insert(id, entry);
    return action;
}

QAction *ActionRegistry::action(const QString &id) const
{
    const auto it = entries_.constFind(id);
    return it == entries_.cend() ? nullptr : it.value().action;
}

void ActionRegistry::append(QMenu *menu, const QString &id)
{
    QAction *action = this->action(id);
    if (menu && action)
        menu->addAction(action);
}

void ActionRegistry::appendSeparator(QMenu *menu)
{
    if (menu)
        menu->addSeparator();
}

void ActionRegistry::setState(const ActionState &state)
{
    state_ = state;
    for (const Entry &entry : std::as_const(entries_)) {
        entry.action->setEnabled(groupEnabled(entry.group, state_));
        if (entry.action->isCheckable())
            entry.action->setChecked(entry.isChecked ? entry.isChecked() : false);
    }
}

} // namespace ui
