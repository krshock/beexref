#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <thread>

#include "board/board.h"
#include "board/write.h"
#include "doc/document.h"
#include "doc/item.h"
#include "doc/source.h"

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

board::Record pixmapRecord(qint64 saveId, const QByteArray &png, const QString &filename)
{
    board::Record record;
    record.saveId = saveId;
    record.type = QStringLiteral("pixmap");
    record.dataJson = QStringLiteral("{\"filename\":\"%1\"}").arg(filename);
    record.metaJson = QStringLiteral("{}");
    record.uuid = QStringLiteral("uuid-%1").arg(saveId);
    record.pixmap = png;
    record.format = QStringLiteral("png");
    record.filename = filename;
    return record;
}

// Runs one statement on a board file, for crafting damage on disk.
bool execOnBoard(const QString &path, const QString &sql)
{
    auto db = board::Connection::open(path, board::Connection::OpenMode::ReadWrite);
    if (!db.isOk())
        return false;
    return db.value().exec(sql).isOk();
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

doc::ItemPtr pixmapItem(const QByteArray &png)
{
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->data.insert(QStringLiteral("filename"), QStringLiteral("x.png"));
    item->meta.insert(QStringLiteral("author"), QStringLiteral("me"));
    item->source = std::make_shared<doc::BytesSource>(png);
    item->format = QStringLiteral("png");
    item->filename = QStringLiteral("x.png");
    item->setOriginalSize(QSize(300, 200));
    return item;
}

} // namespace

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    void savesAndReopensEveryField();
    void reopenReadsTheBlobFormat();
    void reusesSavedFloors();
    void reportsProgress();
    void textItemRoundTrip();
    void sourceStopsAfterClose();
    void sourceIsReadableWhileItemsChange();
    void openMissingFileFails();
    void unsavedItemsGetIdsOnSave();
    void saveWritesTheDataDefaults();
    void saveAdoptsTheFileAsSource();
    void reportsItemDamageWithoutRefusingToOpen();
    void reportsOrphanedFloors();
    void activeDamageCountFollowsDeletedItems();
    void refusesInPlaceSaveForDamagedBoards();
    void savesRecoveredCopiesWithMarkedPlaceholders();
    void opensNewerVersionBoardsAsRecovered();
    void recoversImagesWhenTheItemRowsAreGone();
    void tracksSaveChanges();
    void changesFollowTheSavedFile();
};

void TestDocument::savesAndReopensEveryField()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.beex"));

    auto document = doc::Document::create();
    const QByteArray png = makePng(300, 200, Qt::red);
    doc::ItemPtr pixmap = pixmapItem(png);
    pixmap->uuid = doc::newUuid();
    pixmap->scale = 0.75;
    pixmap->x = 12;
    pixmap->y = 34;
    pixmap->rotation = 90;
    pixmap->flip = -1;
    document.addItem(pixmap);

    QVERIFY(document.save(path).isOk());

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().items().size(), 1);
    const doc::ItemPtr item = reopened.value().items().first();
    QCOMPARE(item->type, QString::fromLatin1(doc::kTypePixmap));
    QCOMPARE(item->scale, 0.75);
    QCOMPARE(item->x, 12.0);
    QCOMPARE(item->y, 34.0);
    QCOMPARE(item->rotation, 90.0);
    QCOMPARE(item->flip, -1.0);
    QCOMPARE(item->uuid, pixmap->uuid);
    QCOMPARE(item->originalSize(), QSize(300, 200));
    QCOMPARE(item->meta.value(QStringLiteral("author")).toString(), QStringLiteral("me"));
    QCOMPARE(item->data.value(QStringLiteral("filename")).toString(), QStringLiteral("x.png"));
    QVERIFY(item->floorFraction > 0);
    QVERIFY(!item->floorData.isEmpty());
    QVERIFY(item->hasSource());

    auto blob = reopened.value().blob(*item);
    QVERIFY(blob.isOk());
    QCOMPARE(blob.value(), png);

    // The item field is populated from the saved data, so a further save
    // does not overwrite the filename with an empty string.
    QCOMPARE(item->filename, QStringLiteral("x.png"));
    QVERIFY(reopened.value().save(path).isOk());
    auto again = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(again.isOk());
    QCOMPARE(again.value().items().first()->data.value(QStringLiteral("filename")).toString(),
             QStringLiteral("x.png"));
}

