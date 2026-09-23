#pragma once

#include "doc/item.h"

#include <QDialog>

namespace ui {

class SceneItem;

// The reference's Help dialog: the controls documentation in a scroll
// area, bundled as a resource.
class HelpDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HelpDialog(QWidget *parent = nullptr);
};

// The reference's Debug Log dialog: the log file's path and contents,
// with a copy-to-clipboard button.
class DebugLogDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DebugLogDialog(QWidget *parent = nullptr);
};

// The reference's Image Info dialog: the selected item's metadata as a
// label/value grid, the source in a wrapped read-only editor.
class ImageInfoDialog : public QDialog
{
    Q_OBJECT

public:
    ImageInfoDialog(QWidget *parent, const SceneItem *view);
};

// The reference's item.get_metadata() rows, for tests and the dialog.
QVector<QPair<QString, QString>> itemMetadata(const SceneItem *view);

} // namespace ui
