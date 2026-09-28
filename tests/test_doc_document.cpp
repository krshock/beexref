#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <thread>

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#endif

#include "board/board.h"
#include "board/write.h"
#include "doc/document.h"
#include "doc/item.h"
#include "doc/source.h"
#include "doc/undo.h"
#include "settings.h"
#include "test_env.h"

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

// Counts how often its bytes are read, to prove an update writes only
// what changed.
class CountingSource final : public doc::Source
{
public:
    explicit CountingSource(QByteArray data)
        : data_(std::move(data))
    {
    }

    bool isValid() const override { return true; }
    QByteArray bytes() const override
    {
        ++reads;
        return data_;
    }
    qint64 residentBytes() const override { return data_.size(); }

    mutable int reads = 0;

private:
    QByteArray data_;
};

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

// The file's inode: an in-place update keeps it, an atomic save (temp +
// rename) replaces it. Zero when the file is gone.
#if defined(Q_OS_UNIX)
quint64 inodeOf(const QString &path)
{
    struct stat info;
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? quint64(info.st_ino) : 0;
}
#endif

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
    // The settings/cache/log paths are pointed at a throwaway directory,
    // so a test can never read or write the real configuration. The
    // scratch dir is shared by the suite, so a test that changes a
    // setting puts it back (see updatesTheFileInPlaceWhenPossible).
    void initTestCase() { testenv::isolate(); }
    void cleanup() { testenv::isolate(); }
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
    void refusesInPlaceSaveWhenTheFileChanged();
    void updatesTheFileInPlaceWhenPossible();
    void incrementalSaveCarriesAddsAndDeletes();
    void incrementalAndFullSavesAgree();
    void deleteUndoSaveNeverReusesIds();
    void incrementalUpdateKeepsPlaceholders();
    void incrementalUpdateWritesOnlyWhatChanged();
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

void TestDocument::refusesInPlaceSaveWhenTheFileChanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("changed.beex"));

    doc::Document document = doc::Document::create();
    document.addItem(pixmapItem(makePng(300, 200, Qt::red)));
    QVERIFY(document.save(path).isOk());
    // The app adopts the file after a save (setPath + adoptFileSources in
    // MainWindow), which is what gives the document a file identity to
    // compare against.
    document.setPath(path);
    document.adoptFileSources();
    QVERIFY(!document.hasChangedOnDisk());

    // Another connection commits to the file behind the document.
    QVERIFY(execOnBoard(path, QStringLiteral("UPDATE items SET x = x + 5")));
    QVERIFY(document.hasChangedOnDisk());

    // An in-place save would clobber that; a copy still works.
    const auto status = document.save(path);
    QVERIFY(!status.isOk());
    QVERIFY2(status.error().message.contains(QStringLiteral("changed on disk")),
             qPrintable(status.error().toString()));

    const QString copy = dir.filePath(QStringLiteral("copy.beex"));
    QVERIFY(document.save(copy, true, {}, true).isOk());
    auto reopened = doc::Document::open(copy, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().items().size(), 1);
}

void TestDocument::updatesTheFileInPlaceWhenPossible()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    doc::Document document = doc::Document::create();
    const doc::ItemPtr item = pixmapItem(makePng(300, 200, Qt::red));
    document.addItem(item);
    QVERIFY(document.save(path).isOk()); // first save: no path yet, so full
    document.setPath(path);
    document.adoptFileSources();

#if defined(Q_OS_UNIX)
    const quint64 firstInode = inodeOf(path);
    QVERIFY(firstInode != 0);
#endif

    // A change is applied to the same file, not a replacement.
    item->x = 42;
    document.noteItemChanged(item);
    QVERIFY(document.save(path).isOk());
#if defined(Q_OS_UNIX)
    QCOMPARE(inodeOf(path), firstInode);