void TestDocument::reopenReadsTheBlobFormat()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.beex"));

    QImage image(8, 6, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "JPEG"));

    auto document = doc::Document::create();
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->filename = QStringLiteral("photo.jpg");
    item->data.insert(QStringLiteral("filename"), QStringLiteral("photo.jpg"));
    item->format = QStringLiteral("jpg");
    item->source = std::make_shared<doc::BytesSource>(jpeg);
    item->setOriginalSize(QSize(8, 6));
    document.addItem(item);
    QVERIFY(document.save(path).isOk());

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().items().size(), 1);
    // The format comes from the sqlar name, so a JPEG original is not
    // mistaken for its saved (PNG) floor.
    QCOMPARE(reopened.value().items().first()->format, QStringLiteral("jpg"));
}

void TestDocument::reusesSavedFloors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath(QStringLiteral("src.beex"));
    const QString out = dir.filePath(QStringLiteral("out.beex"));
    const QString cacheDir = dir.filePath(QStringLiteral("cache"));

    auto document = doc::Document::create();
    document.addItem(pixmapItem(makePng(400, 300, Qt::blue)));
    QVERIFY(document.save(src).isOk());
    document.close();

    auto reopened = doc::Document::open(src, cacheDir);
    QVERIFY(reopened.isOk());
    QVERIFY(!reopened.value().items().first()->floorData.isEmpty());
    QVERIFY(reopened.value().save(out).isOk());

    auto source = board::Board::open(src, cacheDir);
    QVERIFY(source.isOk());
    auto written = board::Board::open(out, cacheDir);
    QVERIFY(written.isOk());
    const auto expected = source.value().floorLevels();
    const auto actual = written.value().floorLevels();
    QCOMPARE(actual.value().size(), expected.value().size());
    for (auto it = expected.value().cbegin(); it != expected.value().cend(); ++it) {
        QVERIFY(actual.value().contains(it.key()));
        const board::FloorLevel &want = it.value();
        const board::FloorLevel &got = actual.value().value(it.key());
        QCOMPARE(got.fraction, want.fraction);
        QCOMPARE(got.format, want.format);
        QCOMPARE(got.data, want.data);
    }
}

void TestDocument::reportsProgress()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("progress.beex"));

    auto document = doc::Document::create();
    for (int i = 0; i < 3; ++i)
        document.addItem(pixmapItem(makePng(200, 150, Qt::green)));

    QVector<QPair<int, int>> saveProgress;
    QVERIFY(document
                .save(path, true,
                      [&saveProgress](int done, int total) { saveProgress.append({done, total}); })
                .isOk());
    QVERIFY(!saveProgress.isEmpty());
    QCOMPARE(saveProgress.last(), qMakePair(3, 3));

    QVector<QPair<int, int>> openProgress;
    auto reopened = doc::Document::open(
        path, dir.filePath(QStringLiteral("cache")),
        [&openProgress](int done, int total) { openProgress.append({done, total}); });
    QVERIFY(reopened.isOk());
    QVERIFY(!openProgress.isEmpty());
    QCOMPARE(openProgress.last(), qMakePair(3, 3));
}

void TestDocument::textItemRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("text.beex"));

    auto document = doc::Document::create();
    auto text = std::make_shared<doc::Item>(doc::kTypeText);
    text->x = 5;
    text->y = 6;
    text->setText(QStringLiteral("hello"));
    document.addItem(text);
    QVERIFY(document.save(path, false).isOk());

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    const doc::ItemPtr item = reopened.value().items().first();
    QVERIFY(item->isText());
    QCOMPARE(item->text(), QStringLiteral("hello"));
    QCOMPARE(item->x, 5.0);
    QVERIFY(!item->hasSource());
}

void TestDocument::sourceStopsAfterClose()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("close.beex"));

    auto document = doc::Document::create();
    document.addItem(pixmapItem(makePng(200, 150, Qt::yellow)));
    QVERIFY(document.save(path).isOk());
    document.close();

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    const doc::ItemPtr item = reopened.value().items().first();
    const doc::SourcePtr source = item->source;
    QVERIFY(!source->bytes().isEmpty());

    // The source keeps the board alive; closing it must not leave a
    // dangling read behind.
    reopened.value().close();
    QVERIFY(source->bytes().isEmpty());
}

