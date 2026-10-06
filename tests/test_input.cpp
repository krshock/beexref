#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include "cache/session_cache.h"
#include "doc/document.h"
#include "doc/image_export.h"
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

// Serves one canned response per connection, so a dropped URL can be
// downloaded without a network (same helper as test_downloader).
class TinyServer : public QTcpServer
{
public:
    explicit TinyServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, &TinyServer::serve);
    }

    QByteArray responseBody;

private:
    void serve()
    {
        QTcpSocket *socket = nextPendingConnection();
        QByteArray request;
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, request]() mutable {
            request += socket->readAll();
            if (!request.contains("\r\n\r\n"))
                return;
            const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\n")
                + QByteArrayLiteral("Content-Type: image/png\r\nContent-Length: ")
                + QByteArray::number(responseBody.size())
                + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + responseBody;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }
};

} // namespace

class TestInput : public QObject
{
    Q_OBJECT

private slots:
    void dropsImageMimeData();
    void textDropIsRejectedButPasteInserts();
    void dropsFileUriKeepsOriginalBytes();
    void dropsRemoteUrlKeepsTheExtractedName();
    void dropsExifFileBakesLossless();
    void internalCopyPasteCreatesCopies();
    void pastePreservesGroupArrangement();
    void pasteSystemClipboardText();
    void cutAndUndoRestores();
    void removingAnImageSpillsItsBytes();
    void undoBeforeTheSpillLandsStillRestores();
    void customInsertHandlerClaimsItsFormat();
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

void TestInput::dropsRemoteUrlKeepsTheExtractedName()
{
    TinyServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.responseBody = makePng(6, 4, Qt::green);

    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);
    QSignalSpy inserted(&controller, &ui::InputController::itemsInserted);

    // A browser drag: the URL's query holds dots, as Instagram's does.
    const QString url = QStringLiteral(
        "http://127.0.0.1:%1/v/t51/783947786_n.jpg"
        "?stp=dst-jpg_e35&ig_cache_key=Mzk3%3D%3D.3-ccb7-5")
        .arg(server.serverPort());
    QMimeData mime;
    mime.setUrls({QUrl(url)});
    controller.insertMimeData(mime, QPointF(0, 0));

    QTRY_COMPARE(inserted.count(), 1);
    QCOMPARE(document->items().size(), 1);
    const doc::ItemPtr item = document->items().first();
    // The import applies the same rule as the images exporter.
    QCOMPARE(item->filename, QStringLiteral("783947786_n.jpg"));
    QCOMPARE(item->meta.value(QStringLiteral("origin_url")).toString(), url);
    QCOMPARE(item->source->bytes(), server.responseBody);
    QCOMPARE(doc::exportFilename(item->filename, item->format, 7),
             QStringLiteral("0007-783947786_n.png"));

    // No usable path segment: the filename stays empty and the export
    // falls back to the item's id.
    QMimeData bare;
    bare.setUrls({QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()))});
    controller.insertMimeData(bare, QPointF(0, 0));
    QTRY_COMPARE(inserted.count(), 2);
    QCOMPARE(document->items().size(), 2);
    QVERIFY(document->items().at(1)->filename.isEmpty());
}

void TestInput::dropsExifFileBakesLossless()
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
    const QByteArray baked = item->source->bytes();
    QVERIFY(baked.startsWith(QByteArray("\x89PNG", 4))
            || (baked.startsWith(QByteArray("RIFF", 4)) && baked.contains("WEBP")));
    QVERIFY(baked != fileBytes(path));
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

void TestInput::pastePreservesGroupArrangement()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    // Three images at distinct positions and sizes.
    const QVector<QPointF> points{QPointF(0, 0), QPointF(120, 40), QPointF(40, 140)};
    const QVector<QSize> sizes{QSize(6, 4), QSize(10, 8), QSize(4, 10)};
    for (int i = 0; i < points.size(); ++i) {
        QImage image(sizes.at(i), QImage::Format_ARGB32);
        image.fill(Qt::green);
        QMimeData mime;
        mime.setImageData(image);
        controller.insertMimeData(mime, points.at(i));
    }
    QCOMPARE(document->items().size(), 3);

    scene.clearSelection();
    for (ui::SceneItem *view : scene.pixmapItemViews())
        view->setSelected(true);
    const QVector<ui::SceneItem *> selected = scene.selectedItemViews();
    QCOMPARE(selected.size(), 3);
    controller.copy();

    controller.paste(QPointF(300, 300), 1.0);
    QCOMPARE(document->items().size(), 6);
    const QVector<doc::ItemPtr> copies = document->items().mid(3, 3);
    QCOMPARE(copies.size(), 3);

    // The copies keep the group's relative arrangement: one shared
    // translation moves each original onto its copy.
    const QPointF delta(copies.first()->x - selected.first()->item()->x,
                        copies.first()->y - selected.first()->item()->y);
    QRectF bounds;
    for (int i = 0; i < copies.size(); ++i) {
        const doc::ItemPtr &original = selected.at(i)->item();
        QCOMPARE(copies.at(i)->x - original->x, delta.x());
        QCOMPARE(copies.at(i)->y - original->y, delta.y());
        const QRectF rect(copies.at(i)->x, copies.at(i)->y, original->originalSize().width(),
                          original->originalSize().height());
        bounds = bounds.isNull() ? rect : bounds.united(rect);
    }
    // And the group as a whole is centred on the paste point.
    QCOMPARE(bounds.center(), QPointF(300, 300));
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

