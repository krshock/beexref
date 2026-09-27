#pragma once

#include "doc/image_export.h"

#include <QDialog>
#include <QHash>

class QRadioButton;

namespace ui {

// The export-conflict dialog: pick what to do when an
// exported file already exists. Defaults to Skip.
class ExportConflictDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportConflictDialog(const QString &filename, QWidget *parent = nullptr);

    doc::ExportConflict answer() const;

private:
    QHash<int, QRadioButton *> buttons_;
};

} // namespace ui
