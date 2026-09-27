#include "controls_dialog.h"

#include "constants.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace ui {
namespace {

// The changed marker column of the tables.
QString changedMark(bool changed)
{
    return changed ? QString::fromUtf8(constants::kChangedSymbol) : QString();
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

// The modifier checkboxes of one binding row.
class ModifierBoxes : public QWidget
{
public:
    ModifierBoxes(const QStringList &selected, QWidget *parent)
        : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        for (const QString &name : controls::modifierNames()) {
            auto *box = new QCheckBox(name, this);
            box->setObjectName(name);
            box->setChecked(selected.contains(name));
            // "No Modifier" stands alone, as the app enforces.
            connect(box, &QCheckBox::checkStateChanged, this, [this, name](Qt::CheckState state) {
                if (name != QLatin1String("No Modifier"))
                    return;
                if (state != Qt::Checked)
                    return;
                for (QCheckBox *other : findChildren<QCheckBox *>()) {
                    if (other->objectName() != name)
                        other->setChecked(false);
                }
            });
            connect(box, &QCheckBox::checkStateChanged, this, [this, name](Qt::CheckState state) {
                if (name == QLatin1String("No Modifier"))
                    return;
                if (state != Qt::Checked)
                    return;
                if (auto *none = findChild<QCheckBox *>(QStringLiteral("No Modifier")))
                    none->setChecked(false);
            });
            layout->addWidget(box);
        }
    }

    QStringList modifiers() const
    {
        QStringList values;
        for (QCheckBox *box : findChildren<QCheckBox *>()) {
            if (box->isChecked())
                values.append(box->objectName());
        }
        return values;
    }
};

} // namespace

ControlsDialog::ControlsDialog(QWidget *parent, ActionRegistry *actions,
                               std::function<QString(const QString &id)> labelFor)
    : QDialog(parent)
    , actions_(actions)
    , labelFor_(std::move(labelFor))
{
    setWindowTitle(QStringLiteral("Keyboard & Mouse Controls"));
    auto *tabs = new QTabWidget(this);
    buildKeyboardTab(tabs);
    buildMouseTab(tabs);
    buildWheelTab(tabs);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *resetButton = new QPushButton(QStringLiteral("&Restore Defaults"), this);
    resetButton->setAutoDefault(false);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("Restore defaults?"),
            QStringLiteral("Do you want to restore all keyboard and mouse settings to their "
                           "default values?"));
        if (reply == QMessageBox::Yes)
            restoreDefaults();
    });
    buttons->addButton(resetButton, QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    resize(860, 500);
}

void ControlsDialog::buildKeyboardTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
    auto *layout = new QVBoxLayout(page);
    auto *search = new QLineEdit(page);
    search->setPlaceholderText(QStringLiteral("Search..."));
    layout->addWidget(search);

    keyboardTable_ = new QTableWidget(page);
    keyboardTable_->setObjectName(QStringLiteral("keyboardTable"));
    keyboardTable_->setColumnCount(4);
    keyboardTable_->setHorizontalHeaderLabels({QStringLiteral("Action"), changedMark(true),
                                               QStringLiteral("Shortcut"),
                                               QStringLiteral("Alternative")});
    keyboardTable_->verticalHeader()->hide();
    keyboardTable_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(keyboardTable_);

    actionIds_ = actions_ ? actions_->ids() : QStringList();
    keyboardTable_->setRowCount(actionIds_.size());
    for (int row = 0; row < actionIds_.size(); ++row)
        refreshKeyboardRow(row);

    auto *keyboardHeader = keyboardTable_->horizontalHeader();
    keyboardHeader->setStretchLastSection(false);
    keyboardHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    keyboardHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    sizeWidgetColumn(keyboardTable_, 2, 24);
    sizeWidgetColumn(keyboardTable_, 3, 24);

    connect(search, &QLineEdit::textChanged, this,
            [this](const QString &text) { installSearch(keyboardTable_, text); });
    tabs->addTab(page, QStringLiteral("&Keyboard Shortcuts"));
}