void TestDocument::sourceIsReadableWhileItemsChange()
{
    const QByteArray png = makePng(200, 150, Qt::magenta);
    auto item = pixmapItem(png);
    const doc::SourcePtr source = item->source;

    std::atomic<int> mismatches{0};
    const auto worker = [&source, &png, &mismatches]() {
        for (int i = 0; i < 200; ++i) {
            if (source->bytes() != png)
                ++mismatches;
        }
    };

    std::thread workerThread(worker);
    for (int i = 0; i < 200; ++i) {
        item->x = i;
        item->scale = 1.0 + i;
        item->setOpacity(0.5);
        item->meta.insert(QStringLiteral("n"), i);
    }
    workerThread.join();
    QCOMPARE(mismatches.load(), 0);
}

void TestDocument::openMissingFileFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto document = doc::Document::open(dir.filePath(QStringLiteral("missing.beex")),
                                         dir.filePath(QStringLiteral("cache")));
    QVERIFY(!document.isOk());
    QVERIFY(!document.error().message.isEmpty());
}

void TestDocument::unsavedItemsGetIdsOnSave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ids.beex"));

    auto document = doc::Document::create();
    const doc::ItemPtr first = pixmapItem(makePng(200, 150, Qt::red));
    const doc::ItemPtr second = pixmapItem(makePng(200, 150, Qt::blue));
    QCOMPARE(first->id, qint64(0));
    document.addItem(first);
    document.addItem(second);
    QVERIFY(document.save(path).isOk());

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().items().at(0)->id, qint64(1));
    QCOMPARE(reopened.value().items().at(1)->id, qint64(2));

    // Saving again keeps the ids.
    QVERIFY(reopened.value().save(path).isOk());
    auto again = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(again.isOk());
    QCOMPARE(again.value().items().at(0)->id, qint64(1));
    QCOMPARE(again.value().items().at(1)->id, qint64(2));
}

void TestDocument::saveWritesTheDataDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("defaults.beex"));

    auto document = doc::Document::create();
    const doc::ItemPtr cropped = pixmapItem(makePng(300, 200, Qt::green));
    cropped->setOpacity(0.4);
    cropped->setGrayscale(true);
    cropped->setCrop(QRectF(5, 6, 100, 80));
    cropped->filename = QStringLiteral("photo.jpg");
    document.addItem(cropped);

    // An image that never had these set still gets the default keys.
    const doc::ItemPtr plain = pixmapItem(makePng(120, 90, Qt::blue));
    document.addItem(plain);

    QVERIFY(document.save(path).isOk());

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    const doc::ItemPtr saved = reopened.value().items().at(0);
    QCOMPARE(saved->data.value(QStringLiteral("filename")).toString(),
             QStringLiteral("photo.jpg"));
    QCOMPARE(saved->opacity(), 0.4);
    QCOMPARE(saved->grayscale(), true);
    QCOMPARE(saved->crop(), QRectF(5, 6, 100, 80));

    const doc::ItemPtr defaults = reopened.value().items().at(1);
    QCOMPARE(defaults->data.value(QStringLiteral("filename")).toString(),
             QStringLiteral("x.png"));
    QCOMPARE(defaults->opacity(), 1.0);
    QCOMPARE(defaults->grayscale(), false);
    QCOMPARE(defaults->crop(), QRectF(0, 0, 300, 200));
}

void TestDocument::saveAdoptsTheFileAsSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("adopt.beex"));
    const QByteArray png = makePng(120, 90, Qt::blue);

    auto document = doc::Document::create();
    const doc::ItemPtr item = pixmapItem(png);
    document.addItem(item);
    QVERIFY(dynamic_cast<const doc::BytesSource *>(item->source.get()) != nullptr);

    QVERIFY(document.save(path).isOk());
    // Writing assigned the row id but kept the in-RAM bytes.
    QVERIFY(item->id > 0);
    QVERIFY(dynamic_cast<const doc::BytesSource *>(item->source.get()) != nullptr);

    document.setPath(path);
    document.adoptFileSources();
    QVERIFY(dynamic_cast<const doc::BoardSource *>(item->source.get()) != nullptr);
    // The board now serves the very same bytes.
    QCOMPARE(item->source->bytes(), png);
}

