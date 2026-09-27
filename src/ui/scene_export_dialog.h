#pragma once

#include <QDialog>
#include <QSize>

class QSpinBox;

namespace ui {

// The export size dialog: a width/height pair whose
// aspect ratio is locked to the default export size, clamped to
// [10, 100000].
class SceneExportDialog : public QDialog
{
    Q_OBJECT

public:
    SceneExportDialog(const QSize &defaultSize, QWidget *parent = nullptr);

    QSize value() const;

private:
    void onWidthChanged(int width);
    void onHeightChanged(int height);

    QSize defaultSize_;
    bool ignoreChange_ = false;
    QSpinBox *widthInput_ = nullptr;
    QSpinBox *heightInput_ = nullptr;
};

} // namespace ui