void ControlsDialog::buildMouseTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
    auto *layout = new QVBoxLayout(page);
    auto *search = new QLineEdit(page);
    search->setPlaceholderText(QStringLiteral("Search..."));
    layout->addWidget(search);

    mouseTable_ = new QTableWidget(page);
    mouseTable_->setObjectName(QStringLiteral("mouseTable"));
    mouseTable_->setColumnCount(5);
    mouseTable_->setHorizontalHeaderLabels({QStringLiteral("Action"), changedMark(true),
                                            QStringLiteral("Button"),
                                            QStringLiteral("Modifiers"),
                                            QStringLiteral("Inverted")});
    mouseTable_->verticalHeader()->hide();
    mouseTable_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(mouseTable_);

    const QVector<controls::MouseBinding> bindings = controls::defaultMouseBindings();
    mouseTable_->setRowCount(bindings.size());
    for (int row = 0; row < bindings.size(); ++row)
        refreshMouseRow(row);

    auto *mouseHeader = mouseTable_->horizontalHeader();
    mouseHeader->setStretchLastSection(false);
    mouseHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    mouseHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    sizeWidgetColumn(mouseTable_, 2, 24);
    sizeWidgetColumn(mouseTable_, 3, 24);

    connect(search, &QLineEdit::textChanged, this,
            [this](const QString &text) { installSearch(mouseTable_, text); });
    tabs->addTab(page, QStringLiteral("&Mouse"));
}

void ControlsDialog::buildWheelTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
    auto *layout = new QVBoxLayout(page);
    auto *search = new QLineEdit(page);
    search->setPlaceholderText(QStringLiteral("Search..."));
    layout->addWidget(search);

    wheelTable_ = new QTableWidget(page);
    wheelTable_->setObjectName(QStringLiteral("wheelTable"));
    wheelTable_->setColumnCount(4);
    wheelTable_->setHorizontalHeaderLabels({QStringLiteral("Action"), changedMark(true),
                                            QStringLiteral("Modifiers"),
                                            QStringLiteral("Inverted")});
    wheelTable_->verticalHeader()->hide();
    wheelTable_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(wheelTable_);

    const QVector<controls::WheelBinding> bindings = controls::defaultWheelBindings();
    wheelTable_->setRowCount(bindings.size());
    for (int row = 0; row < bindings.size(); ++row)
        refreshWheelRow(row);

    auto *wheelHeader = wheelTable_->horizontalHeader();
    wheelHeader->setStretchLastSection(false);
    wheelHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    wheelHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    sizeWidgetColumn(wheelTable_, 2, 24);

    connect(search, &QLineEdit::textChanged, this,
            [this](const QString &text) { installSearch(wheelTable_, text); });
    tabs->addTab(page, QStringLiteral("Mouse &Wheel"));
}

void ControlsDialog::sizeWidgetColumn(QTableWidget *table, int column, int padding)
{
    if (!table || column < 0 || column >= table->columnCount())
        return;
    int width = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (QWidget *widget = table->cellWidget(row, column))
            width = qMax(width, widget->sizeHint().width());
    }
    if (width > 0)
        table->setColumnWidth(column, width + padding);
}

void ControlsDialog::installSearch(QTableWidget *table, const QString &text)
{
    if (!table)
        return;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, 0);
        const bool matches =
            text.isEmpty() || (item && item->text().contains(text, Qt::CaseInsensitive));
        table->setRowHidden(row, !matches);
    }
}

void ControlsDialog::refreshKeyboardRow(int row)
{
    if (!keyboardTable_ || !actions_ || row >= actionIds_.size())
        return;
    updating_ = true;
    const QString id = actionIds_.at(row);
    const QStringList defaults = actions_->defaultShortcuts(id);
    const QStringList current = store_.actionShortcuts(id, defaults);

    keyboardTable_->setItem(row, 0, readOnlyItem(labelFor_ ? labelFor_(id) : id));
    keyboardTable_->setItem(row, 1, readOnlyItem(changedMark(current != defaults)));

    for (int column = 2; column <= 3; ++column) {
        const QString value = column - 2 < current.size() ? current.at(column - 2) : QString();
        auto *editor = new QKeySequenceEdit(QKeySequence(value, QKeySequence::PortableText),
                                            keyboardTable_);
        editor->setObjectName(QStringLiteral("%1_%2").arg(id).arg(column - 2));
        connect(editor, &QKeySequenceEdit::editingFinished, this, [this, row, column, editor]() {
            applyShortcut(row, column,
                          editor->keySequence().toString(QKeySequence::PortableText));
        });
        keyboardTable_->setCellWidget(row, column, editor);
    }
    updating_ = false;
}