void TestDocument::reportsItemDamageWithoutRefusingToOpen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("damaged.beex"));

    QVector<board::Record> records;
    records << pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("a.png"));
    records << pixmapRecord(2, makePng(300, 200, Qt::blue), QStringLiteral("b.png"));
    records << pixmapRecord(3, makePng(300, 200, Qt::green), QStringLiteral("c.png"));
    QVERIFY(board::save(path, records).isOk());

    // Break two things on disk: a missing blob and non-JSON metadata.
    QVERIFY(execOnBoard(path, QStringLiteral("DELETE FROM sqlar WHERE item_id=1")));
    QVERIFY(execOnBoard(path, QStringLiteral("UPDATE items SET data='not json' WHERE id=2")));

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(opened.isOk());
    const doc::Document &document = opened.value();

    // The scene still loads, and the damage is reported per item.
    QCOMPARE(document.items().size(), 3);
    QStringList kinds;
    QStringList details;
    for (const doc::Damage &entry : document.damage()) {
        kinds << doc::damageLabel(entry.kind);
        details << entry.detail;
    }
    QVERIFY2(kinds.contains(QStringLiteral("missing image data")), qPrintable(details.join("; ")));
    QVERIFY2(kinds.contains(QStringLiteral("invalid metadata")), qPrintable(details.join("; ")));
    QVERIFY2(details.contains(QStringLiteral("item 1: no image data in the file")),
             qPrintable(details.join("; ")));
    QVERIFY(document.activeDamageCount() >= 2);
}

void TestDocument::reportsOrphanedFloors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("orphan.beex"));

    QVector<board::Record> records;
    records << pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("a.png"));
    records << pixmapRecord(2, makePng(300, 200, Qt::blue), QStringLiteral("b.png"));
    QVERIFY(board::save(path, records).isOk());

    // The item row goes, its floor stays behind.
    QVERIFY(execOnBoard(path, QStringLiteral("DELETE FROM items WHERE id=2")));

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(opened.isOk());
    const doc::Document &document = opened.value();
    QCOMPARE(document.items().size(), 1);

    QStringList kinds;
    for (const doc::Damage &entry : document.damage())
        kinds << doc::damageLabel(entry.kind);
    QVERIFY2(kinds.contains(QStringLiteral("orphaned thumbnail")), qPrintable(kinds.join("; ")));
    // Board-level damage counts, and has no item to delete.
    QCOMPARE(document.activeDamageCount(), 1);
}

void TestDocument::activeDamageCountFollowsDeletedItems()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("count.beex"));

    QVERIFY(board::save(path, {pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("a.png"))})
                .isOk());
    QVERIFY(execOnBoard(path, QStringLiteral("DELETE FROM sqlar WHERE item_id=1")));

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(opened.isOk());
    doc::Document document = opened.take();
    const int before = document.activeDamageCount();
    QVERIFY(before >= 1);

    // Deleting the broken item drops its damage, so the badge count the
    // status bar shows goes down with it.
    const doc::ItemPtr broken = document.itemById(1);
    QVERIFY(broken);
    document.removeItem(broken);
    QCOMPARE(document.activeDamageCount(), before - 1);
}

void TestDocument::refusesInPlaceSaveForDamagedBoards()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("damaged.beex"));

    QVERIFY(board::save(path, {pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("a.png"))})
                .isOk());
    QVERIFY(execOnBoard(path, QStringLiteral("DELETE FROM sqlar WHERE item_id=1")));

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(opened.isOk());
    doc::Document document = opened.take();
    QVERIFY(document.damaged());

    // The source file must not be written over: Save (in place) refuses
    // and the file keeps its bytes.
    const QByteArray before = fileBytes(path);
    const auto status = document.save(path);
    QVERIFY(!status.isOk());
    QVERIFY2(status.error().message.contains(QStringLiteral("recovered copy")),
             qPrintable(status.error().toString()));
    QCOMPARE(fileBytes(path), before);
}

