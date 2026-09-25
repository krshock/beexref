#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include "doc/image_export.h"
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

doc::ItemPtr pixmapItem(qint64 id, const QString &filename, const QByteArray &bytes)
{
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->id = id;
    item->filename = filename;
    item->format = QStringLiteral("png");
    item->source = std::make_shared<doc::BytesSource>(bytes);
    item->setOriginalSize(QSize(8, 6));
    return item;
}

} // namespace

class TestImageExport : public QObject
{
    Q_OBJECT

private slots:
    void namesMatchTheReference();
    void urlNamesUseThePathSegment();
    void writesVerbatimBytes();
    void assignsIdsToUnsavedItems();
    void skipsAndOverwritesOnConflict();
    void cancelAbortsTheRun();
    void cancelledPredicateStopsTheRun();
    void recordsFailuresForUnreadableItems();
};

void TestImageExport::namesMatchTheReference()
{
    QCOMPARE(doc::exportFilename(QStringLiteral("/a/b/pic.jpg"), QStringLiteral("png"), 7),
             QStringLiteral("0007-pic.png"));
    QCOMPARE(doc::exportFilename(QString(), QStringLiteral("jpg"), 12),
             QStringLiteral("0012.jpg"));
    QCOMPARE(doc::exportFilename(QStringLiteral("noext"), QStringLiteral("png"), 3),
             QStringLiteral("0003-noext.png"));
    QCOMPARE(doc::exportFilename(QStringLiteral("a.b.c.png"), QStringLiteral("png"), 1),
             QStringLiteral("0001-a.b.c.png"));
}

void TestImageExport::urlNamesUseThePathSegment()
{
    // A query string must never leak into the name, even when it holds
    // dots (the reference's splitext cuts at the last dot of the whole
    // string, not of the path).
    QCOMPARE(doc::exportFilename(
                 QStringLiteral("https://crowdworks.jp/attachments/1495102.jpg"
                                "?height=450&width=600"),
                 QStringLiteral("jpeg"), 7),
             QStringLiteral("0007-1495102.jpeg"));
    QCOMPARE(doc::exportFilename(
                 QStringLiteral("https://instagram.example.net/v/t51/783947786_n.jpg"
                                "?stp=dst-jpg_e35&ig_cache_key=Mzk3%3D%3D.3-ccb7-5"),
                 QStringLiteral("jpeg"), 7),
             QStringLiteral("0007-783947786_n.jpeg"));

    // No extension in the path: the segment itself is the stem.
    QCOMPARE(doc::exportFilename(
                 QStringLiteral("https://images.unsplash.com/photo-1730389051388-4cc32add20a2"
                                "?q=80&w=1935&auto=format&fit=crop"),
                 QStringLiteral("png"), 7),
             QStringLiteral("0007-photo-1730389051388-4cc32add20a2.png"));
    QCOMPARE(doc::exportFilename(QStringLiteral("https://example.com/image?id=123"),
                                 QStringLiteral("png"), 7),
             QStringLiteral("0007-image.png"));

    // No usable segment: the id-only default.
    QCOMPARE(doc::exportFilename(QStringLiteral("https://example.com/path/"),
                                 QStringLiteral("png"), 7),
             QStringLiteral("0007.png"));
    QCOMPARE(doc::exportFilename(QStringLiteral("https://example.com"), QStringLiteral("png"), 7),
             QStringLiteral("0007.png"));
    QCOMPARE(doc::exportFilename(QStringLiteral("https://example.com/.jpg"),
                                 QStringLiteral("png"), 7),
             QStringLiteral("0007.png"));

    // Decoded characters that no filesystem wants become underscores;
    // an encoded slash only leaves the last path component behind.
    QCOMPARE(doc::exportFilename(QStringLiteral("https://cdn.example.com/a%3Fb.jpg"),
                                 QStringLiteral("png"), 7),
             QStringLiteral("0007-a_b.png"));
    QCOMPARE(doc::exportFilename(QStringLiteral("https://x/%2E%2E%2Fetc%2Fpasswd.jpg"),
                                 QStringLiteral("png"), 7),
             QStringLiteral("0007-passwd.png"));

    // Local paths keep the reference's rule, `?` included.
    QCOMPARE(doc::exportFilename(QStringLiteral("/tmp/a?b.png"), QStringLiteral("png"), 7),
             QStringLiteral("0007-a?b.png"));

    // The whole name stays within one path component (255 bytes).
    const QString longUrl = QStringLiteral("https://example.com/")
        + QString(300, QLatin1Char('a')) + QStringLiteral(".png");
    const QString capped = doc::exportFilename(longUrl, QStringLiteral("png"), 1);
    QCOMPARE(capped.toUtf8().size(), 255);
    QCOMPARE(capped, QStringLiteral("0001-") + QString(246, QLatin1Char('a'))
                        + QStringLiteral(".png"));
}

