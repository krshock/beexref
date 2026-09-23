#pragma once

#include "settings.h"

#include <QDialog>
#include <QGroupBox>
#include <QString>
#include <QVector>

class QWidget;

namespace ui {

// Presentation of one settings field: the reference's per-field widget
// classes (titles, help texts, options), driven from one table.
struct FieldUi
{
    enum class Kind {
        Radio,
        Integer,
        Checkbox,
        LineEdit,
    };

    struct Option
    {
        QString value;
        QString label;
        QString tip;
    };

    QString key; // "Section/key"
    QString title;
    QString help;
    Kind kind = Kind::Checkbox;
    QVector<Option> options; // Radio
    int minimum = 0;         // Integer
    int maximum = 0;
    QString label; // Checkbox
};

// One group box: reads and writes its field, and marks the title when
// the value differs from the default, like the reference.
class SettingsGroup : public QGroupBox
{
    Q_OBJECT

public:
    SettingsGroup(const FieldUi &field, QWidget *parent = nullptr);

    QString key() const { return field_.key; }
    // The control, for tests (object name is the field key).
    QWidget *input() const { return input_; }

    void writeTo(settings::File &file) const;
    void refresh(const settings::File &file);

signals:
    void changed(const QString &key);

private:
    void applyValue(const QVariant &value);
    void markChanged(const settings::File &file);

    FieldUi field_;
    QWidget *input_ = nullptr;
    bool ignore_ = false;
};

// The reference's SettingsDialog: Miscellaneous, RAM and Images & Items
// tabs over the FIELDS table. Changes are written and reported
// immediately; Restore Defaults asks first.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // Restores every field to its default value (no confirmation; the
    // button asks first, the reference's event bus does the refresh).
    void restoreDefaults();

signals:
    void settingChanged(const QString &key);
    void settingsRestored();

private:
    void load();
    settings::File file_;
    QVector<SettingsGroup *> groups_;
};

} // namespace ui
