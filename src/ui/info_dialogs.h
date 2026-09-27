#pragma once

#include <QDialog>

namespace ui {

// The About box: name, version, credits and the free-software notice,
// with the licence name linking to the full text.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

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

} // namespace ui
