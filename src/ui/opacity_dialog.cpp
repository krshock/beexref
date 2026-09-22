#include "opacity_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

namespace ui {

OpacityDialog::OpacityDialog(QWidget *parent, int percent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Change Opacity:"));
    setWindowModality(Qt::WindowModal);

    auto *layout = new QVBoxLayout(this);

    label_ = new QLabel(this);
    layout->addWidget(label_);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 100);
    slider_->setValue(percent);
    layout->addWidget(slider_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        label_->setText(QStringLiteral("Opacity: %1%").arg(value));
        emit percentChanged(value);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    label_->setText(QStringLiteral("Opacity: %1%").arg(percent));
}

int OpacityDialog::percent() const
{
    return slider_->value();
}

void OpacityDialog::setPercent(int percent)
{
    slider_->setValue(percent);
}

} // namespace ui
