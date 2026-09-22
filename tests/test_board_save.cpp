#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
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
    return QDir(dir).entryList({QStringLiteral(".beex-*.tmp")}, QDir::Files);
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

QTEST_GUILESS_MAIN(TestBoardSave)

#include "test_board_save.moc"
