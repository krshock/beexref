#pragma once

#include "doc/image_io.h"

#include <QDialog>

class QRadioButton;

namespace ui {

// The Compact Board dialog: which of the two compaction paths to apply,
// with the estimated sizes from a sample of the board.
class CompactBoardDialog : public QDialog
{
    Q_OBJECT

public:
    CompactBoardDialog(QWidget *parent, doc::StorageMode initial, const QString &estimate);

    doc::StorageMode mode() const;

private:
    QRadioButton *lossless_ = nullptr;
    QRadioButton *compact_ = nullptr;
};

} // namespace ui