#endif

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().items().size(), 1);
    QCOMPARE(reopened.value().items().first()->x, 42.0);

    // The setting is the safety valve: off means a complete new file.
    document.setPath(path);
    document.adoptFileSources();
    {
        settings::File file(settings::iniPath());
        file.load();
        file.setValue(QStringLiteral("Save"), QStringLiteral("incremental"),
                      QStringLiteral("false"));
        QVERIFY(file.sync());
    }
    item->x = 43;
    document.noteItemChanged(item);
    QVERIFY(document.save(path).isOk());
#if defined(Q_OS_UNIX)
    QVERIFY(inodeOf(path) != firstInode);
#endif

    auto again = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(again.isOk());
    QCOMPARE(again.value().items().first()->x, 43.0);

    // The suite shares one scratch directory, so put the setting back for
    // the tests that follow.
    {
        settings::File file(settings::iniPath());
        file.load();
        file.setValue(QStringLiteral("Save"), QStringLiteral("incremental"),
                      QStringLiteral("true"));
        QVERIFY(file.sync());
    }
}

void TestDocument::incrementalSaveCarriesAddsAndDeletes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    doc::Document document = doc::Document::create();
    const doc::ItemPtr first = pixmapItem(makePng(300, 200, Qt::red));
    const doc::ItemPtr second = pixmapItem(makePng(120, 90, Qt::blue));
    document.addItem(first);
    document.addItem(second);
    QVERIFY(document.save(path).isOk());
    document.setPath(path);
    document.adoptFileSources();
    const qint64 firstId = first->id;
    const qint64 secondId = second->id;
    QVERIFY(firstId > 0);
    QVERIFY(secondId > 0);
    QVERIFY(firstId != secondId);

    // Move one, delete the other and add a third: one in-place update.
    first->x = 77;
    document.noteItemChanged(first);
    document.removeItem(second);
    const doc::ItemPtr third = pixmapItem(makePng(60, 40, Qt::green));
    document.addItem(third);
    QVERIFY(document.save(path).isOk());

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    const doc::Document &board = reopened.value();
    QCOMPARE(board.items().size(), 2);
    QVERIFY(board.itemById(firstId));
    QCOMPARE(board.itemById(firstId)->x, 77.0);
    QVERIFY(!board.itemById(secondId));
    QVERIFY(third->id > 0);
    QVERIFY(board.itemById(third->id));
    // The blobs are intact: the kept image and the new one.
    QCOMPARE(board.blob(*board.itemById(firstId)).value(), makePng(300, 200, Qt::red));
    QCOMPARE(board.blob(*board.itemById(third->id)).value(), makePng(60, 40, Qt::green));
}

