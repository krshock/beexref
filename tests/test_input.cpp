#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "doc/document.h"
#include "ui/drop.h"
#include "ui/input_controller.h"
#include "ui/scene.h"

namespace {

QByteArray makePng(int width, int height, const QColor &color)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString asset(const QString &name)
{
    return QStringLiteral(ASSETS_DIR "/") + name;
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TestInput : public QObject
{
    Q_OBJECT

private slots:
    void dropsImageMimeData();
    void textDropIsRejectedButPasteInserts();
    void dropsFileUriKeepsOriginalBytes();
    void dropsExifFileBakesPng();
    void internalCopyPasteCreatesCopies();
    void pasteSystemClipboardText();
    void cutAndUndoRestores();
};

void TestInput::dropsImageMimeData()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);

    controller.insertMimeData(mime, QPointF(50, 60));
    QCOMPARE(document->items().size(), 1);
    const doc::ItemPtr item = document->items().first();
    QVERIFY(item->isPixmap());
    QVERIFY(item->source->isValid());
    QVERIFY(item->source->bytes().startsWith(QByteArray("\x89PNG", 4)));
    QCOMPARE(item->originalSize(), QSize(6, 4));
    QCOMPARE(scene.itemViews().size(), 1);
    // Centred on the drop point.
    QCOMPARE(item->x, 47.0);
    QCOMPARE(item->y, 58.0);
    QVERIFY(scene.itemViews().first()->isSelected());

    QVERIFY(stack.canUndo());
    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(document->items().size(), 0);
    QCOMPARE(scene.itemViews().size(), 0);
    QVERIFY(stack.redo());
    scene.syncDocument();
    QCOMPARE(document->items().size(), 1);
    QCOMPARE(scene.itemViews().size(), 1);
}

void TestInput::textDropIsRejectedButPasteInserts()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);
    QSignalSpy messages(&controller, &ui::InputController::message);

    // A dropped note is rejected with the reference's message.
    QMimeData mime;
    mime.setText(QStringLiteral("a note"));
    controller.insertMimeData(mime, QPointF(0, 0), 2.0);
    QCOMPARE(document->items().size(), 0);
    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.first().first().toString(), QString::fromLatin1(ui::kNoDropMessage));

    // A pasted note becomes a text item, scaled to screen size.
    QApplication::clipboard()->setText(QStringLiteral("pasted note"));
    controller.paste(QPointF(0, 0), 2.0);
    QCOMPARE(document->items().size(), 1);
    const doc::ItemPtr item = document->items().first();
    QVERIFY(item->isText());
    QCOMPARE(item->text(), QStringLiteral("pasted note"));
    QCOMPARE(item->scale, 0.5);
}

void TestInput::dropsFileUriKeepsOriginalBytes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("photo.png"));
    const QByteArray bytes = makePng(5, 3, Qt::blue);
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }

    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    controller.insertMimeData(mime, QPointF(0, 0));

    QCOMPARE(document->items().size(), 1);
    const doc::ItemPtr item = document->items().first();
    QCOMPARE(item->filename, QStringLiteral("photo.png"));
    QCOMPARE(item->source->bytes(), bytes);
    QCOMPARE(item->originalSize(), QSize(5, 3));
}

void TestInput::dropsExifFileBakesPng()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    const QString path = asset(QStringLiteral("orientation6.jpg"));
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    controller.insertMimeData(mime, QPointF(0, 0));

    QCOMPARE(document->items().size(), 1);
    const doc::ItemPtr item = document->items().first();
    QCOMPARE(item->originalSize(), QSize(2, 4));
    QVERIFY(item->source->bytes().startsWith(QByteArray("\x89PNG", 4)));
    QVERIFY(item->source->bytes() != fileBytes(path));
}

void TestInput::internalCopyPasteCreatesCopies()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QMimeData mime;
    mime.setImageData(image);
    controller.insertMimeData(mime, QPointF(10, 10));
    const doc::ItemPtr original = document->items().first();
    original->uuid = doc::newUuid();

    scene.itemViewFor(original)->setSelected(true);
    controller.copy();
    QVERIFY(controller.hasInternalClipboard());
    QCOMPARE(QApplication::clipboard()->mimeData()->data(QString::fromLatin1(ui::kItemsMime)),
             QByteArrayLiteral("1"));

    controller.paste(QPointF(200, 200), 1.0);
    QCOMPARE(document->items().size(), 2);
    const doc::ItemPtr copy = document->items().last();
    QVERIFY(copy != original);
    QVERIFY(copy->uuid != original->uuid);
    QVERIFY(copy->source->bytes() == original->source->bytes());
    QCOMPARE(copy->originalSize(), original->originalSize());

    // The paste is one undo step.
    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(document->items().size(), 1);
    QVERIFY(stack.redo());
    scene.syncDocument();
    QCOMPARE(document->items().size(), 2);
}

void TestInput::pasteSystemClipboardText()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    QApplication::clipboard()->setText(QStringLiteral("from clipboard"));
    controller.paste(QPointF(0, 0), 1.0);

    QCOMPARE(document->items().size(), 1);
    const doc::ItemPtr item = document->items().first();
    QVERIFY(item->isText());
    QCOMPARE(item->text(), QStringLiteral("from clipboard"));
}

void TestInput::cutAndUndoRestores()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    QMimeData mime;
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::yellow);
    mime.setImageData(image);
    controller.insertMimeData(mime, QPointF(0, 0));
    QCOMPARE(document->items().size(), 1);

    scene.itemViewFor(document->items().first())->setSelected(true);
    controller.cut();
    QCOMPARE(document->items().size(), 0);
    QCOMPARE(scene.itemViews().size(), 0);

    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(document->items().size(), 1);
    QCOMPARE(scene.itemViews().size(), 1);
}

QTEST_MAIN(TestInput)

#include "test_input.moc"
