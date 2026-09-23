#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>

#include <functional>

class QMenu;

namespace ui {

// The reference's action groups: which state enables an action.
enum class ActionGroup {
    Always,
    ItemsInScene,
    Selection,
    SingleImage,
    CanUndo,
    CanRedo,
};

// The state the groups are computed from.
struct ActionState
{
    bool itemsInScene = false;
    bool selection = false;
    bool singleImage = false;
    bool canUndo = false;
    bool canRedo = false;
};

// The reference's action list: one registry per window, with the action
// ids of actions/actions.py, so menus, shortcuts (configurable later,
// like the reference's KeyboardSettings) and state groups all come from
// one place.
class ActionRegistry : public QObject
{
    Q_OBJECT

public:
    using Run = std::function<void(bool checked)>;
    using Checked = std::function<bool()>;

    explicit ActionRegistry(QObject *parent = nullptr);

    QAction *add(const QString &id, const QString &text, const QKeySequence &shortcut,
                 ActionGroup group, Run run, bool checkable = false, Checked isChecked = {});
    QAction *action(const QString &id) const;

    // For the Keyboard & Mouse editor: every action id in registration
    // order, its default shortcut list, and the live override.
    QStringList ids() const;
    QStringList defaultShortcuts(const QString &id) const;
    QStringList shortcuts(const QString &id) const;
    void setShortcuts(const QString &id, const QStringList &shortcuts);

    // Appends one registered action to a menu, in the reference's order.
    void append(QMenu *menu, const QString &id);
    void appendSeparator(QMenu *menu);

    // Enables and checks every action for the given state.
    void setState(const ActionState &state);

private:
    struct Entry
    {
        QAction *action = nullptr;
        ActionGroup group = ActionGroup::Always;
        Checked isChecked;
        QStringList defaults;
    };

    QHash<QString, Entry> entries_;
    QStringList order_;
    ActionState state_;
};

} // namespace ui