void TestDocument::incrementalAndFullSavesAgree()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString incrementalPath = dir.filePath(QStringLiteral("incremental.beex"));
    const QString fullPath = dir.filePath(QStringLiteral("full.beex"));

    // Build the same scene twice -- one edited through an in-place update,
    // one through a complete rewrite -- and compare the files item by item
    // afterwards.
    const auto build = [](const QString &path, bool incremental) {
        doc::Document document = doc::Document::create();
        const doc::ItemPtr first = pixmapItem(makePng(300, 200, Qt::red));
        first->uuid = QStringLiteral("u1");
        const doc::ItemPtr second = pixmapItem(makePng(120, 90, Qt::blue));
        second->uuid = QStringLiteral("u2");
        document.addItem(first);
        document.addItem(second);
        if (!document.save(path).isOk())
            return false;
        document.setPath(path);
        document.adoptFileSources();

        // The same edits in both: move one, delete one, add one.
        first->x = 77;
        first->scale = 0.5;
        document.noteItemChanged(first);
        document.removeItem(second);
        const doc::ItemPtr third = pixmapItem(makePng(60, 40, Qt::green));
        third->uuid = QStringLiteral("u3");
        document.addItem(third);

        if (incremental)
            return document.save(path).isOk();

        // The full path, with the setting off for this one save.
        const auto setIncremental = [](const char *value) {
            settings::File file(settings::iniPath());
            file.load();
            file.setValue(QStringLiteral("Save"), QStringLiteral("incremental"),
                          QString::fromLatin1(value));
            return file.sync();
        };
        if (!setIncremental("false"))
            return false;
        const bool ok = document.save(path).isOk();
        if (!setIncremental("true"))
            return false;
        return ok;
    };
    QVERIFY(build(incrementalPath, true));
    QVERIFY(build(fullPath, false));

    // Compare keyed by uuid: ids may differ between the two files.
    const auto collect = [&dir](const QString &path) {
        QHash<QString, QPair<doc::ItemPtr, QByteArray>> out;
        auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
        if (!opened)
            return out;
        doc::Document document = opened.take();
        for (const doc::ItemPtr &item : document.items())
            out.insert(item->uuid, {item, document.blob(*item).value()});
        return out;
    };
    const auto incremental = collect(incrementalPath);
    const auto full = collect(fullPath);
    QCOMPARE(incremental.size(), 2);
    QCOMPARE(full.size(), incremental.size());

    for (auto it = incremental.cbegin(); it != incremental.cend(); ++it) {
        QVERIFY2(full.contains(it.key()), qPrintable(it.key()));
        const doc::ItemPtr &a = it.value().first;
        const doc::ItemPtr &b = full.value(it.key()).first;
        QCOMPARE(a->x, b->x);
        QCOMPARE(a->y, b->y);
        QCOMPARE(a->scale, b->scale);
        QCOMPARE(a->rotation, b->rotation);
        QCOMPARE(a->flip, b->flip);
        QCOMPARE(a->filename, b->filename);
        QCOMPARE(QJsonDocument(a->data).toJson(QJsonDocument::Compact),
                 QJsonDocument(b->data).toJson(QJsonDocument::Compact));
        QCOMPARE(QJsonDocument(a->meta).toJson(QJsonDocument::Compact),
                 QJsonDocument(b->meta).toJson(QJsonDocument::Compact));
        QCOMPARE(it.value().second, full.value(it.key()).second);
    }
}

void TestDocument::deleteUndoSaveNeverReusesIds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ids.beex"));

    doc::Document document = doc::Document::create();
    const doc::ItemPtr first = pixmapItem(makePng(300, 200, Qt::red));
    first->uuid = QStringLiteral("u1");
    const doc::ItemPtr second = pixmapItem(makePng(120, 90, Qt::blue));
    second->uuid = QStringLiteral("u2");
    document.addItem(first);
    document.addItem(second);
    QVERIFY(document.save(path).isOk());
    document.setPath(path);
    document.adoptFileSources();
    const qint64 secondId = second->id;
    QVERIFY(secondId > 0);

    // Delete the second item through the real command, save the delete,
    // then undo it: the restored item keeps its id, and the item added
    // afterwards must not be handed that id. The spill callback is what
    // the app uses to keep the bytes alive for undo (the session cache);
    // here it keeps them in RAM.
    doc::UndoStack stack(&document);
    stack.push(std::make_unique<doc::RemoveItemsCommand>(
        QVector<doc::ItemPtr>{second},
        [](const doc::ItemPtr &item) {
            item->source = std::make_shared<doc::BytesSource>(item->source->bytes());
        },
        QStringLiteral("Delete")));
    const auto afterDelete = document.save(path);
    QVERIFY2(afterDelete.isOk(), qPrintable(afterDelete.error().toString()));
    document.setPath(path);
    document.adoptFileSources();

    QVERIFY(stack.undo());
    const doc::ItemPtr third = pixmapItem(makePng(60, 40, Qt::green));
    third->uuid = QStringLiteral("u3");
    document.addItem(third);
    const auto afterUndo = document.save(path);
    QVERIFY2(afterUndo.isOk(), qPrintable(afterUndo.error().toString()));

    QVERIFY(third->id > 0);
    QVERIFY(third->id != secondId);
    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().items().size(), 3);
    const doc::ItemPtr restored = reopened.value().itemById(secondId);
    QVERIFY(restored);
    QCOMPARE(restored->uuid, QStringLiteral("u2"));
    QCOMPARE(reopened.value().blob(*restored).value(), makePng(120, 90, Qt::blue));
    QCOMPARE(reopened.value().blob(*reopened.value().itemById(third->id)).value(),
             makePng(60, 40, Qt::green));
}

