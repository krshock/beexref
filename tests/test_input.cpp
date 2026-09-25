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
    void dropsExifFileBakesPng();
    void internalCopyPasteCreatesCopies();
    void pasteSystemClipboardText();
    void cutAndUndoRestores();
    void removingAnImageSpillsItsBytes();
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
    const QString uuid = item->ensureUuid();
    QVERIFY(item->source->residentBytes() > 0);

    scene.itemViewFor(item)->setSelected(true);
    controller.cut();
    QCOMPARE(document->items().size(), 0);

    // The payload left RAM for the cache, and undo still restores it.
    QCOMPARE(item->source->residentBytes(), qint64(0));
    QVERIFY(item->source->isValid());
    const auto spilled = cache->get(QStringLiteral("undo"), uuid);
    QVERIFY(spilled.has_value());
    QCOMPARE(*spilled, png);

    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(document->items().size(), 1);
    auto blob = document->blob(*document->items().first());
    QVERIFY(blob.isOk());
    QCOMPARE(blob.value(), png);
}

QTEST_MAIN(TestInput)

#include "test_input.moc"
