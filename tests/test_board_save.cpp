#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include "board/board.h"
#include "board/schema.h"
#include "board/sqlite.h"
#include "board/write.h"

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

QStringList tempFiles(const QString &dir)
{
    // The temp names start with a dot, so hidden entries must be listed.
    return QDir(dir).entryList({QStringLiteral(".beex-*.tmp")}, QDir::Files | QDir::Hidden);
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
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

} // namespace

class TestBoardSave : public QObject
{
    Q_OBJECT

private slots:
    void savesAndReloads();
    void streamsPixmapSourceOnce();
    void preservesSaveIdsAndNames();
    void reusesFloorDataVerbatim();
    void skipsThumbnailsForSmallImages();
    void thumbnailsCanBeDisabled();
    void replacesExistingFile();
    void failsWhenDirectoryIsMissing();
    void storesUndecodablePixmapWithoutThumbnail();
    void exportsLegacyBeeShape();
    void failsWhenImageBytesAreMissing();
    void verificationCatchesAnIncompleteFile();
    void keepsTheTempWhenRenameFails();
};

void TestBoardSave::savesAndReloads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    const QByteArray png = makePng(300, 200, Qt::red);
    QVector<board::Record> records;
    records << pixmapRecord(1, png, QStringLiteral("a.png"));

    board::Record text;
    text.saveId = 2;
    text.type = QStringLiteral("text");
    text.dataJson = QStringLiteral("{\"text\":\"note\"}");
    records << text;

    QVector<QPair<int, int>> progress;
    const auto status = board::save(path, records, true, [&progress](int done, int total) {
        progress.append({done, total});
    });
    QVERIFY(status.isOk());
    QVERIFY(QFile::exists(path));
    QVERIFY(tempFiles(dir.path()).isEmpty());
    QCOMPARE(progress.size(), 3);
    QCOMPARE(progress.first(), qMakePair(0, 2));
    QCOMPARE(progress.last(), qMakePair(2, 2));

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QVERIFY(board.value().tempPath().isEmpty());
    QCOMPARE(board::schema::readUserVersion(board.value().connection()).value(),
             board::schema::kUserVersion);
    QCOMPARE(board::schema::readApplicationId(board.value().connection()).value(),
             board::schema::kApplicationId);

    auto items = board.value().items();
    QVERIFY(items.isOk());
    QCOMPARE(items.value().size(), 2);
    QCOMPARE(items.value().at(0).id, qint64(1));
    QCOMPARE(items.value().at(0).type, QStringLiteral("pixmap"));
    QCOMPARE(items.value().at(0).uuid, QStringLiteral("uuid-1"));
    QCOMPARE(items.value().at(0).x, 0.0);
    QCOMPARE(items.value().at(0).flip, qint64(1));
    QCOMPARE(items.value().at(1).id, qint64(2));
    QCOMPARE(items.value().at(1).type, QStringLiteral("text"));

    auto blob = board.value().blob(1);
    QVERIFY(blob.isOk());
    QCOMPARE(blob.value(), png);
    QVERIFY(!board.value().blob(2).isOk());

    auto sizes = board.value().originalSizes();
    QVERIFY(sizes.isOk());
    QCOMPARE(sizes.value().value(1), QSize(300, 200));

    auto floors = board.value().floorLevels();
    QVERIFY(floors.isOk());
    QVERIFY(floors.value().contains(1));
    const board::FloorLevel &floor = floors.value().value(1);
    QCOMPARE(floor.fraction, 64.0 / 300.0);
    QVERIFY(floor.format == QStringLiteral("png") || floor.format == QStringLiteral("webp"));
    const QImage thumbnail = QImage::fromData(floor.data);
    QVERIFY(!thumbnail.isNull());
    QCOMPARE(std::max(thumbnail.width(), thumbnail.height()), 64);
}

void TestBoardSave::streamsPixmapSourceOnce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));
    const QByteArray png = makePng(300, 200, Qt::green);

    int calls = 0;
    board::Record record = pixmapRecord(1, QByteArray(), QStringLiteral("b.png"));
    record.pixmapSource = [&calls, &png]() {
        ++calls;
        return png;
    };

    QVERIFY(board::save(path, {record}).isOk());
    QCOMPARE(calls, 1);

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QCOMPARE(board.value().blob(1).value(), png);
}