void TestDocument::incrementalUpdateKeepsPlaceholders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("placeholder.beex"));

    // A recovered copy: one real image and one marked placeholder.
    QVector<board::Record> records;
    records << pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("a.png"));
    board::Record placeholder = pixmapRecord(2, QByteArray(), QStringLiteral("gone.png"));
    placeholder.placeholder = true;
    placeholder.dataJson = QStringLiteral("{\"filename\":\"gone.png\",\"placeholder\":true}");
    records << placeholder;
    QVERIFY(board::save(path, records).isOk());

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(opened.isOk());
    doc::Document document = opened.take();
    QVERIFY(!document.damaged());
    QCOMPARE(document.placeholderCount(), 1);

    // Move the real image and add another: one in-place update that must
    // carry the placeholder row along untouched.
    const doc::ItemPtr first = document.itemById(1);
    QVERIFY(first);
    first->x = 12;
    document.noteItemChanged(first);
    const doc::ItemPtr third = pixmapItem(makePng(60, 40, Qt::green));
    third->uuid = QStringLiteral("u3");
    document.addItem(third);
    QVERIFY(document.save(path).isOk());

    auto again = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(again.isOk());
    const doc::Document &board = again.value();
    QCOMPARE(board.items().size(), 3);
    QCOMPARE(board.placeholderCount(), 1);
    QCOMPARE(board.itemById(1)->x, 12.0);
    QVERIFY(board.itemById(2)); // the placeholder is still there
    QVERIFY(!board.itemById(2)->data.isEmpty());
    // Two real blobs: the kept image and the new one.
    QCOMPARE(board.board()->counts().value().value(QStringLiteral("sqlar")), qint64(2));
}

void TestDocument::incrementalUpdateWritesOnlyWhatChanged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("reads.beex"));

    const QByteArray red = makePng(300, 200, Qt::red);
    QVERIFY(board::save(path, {pixmapRecord(1, red, QStringLiteral("a.png"))}).isOk());

    auto opened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(opened.isOk());
    doc::Document document = opened.take();
    const doc::ItemPtr existing = document.items().first();

    // The kept item's source counts its reads: an update must not touch
    // its blob at all.
    auto *existingSource = new CountingSource(red);
    existing->source = doc::SourcePtr(existingSource);

    // The new item's source is read once, when its blob is written.
    auto *newSource = new CountingSource(makePng(60, 40, Qt::green));
    auto added = std::make_shared<doc::Item>(doc::kTypePixmap);
    added->source = doc::SourcePtr(newSource);
    added->setOriginalSize(QSize(60, 40));
    added->format = QStringLiteral("png");
    added->uuid = QStringLiteral("u2");
    document.addItem(added);

    existing->x = 5;
    document.noteItemChanged(existing);
    QVERIFY(document.save(path).isOk());

    QCOMPARE(existingSource->reads, 0);
    QCOMPARE(newSource->reads, 1);

    auto reopened = doc::Document::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(reopened.isOk());
    QCOMPARE(reopened.value().items().size(), 2);
    QCOMPARE(reopened.value().itemById(1)->x, 5.0);
    QCOMPARE(reopened.value().blob(*reopened.value().itemById(1)).value(), red);
    QCOMPARE(reopened.value().blob(*reopened.value().itemById(added->id)).value(),
             makePng(60, 40, Qt::green));
}

QTEST_GUILESS_MAIN(TestDocument)

#include "test_doc_document.moc"
