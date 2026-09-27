#include "text_edit_tool.h"

#include "scene_item.h"
#include "theme.h"
#include "view.h"

#include "doc/undo.h"

#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QTextEdit>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

namespace ui {

TextEditTool::TextEditTool(View *view)
    : view_(view)
{
}

void TextEditTool::start(SceneItem *item)
{
    if (!item || !item->isText())
        return;
    if (item_ == item) {
        if (editor_)
            editor_->setFocus();
        return;
    }
    // Commit whatever another item's editor still holds.
    if (item_)
        commit();

    item_ = item;
    before_ = item->item()->text();

    if (!editor_) {
        editor_ = new QTextEdit(view_->viewport());
        editor_->setObjectName(QStringLiteral("textEditor"));
        editor_->setAcceptRichText(false);
        editor_->setFrameShape(QFrame::NoFrame);
        editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        editor_->setStyleSheet(
            QStringLiteral("QTextEdit { background: %1; color: %2;"
                           " border: 1px dashed %3; padding: 2px; }")
                .arg(theme::canvas.name(), theme::text.name(), theme::selection.name()));
        editor_->installEventFilter(view_);
        QObject::connect(editor_, &QTextEdit::textChanged, view_,
                         [this]() { positionEditor(); });
    }

    editor_->setPlainText(before_);
    editor_->selectAll();
    item->setTextEditing(true);
    editor_->show();
    editor_->setFocus();
    positionEditor();
}

void TextEditTool::commit()
{
    finish(true);
}

void TextEditTool::cancel()
{
    finish(false);
}

void TextEditTool::cancelIfItem(SceneItem *item)
{
    if (item_ == item)
        cancel();
}

bool TextEditTool::filters(QObject *watched, QEvent *event)
{
    if (watched != editor_ || !editor_)
        return false;
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            cancel();
            return true;
        }
        // Enter commits, Shift+Enter falls through to the editor's
        // newline, like the reference's text item.
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && !key->modifiers().testFlag(Qt::ShiftModifier)) {
            commit();
            return true;
        }
    } else if (event->type() == QEvent::FocusOut) {
        // Clicking elsewhere in the canvas commits.
        commit();
    }
    return false;
}

void TextEditTool::positionEditor()
{
    if (!item_ || !editor_ || !editor_->isVisible())
        return;

    // The text is drawn in item-local pixels, scaled by the item's
    // transform and the view; the editor's font must match that.
    const double totalScale = qMax(0.0001, view_->scaleFor(item_));
    QFont font = item_->font();
    if (font.pointSizeF() > 0)
        font.setPointSizeF(qMax(1.0, font.pointSizeF() * totalScale));
    else if (font.pixelSize() > 0)
        font.setPixelSize(qMax(1, qRound(font.pixelSize() * totalScale)));
    editor_->setFont(font);

    const QRect viewRect =
        view_->mapFromScene(item_->mapToScene(item_->boundingRect())).boundingRect();

    // The same wrap width the item uses, so the committed layout matches
    // what was typed; never wider than the viewport allows.
    const int room = qMax(60, view_->viewport()->width() - viewRect.x() - 4);
    const int width = qBound(60, qRound(SceneItem::kTextWrapWidth * totalScale), room);
    editor_->setFixedWidth(width);
    const int content =
        static_cast<int>(std::ceil(editor_->document()->size().height())) + 4;
    editor_->setFixedHeight(qMax(viewRect.height(), content));
    editor_->move(viewRect.topLeft());
}

void TextEditTool::finish(bool commit)
{
    if (!item_ || !editor_)
        return;

    SceneItem *item = item_;
    const QString text = editor_->toPlainText();

    // Cleared before hiding: hiding delivers a focus-out that would
    // otherwise re-enter this function.
    item_ = nullptr;
    editor_->hide();
    item->setTextEditing(false);

    if (!commit || text == before_)
        return;

    const doc::ChangeItemCommand::State before =
        doc::ChangeItemCommand::State::capture(*item->item());
    item->setText(text);
    if (doc::UndoStack *stack = view_->undoStack()) {
        stack->push(std::make_unique<doc::ChangeItemCommand>(
            item->item(), before, doc::ChangeItemCommand::State::capture(*item->item()),
            QStringLiteral("Edit text")));
    }
    view_->markDocumentModified();
}

} // namespace ui
