#include "export_conflict_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace ui {

ExportConflictDialog::ExportConflictDialog(const QString &filename, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("File exists"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("File already exists:\n%1").arg(filename), this));

    const struct
    {
        doc::ExportConflict value;
        QString label;
    } choices[] = {
        {doc::ExportConflict::Skip, QStringLiteral("Skip this file")},
        {doc::ExportConflict::SkipAll, QStringLiteral("Skip all existing files")},
        {doc::ExportConflict::Overwrite, QStringLiteral("Overwrite this file")},
        {doc::ExportConflict::OverwriteAll, QStringLiteral("Overwrite all existing files")},
    };

    for (const auto &choice : choices) {
        auto *button = new QRadioButton(choice.label, this);
        buttons_.insert(static_cast<int>(choice.value), button);
        layout->addWidget(button);
    }
    buttons_.value(static_cast<int>(doc::ExportConflict::Skip))->setChecked(true);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}

doc::ExportConflict ExportConflictDialog::answer() const
{
    for (auto it = buttons_.constBegin(); it != buttons_.constEnd(); ++it) {
        if (it.value()->isChecked())
            return static_cast<doc::ExportConflict>(it.key());
    }
    return doc::ExportConflict::Skip;
}

} // namespace ui