void TestBoardSave::preservesSaveIdsAndNames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    QVector<board::Record> records;
    records << pixmapRecord(5, makePng(160, 160, Qt::blue), QStringLiteral("a.png"));
    records << pixmapRecord(3, makePng(160, 160, Qt::yellow), QString());
    records << pixmapRecord(0, makePng(160, 160, Qt::cyan), QString());
    QVERIFY(board::save(path, records, false).isOk());

    auto db = board::Connection::open(path, board::Connection::OpenMode::ReadOnly);
    QVERIFY(db.isOk());

    auto ids = db.value().prepare(QStringLiteral("SELECT id FROM items ORDER BY id"));
    QVERIFY(ids.isOk());
    QVector<qint64> readIds;
    while (true) {
        auto row = ids.value().step();
        QVERIFY(row.isOk());
        if (!row.value())
            break;
        readIds.append(ids.value().columnInt64(0));
    }
    QCOMPARE(readIds, QVector<qint64>({3, 5, 6}));

    auto names = db.value().prepare(
        QStringLiteral("SELECT item_id, name FROM sqlar ORDER BY item_id"));
    QVERIFY(names.isOk());
    QStringList readNames;
    while (true) {
        auto row = names.value().step();
        QVERIFY(row.isOk());
        if (!row.value())
            break;
        readNames.append(QStringLiteral("%1:%2")
                             .arg(names.value().columnInt64(0))
                             .arg(names.value().columnText(1)));
    }
    QCOMPARE(readNames,
             QStringList({QStringLiteral("3:0003.png"), QStringLiteral("5:0005-a.png"),
                          QStringLiteral("6:0006.png")}));
}

void TestBoardSave::reusesFloorDataVerbatim()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    const QByteArray floor = makePng(64, 64, Qt::magenta);
    board::Record record = pixmapRecord(1, makePng(640, 480, Qt::red), QStringLiteral("c.png"));
    record.floorData = floor;
    record.floorFraction = 0.1;
    record.floorFormat = QStringLiteral("png");
    record.origW = 640;
    record.origH = 480;

    QVERIFY(board::save(path, {record}).isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    auto floors = board.value().floorLevels();
    QVERIFY(floors.isOk());
    QVERIFY(floors.value().contains(1));
    QCOMPARE(floors.value().value(1).data, floor);
    QCOMPARE(floors.value().value(1).fraction, 0.1);
    QCOMPARE(floors.value().value(1).format, QStringLiteral("png"));
}

void TestBoardSave::skipsThumbnailsForSmallImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    QVERIFY(board::save(path, {pixmapRecord(1, makePng(50, 40, Qt::red), QStringLiteral("s.png"))})
                .isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QVERIFY(board.value().floorLevels().value().isEmpty());
    QCOMPARE(board.value().counts().value().value(QStringLiteral("lod")), qint64(0));
}

void TestBoardSave::thumbnailsCanBeDisabled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    QVERIFY(board::save(path, {pixmapRecord(1, makePng(300, 200, Qt::red), QStringLiteral("d.png"))},
                        false)
                .isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QVERIFY(board.value().floorLevels().value().isEmpty());
    QCOMPARE(board.value().counts().value().value(QStringLiteral("lod")), qint64(0));
}

void TestBoardSave::replacesExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    board::Record first;
    first.type = QStringLiteral("text");
    first.dataJson = QStringLiteral("{\"text\":\"first\"}");
    QVERIFY(board::save(path, {first}, false).isOk());

    board::Record second;
    second.type = QStringLiteral("text");
    second.dataJson = QStringLiteral("{\"text\":\"second\"}");
    QVERIFY(board::save(path, {second}, false).isOk());
    QVERIFY(tempFiles(dir.path()).isEmpty());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    auto items = board.value().items();
    QVERIFY(items.isOk());
    QCOMPARE(items.value().size(), 1);
    QCOMPARE(items.value().first().data, QStringLiteral("{\"text\":\"second\"}"));
}

void TestBoardSave::failsWhenDirectoryIsMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("missing/board.beex"));

    board::Record record;
    record.type = QStringLiteral("text");
    const auto status = board::save(path, {record}, false);
    QVERIFY(!status.isOk());
    QVERIFY(status.error().message.contains(QStringLiteral("Directory does not exist")));
    QVERIFY(!QFile::exists(path));
}

void TestBoardSave::storesUndecodablePixmapWithoutThumbnail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    const QByteArray notAnImage("definitely not an image");
    board::Record record = pixmapRecord(1, notAnImage, QStringLiteral("broken.png"));
    QVERIFY(board::save(path, {record}).isOk());

    auto board = board::Board::open(path, dir.filePath(QStringLiteral("cache")));
    QVERIFY(board.isOk());
    QCOMPARE(board.value().blob(1).value(), notAnImage);
    QVERIFY(board.value().floorLevels().value().isEmpty());
}