void ControlsDialog::refreshMouseRow(int row)
{
    if (!mouseTable_)
        return;
    const QVector<controls::MouseBinding> defaults = controls::defaultMouseBindings();
    if (row >= defaults.size())
        return;
    updating_ = true;
    const controls::MouseBinding binding = store_.mouse(defaults.at(row).id);
    const bool changed = binding.button != defaults.at(row).button
        || binding.modifiers != defaults.at(row).modifiers
        || binding.inverted != defaults.at(row).inverted;

    mouseTable_->setItem(row, 0, readOnlyItem(binding.text + QStringLiteral(" (") + binding.id
                                              + QStringLiteral(")")));
    mouseTable_->setItem(row, 1, readOnlyItem(changedMark(changed)));

    auto *button = new QComboBox(mouseTable_);
    button->addItems(controls::buttonNames());
    button->setCurrentText(binding.button);
    connect(button, &QComboBox::currentTextChanged, this, [this, row](const QString &) {
        applyMouseBinding(row);
    });
    mouseTable_->setCellWidget(row, 2, button);

    auto *modifiers = new ModifierBoxes(binding.modifiers, mouseTable_);
    for (QCheckBox *box : modifiers->findChildren<QCheckBox *>())
        connect(box, &QCheckBox::checkStateChanged, this, [this, row](Qt::CheckState) {
            applyMouseBinding(row);
        });
    mouseTable_->setCellWidget(row, 3, modifiers);

    auto *inverted = new QCheckBox(mouseTable_);
    inverted->setChecked(binding.inverted);
    inverted->setEnabled(binding.invertible);
    connect(inverted, &QCheckBox::checkStateChanged, this,
            [this, row](Qt::CheckState) { applyMouseBinding(row); });
    mouseTable_->setCellWidget(row, 4, inverted);
    updating_ = false;
}

void ControlsDialog::refreshWheelRow(int row)
{
    if (!wheelTable_)
        return;
    const QVector<controls::WheelBinding> defaults = controls::defaultWheelBindings();
    if (row >= defaults.size())
        return;
    updating_ = true;
    const controls::WheelBinding binding = store_.wheel(defaults.at(row).id);
    const bool changed = binding.modifiers != defaults.at(row).modifiers
        || binding.inverted != defaults.at(row).inverted;

    wheelTable_->setItem(row, 0, readOnlyItem(binding.text + QStringLiteral(" (") + binding.id
                                              + QStringLiteral(")")));
    wheelTable_->setItem(row, 1, readOnlyItem(changedMark(changed)));

    auto *modifiers = new ModifierBoxes(binding.modifiers, wheelTable_);
    for (QCheckBox *box : modifiers->findChildren<QCheckBox *>())
        connect(box, &QCheckBox::checkStateChanged, this, [this, row](Qt::CheckState) {
            applyWheelBinding(row);
        });
    wheelTable_->setCellWidget(row, 2, modifiers);

    auto *inverted = new QCheckBox(wheelTable_);
    inverted->setChecked(binding.inverted);
    inverted->setEnabled(binding.invertible);
    connect(inverted, &QCheckBox::checkStateChanged, this,
            [this, row](Qt::CheckState) { applyWheelBinding(row); });
    wheelTable_->setCellWidget(row, 3, inverted);
    updating_ = false;
}

