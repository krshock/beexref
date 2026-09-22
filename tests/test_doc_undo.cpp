#include <QtTest>

#include "doc/document.h"
#include "doc/item.h"
#include "doc/undo.h"

namespace {

doc::ItemPtr makeItem(const QString &text)
{
    auto item = std::make_shared<doc::Item>(doc::kTypeText);
    item->setText(text);
    return item;
}

} // namespace

class TestUndo : public QObject
{
    Q_OBJECT

private slots:
    void addUndoRedo();
    void removeReinsertsAtIndex();
    void changeItemUndoRedo();
    void macroIsOneStep();
    void cleanStateTracksIndex();
    void pushClearsRedoBranch();
    void undoRedoText();
};

void TestUndo::addUndoRedo()
{
    auto document = doc::Document::create();
    doc::UndoStack stack(&document);
    const doc::ItemPtr item = makeItem(QStringLiteral("a"));

    stack.push(std::make_unique<doc::AddItemsCommand>(QVector<doc::ItemPtr>{item},
                                                      QStringLiteral("Add")));
    QCOMPARE(document.items().size(), 1);
    QCOMPARE(stack.count(), 1);
    QVERIFY(stack.canUndo());
    QVERIFY(!stack.canRedo());

    QVERIFY(stack.undo());
    QCOMPARE(document.items().size(), 0);
    QVERIFY(item); // the command keeps the item alive
    QVERIFY(stack.canRedo());

    QVERIFY(stack.redo());
    QCOMPARE(document.items().size(), 1);
    QCOMPARE(document.items().first(), item);
    QVERIFY(!stack.canRedo());
}

void TestUndo::removeReinsertsAtIndex()
{
    auto document = doc::Document::create();
    doc::UndoStack stack(&document);
    const doc::ItemPtr first = makeItem(QStringLiteral("first"));
    const doc::ItemPtr middle = makeItem(QStringLiteral("middle"));
    const doc::ItemPtr last = makeItem(QStringLiteral("last"));
    document.addItem(first);
    document.addItem(middle);
    document.addItem(last);

    stack.push(std::make_unique<doc::RemoveItemsCommand>(QVector<doc::ItemPtr>{middle}));
    QCOMPARE(document.items().size(), 2);
    QCOMPARE(document.items().at(0), first);
    QCOMPARE(document.items().at(1), last);

    QVERIFY(stack.undo());
    QCOMPARE(document.items().size(), 3);
    QCOMPARE(document.items().at(0), first);
    QCOMPARE(document.items().at(1), middle);
    QCOMPARE(document.items().at(2), last);
}

void TestUndo::changeItemUndoRedo()
{
    auto document = doc::Document::create();
    doc::UndoStack stack(&document);
    const doc::ItemPtr item = makeItem(QStringLiteral("note"));
    document.addItem(item);

    const doc::ChangeItemCommand::State before = doc::ChangeItemCommand::State::capture(*item);
    item->x = 42;
    item->scale = 2;
    item->setText(QStringLiteral("changed"));
    const doc::ChangeItemCommand::State after = doc::ChangeItemCommand::State::capture(*item);

    stack.push(std::make_unique<doc::ChangeItemCommand>(item, before, after,
                                                        QStringLiteral("Move")));
    QCOMPARE(item->x, 42.0);
    QCOMPARE(item->text(), QStringLiteral("changed"));

    QVERIFY(stack.undo());
    QCOMPARE(item->x, 0.0);
    QCOMPARE(item->scale, 1.0);
    QCOMPARE(item->text(), QStringLiteral("note"));

    QVERIFY(stack.redo());
    QCOMPARE(item->x, 42.0);
    QCOMPARE(item->scale, 2.0);
    QCOMPARE(item->text(), QStringLiteral("changed"));
}

void TestUndo::macroIsOneStep()
{
    auto document = doc::Document::create();
    doc::UndoStack stack(&document);
    const doc::ItemPtr item = makeItem(QStringLiteral("a"));

    stack.beginMacro(QStringLiteral("Paste"));
    stack.push(std::make_unique<doc::AddItemsCommand>(QVector<doc::ItemPtr>{item}));
    const auto before = doc::ChangeItemCommand::State::capture(*item);
    item->x = 100;
    stack.push(std::make_unique<doc::ChangeItemCommand>(
        item, before, doc::ChangeItemCommand::State::capture(*item)));
    stack.endMacro();

    QCOMPARE(document.items().size(), 1);
    QCOMPARE(item->x, 100.0);
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.undoText(), QStringLiteral("Paste"));

    QVERIFY(stack.undo());
    QCOMPARE(document.items().size(), 0);
    QCOMPARE(item->x, 0.0);

    QVERIFY(stack.redo());
    QCOMPARE(document.items().size(), 1);
    QCOMPARE(item->x, 100.0);
}

void TestUndo::cleanStateTracksIndex()
{
    auto document = doc::Document::create();
    doc::UndoStack stack(&document);
    QVERIFY(stack.isClean());

    stack.push(std::make_unique<doc::AddItemsCommand>(
        QVector<doc::ItemPtr>{makeItem(QStringLiteral("a"))}));
    QVERIFY(!stack.isClean());

    stack.setClean();
    QVERIFY(stack.isClean());

    QVERIFY(stack.undo());
    QVERIFY(!stack.isClean());
    QVERIFY(stack.redo());
    QVERIFY(stack.isClean());
}

void TestUndo::pushClearsRedoBranch()
{
    auto document = doc::Document::create();
    doc::UndoStack stack(&document);
    stack.push(std::make_unique<doc::AddItemsCommand>(
        QVector<doc::ItemPtr>{makeItem(QStringLiteral("a"))}));
    QVERIFY(stack.undo());
    QVERIFY(stack.canRedo());

    stack.push(std::make_unique<doc::AddItemsCommand>(
        QVector<doc::ItemPtr>{makeItem(QStringLiteral("b"))}));
    QVERIFY(!stack.canRedo());
    QCOMPARE(document.items().size(), 1);
    QCOMPARE(document.items().first()->text(), QStringLiteral("b"));
    QCOMPARE(stack.count(), 1);
}

void TestUndo::undoRedoText()
{
    auto document = doc::Document::create();
    doc::UndoStack stack(&document);
    QCOMPARE(stack.undoText(), QString());
    QCOMPARE(stack.redoText(), QString());

    stack.push(std::make_unique<doc::AddItemsCommand>(
        QVector<doc::ItemPtr>{makeItem(QStringLiteral("a"))}, QStringLiteral("Insert")));
    QCOMPARE(stack.undoText(), QStringLiteral("Insert"));
    QVERIFY(stack.undo());
    QCOMPARE(stack.redoText(), QStringLiteral("Insert"));
    QCOMPARE(stack.undoText(), QString());
}

QTEST_GUILESS_MAIN(TestUndo)

#include "test_doc_undo.moc"