void TestBoardSave::exportsLegacyBeeShape()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.bee"));

    const QByteArray png = makePng(300, 200, Qt::red);
    QVector<board::Record> records;
    records << pixmapRecord(1, png, QStringLiteral("a.png"));
    records << pixmapRecord(2, makePng(120, 90, Qt::blue), QStringLiteral("b.png"));

    QVERIFY(board::save(path, records, true, {}, nullptr, board::Format::Bee).isOk());
    QVERIFY(QFile::exists(path));
    QVERIFY(tempFiles(dir.path()).isEmpty());

    auto db = board::Connection::open(path, board::Connection::OpenMode::ReadOnly);
    QVERIFY(db);
    board::Connection &connection = db.value();

    QCOMPARE(board::schema::readUserVersion(connection).value(), board::schema::kBeeUserVersion);
    QCOMPARE(board::schema::readApplicationId(connection).value(),
             board::schema::kBeeApplicationId);

    // The legacy items table has no meta or uuid columns.
    auto columns = connection.prepare(QStringLiteral("PRAGMA table_info(items)"));
    QVERIFY(columns);
    QStringList names;
    while (true) {
        auto row = columns.value().step();
        QVERIFY(row);
        if (!row.value())
            break;
        names << columns.value().columnText(1);
    }
    QVERIFY(names.contains(QStringLiteral("data")));
    QVERIFY(!names.contains(QStringLiteral("meta")));
    QVERIFY(!names.contains(QStringLiteral("uuid")));

    // No lod table in the legacy format.
    auto lod = connection.prepare(QStringLiteral(
        "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='lod'"));
    QVERIFY(lod);
    auto lodRow = lod.value().step();
    QVERIFY(lodRow);
    QVERIFY(lodRow.value());
    QCOMPARE(lod.value().columnInt64(0), qint64(0));

    // Both pixmaps made it into sqlar.
    auto blobs = connection.prepare(QStringLiteral("SELECT count(*) FROM sqlar"));
    QVERIFY(blobs);
    auto blobRow = blobs.value().step();
    QVERIFY(blobRow);
    QVERIFY(blobRow.value());
    QCOMPARE(blobs.value().columnInt64(0), qint64(2));
}

void TestBoardSave::failsWhenImageBytesAreMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    // A good save first, so the target has content that must survive the
    // failed one below.
    QVERIFY(board::save(path, {pixmapRecord(1, makePng(80, 60, Qt::red), QStringLiteral("a.png"))})
                .isOk());
    const QByteArray before = fileBytes(path);
    QVERIFY(!before.isEmpty());

    // A pixmap whose source cannot produce bytes must fail the whole
    // save: writing the row without the image would silently lose it.
    board::Record broken = pixmapRecord(1, QByteArray(), QStringLiteral("gone.png"));
    broken.pixmapSource = []() { return QByteArray(); };
    QVector<board::Record> records;
    records << pixmapRecord(2, makePng(40, 30, Qt::blue), QStringLiteral("b.png")) << broken;

    const auto status = board::save(path, records);
    QVERIFY(!status.isOk());
    QVERIFY2(status.error().message.contains(QStringLiteral("could not be read")),
             qPrintable(status.error().toString()));
    QVERIFY2(status.error().message.contains(QStringLiteral("nothing was written")),
             qPrintable(status.error().toString()));

    // The target is byte-identical and no temp file is left behind.
    QCOMPARE(fileBytes(path), before);
    QVERIFY(tempFiles(dir.path()).isEmpty());
}

void TestBoardSave::verificationCatchesAnIncompleteFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("half.beex"));

    // A file with the right schema but no rows at all, checked against
    // two records: the verification save() runs before the rename must
    // reject it.
    auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
    QVERIFY(db.isOk());
    QVERIFY(board::schema::createTables(db.value()).isOk());
    QVERIFY(board::schema::writeHeader(db.value()).isOk());

    const QVector<board::Record> records = {
        pixmapRecord(1, makePng(40, 30, Qt::red), QStringLiteral("a.png")),
        pixmapRecord(2, makePng(40, 30, Qt::blue), QStringLiteral("b.png"))};
    const auto status = board::verifyWritten(db.value(), records, board::Format::Beex);
    QVERIFY(!status.isOk());
    QVERIFY2(status.error().message.contains(QStringLiteral("Verification failed")),
             qPrintable(status.error().toString()));

    // And the same records pass once they really are written.
    const QString good = dir.filePath(QStringLiteral("good.beex"));
    QVERIFY(board::save(good, records).isOk());
    auto goodDb = board::Connection::open(good, board::Connection::OpenMode::ReadOnly);
    QVERIFY(goodDb.isOk());
    QVERIFY(board::verifyWritten(goodDb.value(), records, board::Format::Beex).isOk());
}

void TestBoardSave::keepsTheTempWhenRenameFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // A directory as the target: the temp file is written and verified,
    // but the rename cannot replace a directory, and the complete new
    // file must be kept rather than deleted.
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("target")));
    const QString path = dir.filePath(QStringLiteral("target"));

    const auto status =
        board::save(path, {pixmapRecord(1, makePng(60, 40, Qt::green), QStringLiteral("g.png"))});
    QVERIFY(!status.isOk());
    QVERIFY2(status.error().message.contains(QStringLiteral("new version is kept")),
             qPrintable(status.error().toString()));

    const QStringList kept = tempFiles(dir.path());
    QCOMPARE(kept.size(), 1);
    QVERIFY(QFileInfo(dir.filePath(kept.first())).size() > 0);
    QVERIFY(QFile::remove(dir.filePath(kept.first())));
}

QTEST_GUILESS_MAIN(TestBoardSave)

#include "test_board_save.moc"
