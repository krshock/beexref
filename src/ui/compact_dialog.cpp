#include "compact_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace ui {

CompactBoardDialog::CompactBoardDialog(QWidget *parent, doc::StorageMode initial,
                                       const QString &estimate)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Compact Board"));

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(
        QStringLiteral("Re-encode the board's lossless images to make the file smaller. "
                       "A new file is written; the original is left untouched."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    lossless_ = new QRadioButton(QStringLiteral("Compact lossless"), this);
    layout->addWidget(lossless_);
    auto *losslessHelp = new QLabel(
        QStringLiteral("Re-encodes losslessly as WebP when that is smaller. The pixels are "
                       "untouched; expect a modest saving."),
        this);
    losslessHelp->setWordWrap(true);
    losslessHelp->setContentsMargins(24, 0, 0, 0);
    layout->addWidget(losslessHelp);

    compact_ = new QRadioButton(QStringLiteral("Compact (imperceptible)"), this);
    layout->addWidget(compact_);
    auto *compactHelp = new QLabel(
        QStringLiteral("WebP at quality 95 for photographs, lossless where artifacts would "
                       "show. Much smaller files; transparency stays exact."),
        this);
    compactHelp->setWordWrap(true);
    compactHelp->setContentsMargins(24, 0, 0, 0);
    layout->addWidget(compactHelp);

    if (initial == doc::StorageMode::Compact)
        compact_->setChecked(true);
    else
        lossless_->setChecked(true);

    if (!estimate.isEmpty()) {
        auto *estimateLabel = new QLabel(estimate, this);
        estimateLabel->setObjectName(QStringLiteral("compactEstimate"));
        estimateLabel->setWordWrap(true);
        layout->addWidget(estimateLabel);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Compact"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

doc::StorageMode CompactBoardDialog::mode() const
{
    return compact_->isChecked() ? doc::StorageMode::Compact : doc::StorageMode::Lossless;
}

} // namespace ui