void TestImageExport::writesVerbatimBytes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray png = makePng(8, 6, Qt::red);
    QVector<doc::ItemPtr> items;
    items << pixmapItem(5, QStringLiteral("red.png"), png);

    const doc::ImageExportSummary summary = doc::exportImages(items, dir.path(), {});
    QCOMPARE(summary.written, 1);
    QCOMPARE(summary.skipped, 0);
    QVERIFY(summary.ok());

    QFile file(dir.filePath(QStringLiteral("0005-red.png")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), png);
}

void TestImageExport::assignsIdsToUnsavedItems()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVector<doc::ItemPtr> items;
    items << pixmapItem(9, QStringLiteral("a.png"), makePng(8, 6, Qt::red));
    items << pixmapItem(0, QStringLiteral("b.png"), makePng(8, 6, Qt::green));
    items << pixmapItem(0, QString(), makePng(8, 6, Qt::blue));

    const doc::ImageExportSummary summary = doc::exportImages(items, dir.path(), {});
    QCOMPARE(summary.written, 3);
    // Unsaved items continue after the highest existing id.
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("0009-a.png"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("0010-b.png"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("0011.png"))));
}

void TestImageExport::skipsAndOverwritesOnConflict()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = dir.filePath(QStringLiteral("0001-a.png"));
    QFile existing(target);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("old");
    existing.close();

    QVector<doc::ItemPtr> items;
    items << pixmapItem(1, QStringLiteral("a.png"), makePng(8, 6, Qt::red));

    // Skip leaves the old content alone.
    doc::ImageExportSummary summary =
        doc::exportImages(items, dir.path(), [](const QString &) {
            return std::optional<doc::ExportConflict>(doc::ExportConflict::Skip);
        });
    QCOMPARE(summary.written, 0);
    QCOMPARE(summary.skipped, 1);
    QFile afterSkip(target);
    QVERIFY(afterSkip.open(QIODevice::ReadOnly));
    QCOMPARE(afterSkip.readAll(), QByteArray("old"));

    // Overwrite replaces it.
    summary = doc::exportImages(items, dir.path(), [](const QString &) {
        return std::optional<doc::ExportConflict>(doc::ExportConflict::Overwrite);
    });
    QCOMPARE(summary.written, 1);
    QFile afterOverwrite(target);
    QVERIFY(afterOverwrite.open(QIODevice::ReadOnly));
    QVERIFY(afterOverwrite.readAll() != QByteArray("old"));
}

void TestImageExport::cancelAbortsTheRun()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVector<doc::ItemPtr> items;
    items << pixmapItem(1, QStringLiteral("a.png"), makePng(8, 6, Qt::red));
    QFile existing(dir.filePath(QStringLiteral("0001-a.png")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("old");
    existing.close();

    const doc::ImageExportSummary summary =
        doc::exportImages(items, dir.path(), [](const QString &) {
            return std::optional<doc::ExportConflict>(); // abort
        });
    QVERIFY(summary.cancelled);
    QCOMPARE(summary.written, 0);
    QCOMPARE(summary.skipped, 0);
}

void TestImageExport::cancelledPredicateStopsTheRun()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVector<doc::ItemPtr> items;
    items << pixmapItem(1, QStringLiteral("a.png"), makePng(8, 6, Qt::red));
    items << pixmapItem(2, QStringLiteral("b.png"), makePng(8, 6, Qt::green));
    items << pixmapItem(3, QStringLiteral("c.png"), makePng(8, 6, Qt::blue));

    int polls = 0;
    const doc::ImageExportSummary summary =
        doc::exportImages(items, dir.path(), {}, {},
                          [&polls]() { return ++polls > 1; });
    QVERIFY(summary.cancelled);
    // The first file was written; the second poll stopped the run.
    QCOMPARE(summary.written, 1);
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("0001-a.png"))));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("0002-b.png"))));
}

void TestImageExport::recordsFailuresForUnreadableItems()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->id = 1;
    item->filename = QStringLiteral("gone.png");
    item->format = QStringLiteral("png");
    // No source at all: the bytes cannot be produced.
    QVector<doc::ItemPtr> items;
    items << item;

    const doc::ImageExportSummary summary = doc::exportImages(items, dir.path(), {});
    QCOMPARE(summary.written, 0);
    QCOMPARE(summary.errors.size(), 1);
    QVERIFY(!summary.ok());
}

QTEST_GUILESS_MAIN(TestImageExport)

#include "test_image_export.moc"
