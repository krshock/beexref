#include "scene_export_dialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>

namespace ui {
namespace {

constexpr int kMinSize = 10;
constexpr int kMaxSize = 100000;

} // namespace

SceneExportDialog::SceneExportDialog(const QSize &defaultSize, QWidget *parent)
    : QDialog(parent)
    , defaultSize_(defaultSize)
{
    if (defaultSize_.width() >= kMaxSize)
        defaultSize_.scale(kMaxSize, kMaxSize, Qt::KeepAspectRatio);

    setWindowTitle(QStringLiteral("Export Scene to Image"));
    setWindowModality(Qt::WindowModal);

    auto *layout = new QGridLayout(this);

    layout->addWidget(new QLabel(QStringLiteral("Width:")), 0, 0);
    widthInput_ = new QSpinBox(this);
    widthInput_->setRange(kMinSize, kMaxSize);
    widthInput_->setValue(defaultSize_.width());
    connect(widthInput_, &QSpinBox::valueChanged, this, &SceneExportDialog::onWidthChanged);
    layout->addWidget(widthInput_, 0, 1);

    layout->addWidget(new QLabel(QStringLiteral("Height:")), 1, 0);
    heightInput_ = new QSpinBox(this);
    heightInput_->setRange(kMinSize, kMaxSize);
    heightInput_->setValue(defaultSize_.height());
    connect(heightInput_, &QSpinBox::valueChanged, this, &SceneExportDialog::onHeightChanged);
    layout->addWidget(heightInput_, 1, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons, 3, 1);
}

QSize SceneExportDialog::value() const
{
    return QSize(widthInput_->value(), heightInput_->value());
}

void SceneExportDialog::onWidthChanged(int width)
{
    if (ignoreChange_)
        return;
    ignoreChange_ = true;
    const QSize scaled = defaultSize_.scaled(width, kMaxSize, Qt::KeepAspectRatio);
    heightInput_->setValue(scaled.height());
    ignoreChange_ = false;
}

void SceneExportDialog::onHeightChanged(int height)
{
    if (ignoreChange_)
        return;
    ignoreChange_ = true;
    const QSize scaled = defaultSize_.scaled(kMaxSize, height, Qt::KeepAspectRatio);
    widthInput_->setValue(scaled.width());
    ignoreChange_ = false;
}

} // namespace ui