void TestDocument::savesRecoveredCopiesWithMarkedPlaceholders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("damaged.beex"));
    const QString recovered = dir.filePath(QStringLiteral("recovered.beex"));

    QVector<board::Record> records;
    records << pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("a.png"));
    records << pixmapRecord(2, makePng(300, 200, Qt::blue), QStringLiteral("b.png"));
    QVERIFY(board::save(path, records).isOk());
    QVERIFY(execOnBoard(path, QStringLiteral("DELETE FROM sqlar WHERE item_id=1")));

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(opened.isOk());
    doc::Document document = opened.take();
    QVERIFY(document.damaged());
    QCOMPARE(document.placeholderCount(), 1);

    // Save As writes the imageless item as an explicit placeholder row
    // and the document matches the written file afterwards.
    QVERIFY(document.save(recovered, true, {}, true).isOk());
    QVERIFY(!document.damaged());
    QCOMPARE(document.placeholderCount(), 1);

    {
        auto db = board::Connection::open(recovered, board::Connection::OpenMode::ReadOnly);
        QVERIFY(db.isOk());
        auto data = db.value().prepare(QStringLiteral("SELECT data FROM items WHERE id=1"));
        QVERIFY(data);
        auto row = data.value().step();
        QVERIFY(row);
        QVERIFY(row.value());
        QVERIFY2(data.value().columnText(0).contains(QStringLiteral("placeholder")),
                 qPrintable(data.value().columnText(0)));

        // One blob for the two rows: the placeholder has no image.
        auto blobs = db.value().prepare(QStringLiteral("SELECT count(*) FROM sqlar"));
        QVERIFY(blobs);
        auto blobRow = blobs.value().step();
        QVERIFY(blobRow);
        QVERIFY(blobRow.value());
        QCOMPARE(blobs.value().columnInt64(0), qint64(1));
    }

    // Reopening the recovered copy is clean: the placeholder is known,
    // not damage, so the copy can be saved in place from now on.
    auto again = doc::Document::open(recovered, dir.filePath(QStringLiteral("cache")));
    QVERIFY(again.isOk());
    const doc::Document &reopened = again.value();
    QVERIFY(!reopened.damaged());
    QCOMPARE(reopened.placeholderCount(), 1);
    QCOMPARE(reopened.items().size(), 2);
}

void TestDocument::opensNewerVersionBoardsAsRecovered()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("future.beex"));

    QVERIFY(board::save(path, {pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("a.png"))})
                .isOk());
    QVERIFY(execOnBoard(path, QStringLiteral("PRAGMA user_version=6")));

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    if (!opened)
        QFAIL(qPrintable(opened.error().toString()));
    doc::Document document = opened.take();

    // The scene is not lost: it loads and is marked as newer.
    QCOMPARE(document.items().size(), 1);
    QVERIFY(document.damaged());
    QStringList kinds;
    for (const doc::Damage &entry : document.damage())
        kinds << doc::damageLabel(entry.kind);
    QVERIFY2(kinds.contains(QStringLiteral("newer board version")), qPrintable(kinds.join("; ")));

    // It is never downgraded in place; a recovered copy carries it into
    // the current format.
    const QByteArray before = fileBytes(path);
    QVERIFY(!document.save(path).isOk());
    QCOMPARE(fileBytes(path), before);

    const QString copy = dir.filePath(QStringLiteral("copy.beex"));
    QVERIFY(document.save(copy, true, {}, true).isOk());
    auto again = doc::Document::open(copy, dir.filePath(QStringLiteral("cache")));
    QVERIFY(again.isOk());
    QVERIFY(!again.value().damaged());
    QCOMPARE(again.value().items().size(), 1);
}

