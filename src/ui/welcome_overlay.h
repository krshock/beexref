#pragma once

#include <QPixmap>
#include <QStringList>
#include <QStyle>
#include <QWidget>

#include <functional>

class QBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QMimeData;

namespace ui {

// The empty-scene overlay: one widget with two states, so an
// empty board in an open document can never be mistaken for the start
// screen.
//
//   * Start: no document open; shows the app name and the recent files.
//   * Empty: a document is open but has no items; shows the board name
//     and the document actions.
//
// It covers the canvas, so it also takes drops (the window accepts the
// same payloads as the canvas) and offers Insert/Open/Undo.
class WelcomeOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        Start,
        Empty,
    };

    using MimeFilter = std::function<bool(const QMimeData &)>;

    explicit WelcomeOverlay(QWidget *canvas);

    // The canvas whose size the overlay follows.
    void setCanvas(QWidget *canvas);
    void setMode(Mode mode);
    Mode mode() const { return mode_; }
    // Whether the recent-files card sits beside the welcome card (when
    // stacking it would not fit vertically) rather than below it.
    bool sideBySide() const { return sideBySide_; }
    void setRecentFiles(const QStringList &files);
    // The board name shown in empty-board mode (elided in the middle).
    void setBoardName(const QString &name);
    // The undo button's state and tooltip.
    void setUndoState(bool canUndo, const QString &text);
    // Same filter contract as the view's drops.
    void setMimeFilter(std::function<bool(const QMimeData &)> filter);

    // Shows the overlay (fading in) and hides it.
    void showOverlay();
    void hideOverlay();

signals:
    // A recent file was clicked.
    void recentFileActivated(const QString &path);
    // A payload was dropped on the overlay.
    void mimeDropped(const QMimeData *data, const QPointF &scenePos);
    void insertImagesRequested();
    void openFileRequested();
    void undoRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QWidget *makeCard();
    QPushButton *makeButton(const QString &text, QStyle::StandardPixmap icon);
    void setPrimary(QPushButton *button, bool primary);
    void updateVisibility();

    QWidget *canvas_ = nullptr;
    Mode mode_ = Mode::Start;
    QStringList recentFiles_;
    bool hasRecentFiles_ = false;
    MimeFilter mimeFilter_;
    QPixmap logo_;

    QWidget *content_ = nullptr;
    QBoxLayout *contentLayout_ = nullptr;
    bool sideBySide_ = false;
    QWidget *card_ = nullptr;
    QLabel *logoLabel_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *subtitleLabel_ = nullptr;
    QLabel *hintLabel_ = nullptr;
    QPushButton *insertButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *undoButton_ = nullptr;
    QWidget *filesCard_ = nullptr;
    QListWidget *filesView_ = nullptr;
};

} // namespace ui
