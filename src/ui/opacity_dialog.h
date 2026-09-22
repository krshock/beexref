#pragma once

#include <QDialog>

class QLabel;
class QSlider;

namespace ui {

// The reference's ChangeOpacityDialog: a 0..100 percent slider whose
// value the caller applies live; OK commits one undo step and Cancel
// (or closing) restores what was there before.
class OpacityDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpacityDialog(QWidget *parent, int percent);

    int percent() const;
    void setPercent(int percent);

signals:
    void percentChanged(int percent);

private:
    QLabel *label_ = nullptr;
    QSlider *slider_ = nullptr;
};

} // namespace ui