void TestDocument::recoversImagesWhenTheItemRowsAreGone()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("blobs-only.beex"));

    // A file whose item table is gone but whose blob store survived.
    const QByteArray png = makePng(300, 200, Qt::red);
    {
        auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
        QVERIFY(db.isOk());
        QVERIFY(db.value()
                    .exec(QStringLiteral("CREATE TABLE sqlar (name TEXT PRIMARY KEY, "
                                         "item_id INTEGER NOT NULL UNIQUE, mode INT, mtime INT, "
                                         "sz INT, data BLOB)"))
                    .isOk());
        auto insert = db.value().prepare(
            QStringLiteral("INSERT INTO sqlar (name, item_id, sz, data) VALUES (?, ?, ?, ?)"));
        QVERIFY(insert);
        QVERIFY(insert.value().bind(1, QStringLiteral("a.png")).isOk());
        QVERIFY(insert.value().bind(2, qint64(1)).isOk());
        QVERIFY(insert.value().bind(3, qint64(png.size())).isOk());
        QVERIFY(insert.value().bind(4, png).isOk());
        QVERIFY(insert.value().exec().isOk());
    }

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    if (!opened)
        QFAIL(qPrintable(opened.error().toString()));
    doc::Document document = opened.take();

    // The image is back as an item: filename from the blob store, no
    // position (that was in the lost item table).
    QCOMPARE(document.items().size(), 1);
    QVERIFY(document.items().first()->isPixmap());
    QCOMPARE(document.items().first()->filename, QStringLiteral("a.png"));
    QCOMPARE(document.blob(*document.items().first()).value(), png);
    QVERIFY(document.damaged());
    QStringList kinds;
    for (const doc::Damage &entry : document.damage())
        kinds << doc::damageLabel(entry.kind);
    QVERIFY2(kinds.contains(QStringLiteral("recovered image")), qPrintable(kinds.join("; ")));

    // Save As writes a normal board with the image intact.
    const QString copy = dir.filePath(QStringLiteral("recovered.beex"));
    QVERIFY(document.save(copy, true, {}, true).isOk());
    auto again = doc::Document::open(copy, dir.filePath(QStringLiteral("cache")));
    QVERIFY(again.isOk());
    QVERIFY(!again.value().damaged());
    QCOMPARE(again.value().items().size(), 1);
    QCOMPARE(again.value().blob(*again.value().items().first()).value(), png);
}

void TestDocument::tracksSaveChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("changes.beex"));

    doc::Document document = doc::Document::create();
    const doc::ItemPtr first = pixmapItem(makePng(300, 200, Qt::red));
    document.addItem(first);

    // Everything is new before the first save.
    QVERIFY(document.changes().added.contains(first.get()));
    QVERIFY(document.changes().removedIds.isEmpty());
    QVERIFY(!document.changes().isEmpty());

    QVERIFY(document.save(path).isOk());
    QVERIFY(document.changes().isEmpty());

    // A change to a saved item is an update, not an insert.
    first->scale = 0.5;
    document.noteItemChanged(first);
    QVERIFY(document.changes().changed.contains(first.get()));
    QVERIFY(document.changes().added.isEmpty());

    // A new item is an insert.
    const doc::ItemPtr second = pixmapItem(makePng(120, 90, Qt::blue));
    document.addItem(second);
    QVERIFY(document.changes().added.contains(second.get()));

    QVERIFY(document.save(path).isOk());
    QVERIFY(document.changes().isEmpty());

    // Removing a saved item is a pending delete; undoing the removal
    // takes the id back out.
    document.removeItem(second);
    QCOMPARE(document.changes().removedIds.size(), 1);
    QVERIFY(document.changes().removedIds.contains(second->id));
    document.insertItem(0, second);
    QVERIFY(document.changes().removedIds.isEmpty());

    // A save settles everything; an item added and removed before any
    // save never was a file change.
    QVERIFY(document.save(path).isOk());
    QVERIFY(document.changes().isEmpty());
    const doc::ItemPtr third = pixmapItem(makePng(60, 40, Qt::green));
    document.addItem(third);
    document.removeItem(third);
    QVERIFY(document.changes().isEmpty());
}

void TestDocument::changesFollowTheSavedFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("changes.beex"));

    doc::Document document = doc::Document::create();
    document.addItem(pixmapItem(makePng(300, 200, Qt::red)));
    QVERIFY(document.save(path).isOk());

    // A reopened document starts clean, and its rows are updates, not
    // inserts.
    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    doc::Document again = reopened.take();
    QVERIFY(again.changes().isEmpty());

    const doc::ItemPtr row = again.items().first();
    row->scale = 2.0;
    again.noteItemChanged(row);
    QVERIFY(again.changes().changed.contains(row.get()));
    QVERIFY(again.changes().added.isEmpty());

    // A row deleted after the save is a pending delete on reopen too.
    again.removeItem(row);
    QCOMPARE(again.changes().removedIds.size(), 1);
}

QTEST_GUILESS_MAIN(TestDocument)

#include "test_doc_document.moc"
