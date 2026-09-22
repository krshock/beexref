#pragma once

#include "item.h"

#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace doc {

class Document;

// One undoable step. Commands are UI-thread-only and hold shared item
// references, so a removed item stays alive (with its encoded source)
// until the command that owns it is dropped.
class Command
{
public:
    virtual ~Command() = default;
    virtual void redo(Document &document) = 0;
    virtual void undo(Document &document) = 0;
    virtual QString text() const { return {}; }
};

// Undo stack with macro support and a clean marker; QUndoStack
// semantics as used by the reference: push executes the command and
// drops the redo branch.
class UndoStack
{
public:
    UndoStack() = default;
    explicit UndoStack(Document *document)
        : document_(document)
    {
    }

    void setDocument(Document *document) { document_ = document; }
    Document *document() const { return document_; }

    void push(std::unique_ptr<Command> command);

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ < static_cast<qsizetype>(entries_.size()); }
    bool undo();
    bool redo();

    // Groups everything pushed until endMacro into a single step.
    void beginMacro(const QString &text = {});
    void endMacro();
    bool inMacro() const { return macro_; }

    void setClean() { cleanIndex_ = index_; }
    bool isClean() const { return index_ == cleanIndex_; }
    void clear();

    int count() const { return static_cast<int>(entries_.size()); }
    int index() const { return static_cast<int>(index_); }
    QString undoText() const;
    QString redoText() const;

private:
    struct Entry
    {
        std::unique_ptr<Command> command;
        QString text;
    };

    void insertEntry(Entry entry);

    std::vector<Entry> entries_;
    Document *document_ = nullptr;
    qsizetype index_ = 0; // number of executed entries
    qsizetype cleanIndex_ = 0;
    bool macro_ = false;
    QString macroText_;
    std::vector<Entry> macroEntries_;
};

// Adds items at the end of the document; undo removes them again.
class AddItemsCommand final : public Command
{
public:
    explicit AddItemsCommand(QVector<ItemPtr> items, QString text = {});
    void redo(Document &document) override;
    void undo(Document &document) override;
    QString text() const override { return text_; }

private:
    QVector<ItemPtr> items_;
    QString text_;
};

// Removes items; undo reinserts them at their original indices. The
// optional spill callback runs for each item before it leaves the
// document, which is where the session cache takes over the payload of
// an item that only lives in RAM.
class RemoveItemsCommand final : public Command
{
public:
    using Spill = std::function<void(const ItemPtr &)>;

    explicit RemoveItemsCommand(QVector<ItemPtr> items, Spill spill = {},
                                QString text = QString());
    void redo(Document &document) override;
    void undo(Document &document) override;
    QString text() const override { return text_; }

private:
    QVector<ItemPtr> items_;
    QVector<qsizetype> indices_;
    Spill spill_;
    QString text_;
};

// Changes one item's geometry, data and meta in a single step.
class ChangeItemCommand final : public Command
{
public:
    struct State
    {
        double x = 0;
        double y = 0;
        double z = 0;
        double scale = 1;
        double rotation = 0;
        double flip = 1;
        QJsonObject data;
        QJsonObject meta;

        static State capture(const Item &item);
        void apply(Item &item) const;
    };

    ChangeItemCommand(ItemPtr item, State before, State after, QString text = {});
    void redo(Document &document) override;
    void undo(Document &document) override;
    QString text() const override { return text_; }

private:
    ItemPtr item_;
    State before_;
    State after_;
    QString text_;
};

} // namespace doc
