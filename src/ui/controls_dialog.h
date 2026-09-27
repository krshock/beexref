#pragma once

#include "actions.h"
#include "controls.h"

#include <QDialog>
#include <QStringList>

#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTabWidget;
class QTableWidget;

namespace ui {

// The "Keyboard & Mouse Controls" dialog: a search box and a
// table per tab (keyboard shortcuts, mouse buttons, mouse wheel), edits
// written to KeyboardSettings.ini immediately, conflicts resolved by
// clearing the other binding, and Restore Defaults.
class ControlsDialog : public QDialog
{
    Q_OBJECT

public:
    // labelFor returns the "Menu: Action" label of an action id, like
    // the menu path.
    ControlsDialog(QWidget *parent, ActionRegistry *actions,
                   std::function<QString(const QString &id)> labelFor);

    // Restores every binding and shortcut (no confirmation; the button
    // asks first).
    void restoreDefaults();

signals:
    void controlsChanged();

private:
    void buildKeyboardTab(QTabWidget *tabs);
    void buildMouseTab(QTabWidget *tabs);
    void buildWheelTab(QTabWidget *tabs);

    void applyShortcut(int row, int column, const QString &value);
    void applyMouseBinding(int row);
    void applyWheelBinding(int row);
    void refreshKeyboardRow(int row);
    void refreshMouseRow(int row);
    void refreshWheelRow(int row);
    void installSearch(QTableWidget *table, const QString &text);
    // Widens a column to fit the cell widgets it holds (the inline
    // modifier checkboxes need it).
    void sizeWidgetColumn(QTableWidget *table, int column, int padding);

    ActionRegistry *actions_ = nullptr;
    std::function<QString(const QString &)> labelFor_;
    controls::Store store_;

    QTableWidget *keyboardTable_ = nullptr;
    QTableWidget *mouseTable_ = nullptr;
    QTableWidget *wheelTable_ = nullptr;
    // The keyboard table's rows follow the registry order.
    QStringList actionIds_;
    bool updating_ = false;
};

} // namespace ui