void TestInput::removingAnImageSpillsItsBytes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto cache = cache::SessionCache::create(dir.path());
    QVERIFY(cache->isAvailable());

    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);
    controller.setSessionCache(cache);

    const QByteArray png = makePng(400, 300, Qt::red);
    QImage image;
    image.loadFromData(png);
    QMimeData mime;
    mime.setImageData(image);
    controller.insertMimeData(mime, QPointF(0, 0));
    QCOMPARE(document->items().size(), 1);

    const doc::ItemPtr item = document->items().first();
    const QByteArray payload = item->source->bytes();
    const QString uuid = item->ensureUuid();
    QVERIFY(item->source->residentBytes() > 0);

    scene.itemViewFor(item)->setSelected(true);
    controller.cut();
    QCOMPARE(document->items().size(), 0);

    // The spill runs on a worker; wait for the write and the source
    // swap before looking at the cache.
    controller.waitForSpills();

    // The payload left RAM for the cache, and undo still restores it.
    QCOMPARE(item->source->residentBytes(), qint64(0));
    QVERIFY(item->source->isValid());
    const auto spilled = cache->get(QStringLiteral("undo"), uuid);
    QVERIFY(spilled.has_value());
    QCOMPARE(*spilled, payload);

    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(document->items().size(), 1);
    auto blob = document->blob(*document->items().first());
    QVERIFY(blob.isOk());
    QCOMPARE(blob.value(), payload);
}

void TestInput::undoBeforeTheSpillLandsStillRestores()
{
    // The spill is asynchronous: an undo that arrives while it is still
    // queued must restore the image (the item gets its cache-backed
    // source afterwards, and the bytes stay readable).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto cache = cache::SessionCache::create(dir.path());
    QVERIFY(cache->isAvailable());

    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);
    controller.setSessionCache(cache);

    const QByteArray png = makePng(400, 300, Qt::blue);
    QImage image;
    image.loadFromData(png);
    QMimeData mime;
    mime.setImageData(image);
    controller.insertMimeData(mime, QPointF(0, 0));
    QCOMPARE(document->items().size(), 1);

    const QByteArray payload = document->items().first()->source->bytes();
    scene.itemViewFor(document->items().first())->setSelected(true);
    controller.cut();
    QCOMPARE(document->items().size(), 0);

    // Undo right away, without letting the queued swap land.
    QVERIFY(stack.undo());
    controller.waitForSpills();
    scene.syncDocument();
    QCOMPARE(document->items().size(), 1);
    auto blob = document->blob(*document->items().first());
    QVERIFY(blob.isOk());
    QCOMPARE(blob.value(), payload);
}

void TestInput::customInsertHandlerClaimsItsFormat()
{
    // An extension claims its own mime formats: the handler runs before
    // the built-in classification, both for drops and pastes, and adds
    // its items through insertItems() as one undo step.
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    ui::InputController controller(&scene, &stack);

    const QString format = QStringLiteral("application/x-beexref-test-note");
    controller.addInsertHandler(
        {QStringList{format},
         [&controller, &format](const QMimeData &data, const QPointF &scenePos, double) {
             const QString text = QString::fromUtf8(data.data(format));
             if (text.isEmpty())
                 return false;
             doc::ItemPtr item = doc::createItem(doc::kTypeText);
             item->setText(text);
             controller.insertItems({item}, scenePos, QStringLiteral("Test insert"));
             return true;
         },
         QStringLiteral("Empty test payload")});

    QMimeData mime;
    mime.setData(format, QByteArrayLiteral("hello"));
    QVERIFY(controller.acceptsMimeData(mime)); // drag-enter, formats only
    controller.insertMimeData(mime, QPointF(20, 20));
    QCOMPARE(document->items().size(), 1);
    QVERIFY(document->items().first()->isText());
    QCOMPARE(document->items().first()->text(), QStringLiteral("hello"));
    QVERIFY(stack.canUndo());

    // A payload the handler rejects reports its message and inserts
    // nothing.
    QSignalSpy messages(&controller, &ui::InputController::message);
    QMimeData empty;
    empty.setData(format, QByteArray());
    controller.insertMimeData(empty, QPointF(20, 20));
    QCOMPARE(document->items().size(), 1);
    QCOMPARE(messages.count(), 1);
    QCOMPARE(messages.first().first().toString(), QStringLiteral("Empty test payload"));

    // Paste takes the same route (after the internal-items rule).
    auto *clipboardMime = new QMimeData;
    clipboardMime->setData(format, QByteArrayLiteral("pasted"));
    QApplication::clipboard()->setMimeData(clipboardMime);
    controller.paste(QPointF(30, 30));
    QCOMPARE(document->items().size(), 2);
    QCOMPARE(document->items().last()->text(), QStringLiteral("pasted"));

    // The built-in route is untouched: an image drop still inserts.
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QMimeData imageMime;
    imageMime.setImageData(image);
    controller.insertMimeData(imageMime, QPointF(0, 0));
    QCOMPARE(document->items().size(), 3);
}

QTEST_MAIN(TestInput)

#include "test_input.moc"
