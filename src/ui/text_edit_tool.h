#pragma once

#include "tool.h"

#include <QString>

class QEvent;
class QObject;
class QTextEdit;

namespace ui {

class SceneItem;
class View;

// The reference's in-place text editing: a QTextEdit overlay over the
// text item, with the item hiding its own text while the editor is open.
// Enter or a click elsewhere commits as one undo step, Esc cancels,
// Shift+Enter inserts a newline. The editor is a widget, so the canvas
// does not consume mouse events for it: clicking outside commits through
// the focus-out filter and the press proceeds normally.
class TextEditTool : public Tool
{
public:
    explicit TextEditTool(View *view);

    QString id() const override { return QStringLiteral("text_edit"); }
    bool active() const override { return item_ != nullptr; }
    void cancel() override;

    // Starts editing item; a session still open on another item is
    // committed first.
    void start(SceneItem *item);
    // Commits the typed text as one undo step.
    void commit();
    // The editor's event filter: true when the event was consumed
    // (Enter commits, Esc cancels, focus-out commits).
    bool filters(QObject *watched, QEvent *event);
    // Keeps the editor over the item while the view scrolls, zooms or
    // resizes.
    void positionEditor();
    // The scene is about to delete item: drop the session.
    void cancelIfItem(SceneItem *item);

private:
    void finish(bool commit);

    View *view_ = nullptr;
    QTextEdit *editor_ = nullptr;
    SceneItem *item_ = nullptr;
    QString before_;
};

} // namespace ui
