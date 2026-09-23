#pragma once

#include "settings.h"

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVector>
#include <Qt>

namespace ui::controls {

// The modifier and button names of config/controls.py, used verbatim in
// KeyboardSettings.ini so the file stays shared with the Python port.
QStringList modifierNames();
QStringList buttonNames();

// One mouse binding: a button plus modifiers map to a control group.
struct MouseBinding
{
    QString id;
    QString group;
    QString text;
    QString button;       // "Not Configured", "Left" or "Middle"
    QStringList modifiers;
    bool invertible = false;
    bool inverted = false;
};

// One wheel binding: modifiers map to a control group.
struct WheelBinding
{
    QString id;
    QString group;
    QString text;
    QStringList modifiers;
    bool invertible = false;
    bool inverted = false;
};

// The binding tables of the reference, with their default assignments.
const QVector<MouseBinding> &defaultMouseBindings();
const QVector<WheelBinding> &defaultWheelBindings();

// KeyboardSettings.ini: the bindings and the per-action shortcut
// overrides. A value equal to its default is removed from the file, as
// the reference's set_list/set_value do.
class Store
{
public:
    Store();

    QString path() const { return file_.path(); }

    // Resolved bindings (stored value, else the default).
    MouseBinding mouse(const QString &id) const;
    WheelBinding wheel(const QString &id) const;
    QVector<MouseBinding> mouseBindings() const;
    QVector<WheelBinding> wheelBindings() const;

    // Shortcuts of one action: the stored override, else the defaults.
    QStringList actionShortcuts(const QString &id, const QStringList &defaults) const;

    // Writes an override, or removes the entry when it equals the
    // default.
    void setActionShortcuts(const QString &id, const QStringList &values,
                            const QStringList &defaults);
    void setMouse(const MouseBinding &binding);
    void setWheel(const WheelBinding &binding);

    // Removes every stored binding and shortcut, restoring defaults.
    void restoreDefaults();

private:
    settings::File file_;
};

// A resolved snapshot of the bindings, for the view to match events
// against without reading the file per event.
class Bindings
{
public:
    // Reads the store (defaults when nothing is stored).
    static Bindings load();

    struct Match
    {
        bool valid = false;
        QString group;
        bool inverted = false;
    };

    Match mouseAction(Qt::MouseButton button, Qt::KeyboardModifiers modifiers) const;
    Match wheelAction(Qt::KeyboardModifiers modifiers) const;

    const QVector<MouseBinding> &mouse() const { return mouse_; }
    const QVector<WheelBinding> &wheel() const { return wheel_; }

private:
    QVector<MouseBinding> mouse_;
    QVector<WheelBinding> wheel_;
};

} // namespace ui::controls
