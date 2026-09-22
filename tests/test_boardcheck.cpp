#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "board/schema.h"
#include "board/sqlite.h"
#include "board/write.h"

namespace {

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

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

board::Record pixmapRecord(qint64 saveId, const QByteArray &png)
{
    board::Record record;
    record.saveId = saveId;
    record.type = QStringLiteral("pixmap");
    record.dataJson = QStringLiteral("{\"filename\":\"a.png\"}");
    record.metaJson = QStringLiteral("{}");
    record.uuid = QStringLiteral("uuid-%1").arg(saveId);
    record.pixmap = png;
    record.format = QStringLiteral("png");
    record.filename = QStringLiteral("a.png");
    return record;
}

struct Run
{
    int exitCode = -1;
    QString output;
};

Run runBoardcheck(const QStringList &arguments)
{
    QProcess process;
    process.start(QStringLiteral(BOARDCHECK_PATH), arguments);
    if (!process.waitForFinished(60000))
        return {-2, QStringLiteral("timeout")};
    Run run;
    run.exitCode = process.exitCode();
    run.output = QString::fromLocal8Bit(process.readAllStandardOutput())
        + QString::fromLocal8Bit(process.readAllStandardError());
    return run;
}

// Old native shape: items without meta/uuid, no lod table.
board::Status createLegacyBoard(const QString &path)
{
    auto db = board::Connection::open(path, board::Connection::OpenMode::Create);
    if (!db)
        return db.error();
    board::Connection &d = db.value();
    if (board::Status status = d.exec(QStringLiteral(
            "CREATE TABLE items (id INTEGER PRIMARY KEY, type TEXT NOT NULL, x REAL, y REAL, "
            "z REAL, scale REAL, rotation REAL, flip INTEGER, data JSON)"));
        !status)
        return status;
    if (board::Status status = d.exec(QStringLiteral(
            "CREATE TABLE sqlar (name TEXT PRIMARY KEY, item_id INTEGER NOT NULL UNIQUE, "
            "mode INT, mtime INT, sz INT, data BLOB)"));
        !status)
        return status;
    const QByteArray png = makePng(200, 100, Qt::red);
    {
        auto statement = d.prepare(
            QStringLiteral("INSERT INTO items (id, type, data) VALUES (1, 'pixmap', ?)"));
        if (!statement)
            return statement.error();
        if (board::Status status = statement.value().bind(1, QStringLiteral("{}")); !status)
            return status;
        if (board::Status status = statement.value().exec(); !status)
            return status;
    }
    {
        auto statement = d.prepare(QStringLiteral(
            "INSERT INTO sqlar (item_id, name, mode, sz, data) VALUES (1, '0001-a.png', 420, ?, ?)"));
        if (!statement)
            return statement.error();
        if (board::Status status = statement.value().bind(1, qint64(png.size())); !status)
            return status;
        if (board::Status status = statement.value().bind(2, png); !status)
            return status;
        if (board::Status status = statement.value().exec(); !status)
            return status;
    }
    if (board::Status status = d.exec(QStringLiteral("PRAGMA user_version=3")); !status)
        return status;
    return d.exec(QStringLiteral("PRAGMA application_id=2060242126"));
}

} // namespace

class TestBoardcheck : public QObject
{
    Q_OBJECT

private slots:
    void checksWrittenBoard();
    void checksLegacyBoardAndWriteBack();
    void failsOnCorruptBlob();
};

void TestBoardcheck::checksWrittenBoard()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board.beex"));

    board::Record text;
    text.saveId = 2;
    text.type = QStringLiteral("text");
    text.dataJson = QStringLiteral("{\"text\":\"note\"}");
    QVERIFY(board::save(path, {pixmapRecord(1, makePng(300, 200, Qt::blue)), text}).isOk());

    const Run run = runBoardcheck({path});
    QCOMPARE(run.exitCode, 0);
    QVERIFY2(run.output.contains(QStringLiteral("roundtrip=identical")), qPrintable(run.output));
    QVERIFY2(run.output.contains(QStringLiteral("dim_mismatch=0")), qPrintable(run.output));
    QVERIFY2(run.output.contains(QStringLiteral("board_failures=0")), qPrintable(run.output));
}

void TestBoardcheck::checksLegacyBoardAndWriteBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.bee"));
    QVERIFY(createLegacyBoard(path).isOk());

    const QByteArray hashBefore = fileHash(path);
    QVERIFY(!hashBefore.isEmpty());

    const QString writeBack = dir.filePath(QStringLiteral("written.beex"));
    const Run run = runBoardcheck({QStringLiteral("--write-back"), writeBack, path});
    QCOMPARE(run.exitCode, 0);
    QVERIFY2(run.output.contains(QStringLiteral("v3->v5")), qPrintable(run.output));
    QVERIFY2(run.output.contains(QStringLiteral("write_back=ok")), qPrintable(run.output));
    QVERIFY(QFile::exists(writeBack));

    // The gate never touches the file it checked.
    QCOMPARE(fileHash(path), hashBefore);
}

void TestBoardcheck::failsOnCorruptBlob()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("corrupt.beex"));

    board::Record record = pixmapRecord(1, QByteArray("not an image at all"));
    QVERIFY(board::save(path, {record}, false).isOk());

    const Run run = runBoardcheck({path});
    QCOMPARE(run.exitCode, 1);
    QVERIFY2(run.output.contains(QStringLiteral("decode_fail=1")), qPrintable(run.output));
}

QTEST_GUILESS_MAIN(TestBoardcheck)

#include "test_boardcheck.moc"