void ControlsDialog::applyShortcut(int row, int column, const QString &value)
{
    if (updating_ || !actions_ || row >= actionIds_.size())
        return;
    const QString id = actionIds_.at(row);
    const QStringList defaults = actions_->defaultShortcuts(id);
    QStringList values = store_.actionShortcuts(id, defaults);

    // The shortcut columns start at column 2.
    const int slot = column - 2;
    while (values.size() <= slot)
        values.append(QString());
    values[slot] = value;
    values.removeAll(QString());
    values.removeDuplicates();

    store_.setActionShortcuts(id, values, defaults);
    actions_->setShortcuts(id, values);

    // A shortcut belongs to one action: take it from the other one.
    if (!value.isEmpty()) {
        for (const QString &otherId : actionIds_) {
            if (otherId == id)
                continue;
            const QStringList otherDefaults = actions_->defaultShortcuts(otherId);
            QStringList otherValues = actions_->shortcuts(otherId);
            if (!otherValues.removeOne(value))
                continue;
            store_.setActionShortcuts(otherId, otherValues, otherDefaults);
            actions_->setShortcuts(otherId, otherValues);
            refreshKeyboardRow(actionIds_.indexOf(otherId));
        }
    }

    refreshKeyboardRow(row);
    emit controlsChanged();
}

void ControlsDialog::applyMouseBinding(int row)
{
    if (updating_)
        return;
    const QVector<controls::MouseBinding> defaults = controls::defaultMouseBindings();
    if (row >= defaults.size())
        return;
    controls::MouseBinding binding = defaults.at(row);
    if (auto *button = qobject_cast<QComboBox *>(mouseTable_->cellWidget(row, 2)))
        binding.button = button->currentText();
    if (auto *modifiers = dynamic_cast<ModifierBoxes *>(mouseTable_->cellWidget(row, 3)))
        binding.modifiers = modifiers->modifiers();
    if (auto *inverted = qobject_cast<QCheckBox *>(mouseTable_->cellWidget(row, 4)))
        binding.inverted = inverted->isChecked();

    store_.setMouse(binding);

    // Clear any other binding that now matches the same button and
    // modifiers.
    if (binding.button != QStringLiteral("Not Configured")) {
        for (int other = 0; other < defaults.size(); ++other) {
            if (other == row)
                continue;
            controls::MouseBinding otherBinding = store_.mouse(defaults.at(other).id);
            if (otherBinding.button != binding.button
                || otherBinding.modifiers != binding.modifiers) {
                continue;
            }
            otherBinding.button = QStringLiteral("Not Configured");
            otherBinding.modifiers = {};
            store_.setMouse(otherBinding);
            refreshMouseRow(other);
        }
    }
    refreshMouseRow(row);
    emit controlsChanged();
}

void ControlsDialog::applyWheelBinding(int row)
{
    if (updating_)
        return;
    const QVector<controls::WheelBinding> defaults = controls::defaultWheelBindings();
    if (row >= defaults.size())
        return;
    controls::WheelBinding binding = defaults.at(row);
    if (auto *modifiers = dynamic_cast<ModifierBoxes *>(wheelTable_->cellWidget(row, 2)))
        binding.modifiers = modifiers->modifiers();
    if (auto *inverted = qobject_cast<QCheckBox *>(wheelTable_->cellWidget(row, 3)))
        binding.inverted = inverted->isChecked();

    store_.setWheel(binding);

    if (!binding.modifiers.isEmpty()) {
        for (int other = 0; other < defaults.size(); ++other) {
            if (other == row)
                continue;
            controls::WheelBinding otherBinding = store_.wheel(defaults.at(other).id);
            if (otherBinding.modifiers != binding.modifiers)
                continue;
            otherBinding.modifiers = {};
            store_.setWheel(otherBinding);
            refreshWheelRow(other);
        }
    }
    refreshWheelRow(row);
    emit controlsChanged();
}

void ControlsDialog::restoreDefaults()
{
    store_.restoreDefaults();
    for (int row = 0; row < actionIds_.size(); ++row)
        refreshKeyboardRow(row);
    for (int row = 0; row < controls::defaultMouseBindings().size(); ++row)
        refreshMouseRow(row);
    for (int row = 0; row < controls::defaultWheelBindings().size(); ++row)
        refreshWheelRow(row);
    emit controlsChanged();
}

} // namespace ui
