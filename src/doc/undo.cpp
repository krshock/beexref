#include "undo.h"

#include "document.h"

#include <algorithm>
#include <utility>

namespace doc {
namespace {

class MacroCommand final : public Command
{
public:
    explicit MacroCommand(QString text)
        : text_(std::move(text))
    {
    }

    void add(std::unique_ptr<Command> command) { commands_.push_back(std::move(command)); }
    bool isEmpty() const { return commands_.empty(); }

    void redo(Document &document) override
    {
        for (auto &command : commands_)
            command->redo(document);
    }

    void undo(Document &document) override
    {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            (*it)->undo(document);
    }

    QString text() const override { return text_; }

private:
    QString text_;
    std::vector<std::unique_ptr<Command>> commands_;
};

} // namespace

void UndoStack::insertEntry(Entry entry)
{
    if (index_ < static_cast<qsizetype>(entries_.size())) {
        if (cleanIndex_ > index_)
            cleanIndex_ = -1;
        entries_.resize(static_cast<size_t>(index_));
    }
    entries_.push_back(std::move(entry));
    ++index_;
}

void UndoStack::push(std::unique_ptr<Command> command)
{
    if (!command || !document_)
        return;
    const QString text = command->text();
    command->redo(*document_);
    if (macro_) {
        macroEntries_.push_back(Entry{std::move(command), text});
        return;
    }
    insertEntry(Entry{std::move(command), text});
}

bool UndoStack::undo()
{
    if (!document_ || !canUndo())
        return false;
    --index_;
    entries_[static_cast<size_t>(index_)].command->undo(*document_);
    return true;
}

bool UndoStack::redo()
{
    if (!document_ || !canRedo())
        return false;
    entries_[static_cast<size_t>(index_)].command->redo(*document_);
    ++index_;
    return true;
}

void UndoStack::beginMacro(const QString &text)
{
    if (macro_)
        return;
    macro_ = true;
    macroText_ = text;
    macroEntries_.clear();
}

void UndoStack::endMacro()
{
    if (!macro_)
        return;
    macro_ = false;
    if (macroEntries_.empty())
        return;

    auto macro = std::make_unique<MacroCommand>(macroText_);
    for (auto &entry : macroEntries_)
        macro->add(std::move(entry.command));
    macroEntries_.clear();
    insertEntry(Entry{std::move(macro), macroText_});
}

void UndoStack::clear()
{
    entries_.clear();
    macroEntries_.clear();
    macro_ = false;
    index_ = 0;
    cleanIndex_ = 0;
}

QString UndoStack::undoText() const
{
    if (!canUndo())
        return {};
    return entries_[static_cast<size_t>(index_ - 1)].text;
}

QString UndoStack::redoText() const
{
    if (!canRedo())
        return {};
    return entries_[static_cast<size_t>(index_)].text;
}

AddItemsCommand::AddItemsCommand(QVector<ItemPtr> items, QString text)
    : items_(std::move(items))
    , text_(std::move(text))
{
}

void AddItemsCommand::redo(Document &document)
{
    for (const ItemPtr &item : items_)
        document.addItem(item);
}

void AddItemsCommand::undo(Document &document)
{
    for (const ItemPtr &item : items_)
        document.removeItem(item);
}

RemoveItemsCommand::RemoveItemsCommand(QVector<ItemPtr> items, Spill spill, QString text)
    : items_(std::move(items))
    , spill_(std::move(spill))
    , text_(std::move(text))
{
}

void RemoveItemsCommand::redo(Document &document)
{
    if (indices_.isEmpty()) {
        indices_.reserve(items_.size());
        for (const ItemPtr &item : items_)
            indices_.append(qMax(qsizetype(0), document.indexOf(item)));
    }
    for (const ItemPtr &item : items_) {
        if (spill_)
            spill_(item);
        document.removeItem(item);
    }
}

void RemoveItemsCommand::undo(Document &document)
{
    QVector<QPair<qsizetype, ItemPtr>> pairs;
    pairs.reserve(items_.size());
    for (qsizetype i = 0; i < items_.size(); ++i)
        pairs.append({indices_.value(i), items_.at(i)});
    std::sort(pairs.begin(), pairs.end(),
              [](const QPair<qsizetype, ItemPtr> &left, const QPair<qsizetype, ItemPtr> &right) {
                  return left.first < right.first;
              });
    for (const auto &pair : pairs)
        document.insertItem(pair.first, pair.second);
}

ChangeItemCommand::State ChangeItemCommand::State::capture(const Item &item)
{
    State state;
    state.x = item.x;
    state.y = item.y;
    state.z = item.z;
    state.scale = item.scale;
    state.rotation = item.rotation;
    state.flip = item.flip;
    state.data = item.data;
    state.meta = item.meta;
    return state;
}

void ChangeItemCommand::State::apply(Item &item) const
{
    item.x = x;
    item.y = y;
    item.z = z;
    item.scale = scale;
    item.rotation = rotation;
    item.flip = flip;
    item.data = data;
    item.meta = meta;
}

bool ChangeItemCommand::State::operator==(const State &other) const
{
    return qFuzzyCompare(x, other.x) && qFuzzyCompare(y, other.y) && qFuzzyCompare(z, other.z)
        && qFuzzyCompare(scale, other.scale) && qFuzzyCompare(rotation, other.rotation)
        && qFuzzyCompare(flip, other.flip) && data == other.data && meta == other.meta;
}

ChangeItemCommand::ChangeItemCommand(ItemPtr item, State before, State after, QString text)
    : item_(std::move(item))
    , before_(std::move(before))
    , after_(std::move(after))
    , text_(std::move(text))
{
}

void ChangeItemCommand::redo(Document &)
{
    if (item_)
        after_.apply(*item_);
}

void ChangeItemCommand::undo(Document &)
{
    if (item_)
        before_.apply(*item_);
}

} // namespace doc
