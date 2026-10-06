#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QtTest>

#include <cmath>

#include "doc/image_io.h"

namespace {

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

// JPEG decoders differ slightly; the reference test allows a small
// tolerance and so do we.
double pixelDistance(const QImage &left, const QImage &right, int x, int y)
{
    const QColor a = left.pixelColor(x, y);
    const QColor b = right.pixelColor(x, y);
    const double dr = a.red() - b.red();
    const double dg = a.green() - b.green();
    const double db = a.blue() - b.blue();
    const double da = a.alpha() - b.alpha();
    return std::sqrt(dr * dr + dg * dg + db * db + da * da);
}

} // namespace

class TestImageIo : public QObject
{
    Q_OBJECT

private slots:
    void keepsOriginalBytesWhenSizeMatches();
    void appliesAllExifOrientations();
    void bakesNonSquareOrientationLossless();
    void rejectsUndecodableData();
    void clipboardImagesBecomeLossless();
    void storageModesDecideTheEncoding();
};

void TestImageIo::keepsOriginalBytesWhenSizeMatches()
{
    const doc::LoadedImage jpeg = doc::loadImageFile(asset(QStringLiteral("test3x3_orientation1.jpg")));
    QVERIFY(jpeg.isValid());
    QCOMPARE(jpeg.format, QStringLiteral("jpeg"));
    QCOMPARE(jpeg.encoded, fileBytes(asset(QStringLiteral("test3x3_orientation1.jpg"))));

    const doc::LoadedImage png = doc::loadImageFile(asset(QStringLiteral("test3x3.png")));
    QVERIFY(png.isValid());
    QCOMPARE(png.format, QStringLiteral("png"));
    QCOMPARE(png.encoded, fileBytes(asset(QStringLiteral("test3x3.png"))));
    QCOMPARE(png.image.size(), QSize(3, 3));
}

void TestImageIo::appliesAllExifOrientations()
{
    const QImage expected = QImage(asset(QStringLiteral("test3x3.jpg")));
    QVERIFY(!expected.isNull());

    for (int orientation = 1; orientation <= 8; ++orientation) {
        const QString name =
            QStringLiteral("test3x3_orientation%1.jpg").arg(orientation);
        const doc::LoadedImage loaded = doc::loadImageFile(asset(name));
        QVERIFY2(loaded.isValid(), qPrintable(name));
        QCOMPARE(loaded.image.size(), expected.size());
        for (int y = 0; y < expected.height(); ++y) {
            for (int x = 0; x < expected.width(); ++x) {
                const double distance = pixelDistance(loaded.image, expected, x, y);
                QVERIFY2(distance < 10.0,
                         qPrintable(QStringLiteral("%1 at %2,%3 distance %4")
                                        .arg(name)
                                        .arg(x)
                                        .arg(y)
                                        .arg(distance)));
            }
        }
    }
}

void TestImageIo::bakesNonSquareOrientationLossless()
{
    const QString path = asset(QStringLiteral("orientation6.jpg"));
    const doc::LoadedImage loaded = doc::loadImageFile(path);
    QVERIFY(loaded.isValid());

    // Stored 4x2 with orientation 6, so the display size changes and the
    // bytes are re-encoded losslessly: WebP when the plugin is there,
    // PNG otherwise. Never the file's own (rotated) bytes.
    QVERIFY(loaded.format == QStringLiteral("webp") || loaded.format == QStringLiteral("png"));
    QVERIFY(!loaded.originalBytes);
    QVERIFY(loaded.encoded != fileBytes(path));

    const QImage upright(asset(QStringLiteral("orientation6_upright.png")));
    QVERIFY(!upright.isNull());
    QCOMPARE(loaded.image.size(), upright.size());
    for (int y = 0; y < upright.height(); ++y) {
        for (int x = 0; x < upright.width(); ++x) {
            QVERIFY2(pixelDistance(loaded.image, upright, x, y) < 2.0,
                     qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
        }
    }
}

void TestImageIo::rejectsUndecodableData()
{
    QVERIFY(!doc::loadImageData(QByteArrayLiteral("not an image")).isValid());
    QVERIFY(!doc::loadImageFile(asset(QStringLiteral("missing.png"))).isValid());
}

void TestImageIo::clipboardImagesBecomeLossless()
{
    QImage image(4, 2, QImage::Format_ARGB32);
    image.fill(Qt::darkCyan);
    const doc::LoadedImage loaded = doc::imageToLoaded(image);
    QVERIFY(loaded.isValid());
    // WebP lossless when the plugin is there, PNG otherwise; either way
    // the pixels come back exactly and no source bytes were dropped.
    QVERIFY(loaded.format == QStringLiteral("webp") || loaded.format == QStringLiteral("png"));
    QVERIFY(!loaded.originalBytes);

    QImage decoded;
    QVERIFY(decoded.loadFromData(loaded.encoded));
    QCOMPARE(decoded.size(), image.size());
    QCOMPARE(decoded.pixelColor(1, 1), image.pixelColor(1, 1));
}

void TestImageIo::storageModesDecideTheEncoding()
{
    // The setting maps to the modes; unknown values -- the upstream
    // format names included -- keep the originals.
    QCOMPARE(doc::storageModeForSetting(QStringLiteral("original")),
             doc::StorageMode::Original);
    QCOMPARE(doc::storageModeForSetting(QStringLiteral("lossless")),
             doc::StorageMode::Lossless);
    QCOMPARE(doc::storageModeForSetting(QStringLiteral("compact")),
             doc::StorageMode::Compact);
    QCOMPARE(doc::storageModeForSetting(QStringLiteral("best")), doc::StorageMode::Original);
    QCOMPARE(doc::storageModeForSetting(QStringLiteral("png")), doc::StorageMode::Original);
    QCOMPARE(doc::storageModeForSetting(QString()), doc::StorageMode::Original);

    if (!QImageWriter::supportedImageFormats().contains("webp"))
        QSKIP("the WebP plugin is not available");

    // A photo-like image (noise: incompressible, so PNG is huge and the
    // WebP paths win). Both compaction paths shrink it, and compact never
    // ends up larger than lossless.
    QImage photo(64, 64, QImage::Format_RGB32);
    quint32 noise = 12345;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            noise = noise * 1664525 + 1013904223;
            photo.setPixel(x, y, qRgb(int(noise >> 16) & 0xff, int(noise >> 8) & 0xff,
                                      int(noise) & 0xff));
        }
    }
    const QByteArray png = doc::encodePng(photo);
    QVERIFY(!png.isEmpty());

    doc::LoadedImage lossless;
    lossless.image = photo;
    lossless.encoded = png;
    lossless.format = QStringLiteral("png");
    doc::applyStorageMode(lossless, doc::StorageMode::Lossless);
    QCOMPARE(lossless.format, QStringLiteral("webp"));
    QVERIFY(lossless.encoded.size() < png.size());

    doc::LoadedImage compact;
    compact.image = photo;
    compact.encoded = png;
    compact.format = QStringLiteral("png");
    doc::applyStorageMode(compact, doc::StorageMode::Compact);
    QCOMPARE(compact.format, QStringLiteral("webp"));
    QVERIFY(compact.encoded.size() <= lossless.encoded.size());

    // Transparency stays lossless in the compact mode: the pixels come
    // back exactly.
    QImage alpha(32, 32, QImage::Format_ARGB32);
    alpha.fill(Qt::transparent);
    for (int i = 0; i < 32; ++i)
        alpha.setPixelColor(i, i, QColor(255, 0, 0, 128));
    doc::LoadedImage transparent;
    transparent.image = alpha;
    transparent.encoded = doc::encodePng(alpha);
    transparent.format = QStringLiteral("png");
    doc::applyStorageMode(transparent, doc::StorageMode::Compact);
    QCOMPARE(transparent.format, QStringLiteral("webp"));
    QImage back;
    QVERIFY(back.loadFromData(transparent.encoded, "webp"));
    for (int i = 0; i < 32; ++i)
        QCOMPARE(back.pixelColor(i, i), alpha.pixelColor(i, i));

    // A source JPEG is never re-encoded: another generation only loses.
    doc::LoadedImage jpeg;
    jpeg.image = photo;
    jpeg.encoded = QByteArrayLiteral("fake-jpeg-bytes");
    jpeg.format = QStringLiteral("jpeg");
    jpeg.originalBytes = true;
    doc::applyStorageMode(jpeg, doc::StorageMode::Compact);
    QCOMPARE(jpeg.format, QStringLiteral("jpeg"));
    QCOMPARE(jpeg.encoded, QByteArrayLiteral("fake-jpeg-bytes"));

    // Never larger: bytes that are already smaller stay untouched.
    doc::LoadedImage kept;
    kept.image = photo;
    kept.encoded = QByteArrayLiteral("tiny");
    kept.format = QStringLiteral("png");
    doc::applyStorageMode(kept, doc::StorageMode::Compact);
    QCOMPARE(kept.encoded, QByteArrayLiteral("tiny"));
    QCOMPARE(kept.format, QStringLiteral("png"));

    // Qt's WebP writer truncates some trivial images (a solid 16x16, say):
    // the encoder detects the stream that does not decode and reports
    // failure instead of storing something unreadable.
    QImage solid(16, 16, QImage::Format_RGB32);
    solid.fill(Qt::darkCyan);
    QVERIFY(doc::encodeWebp(solid, 100).isEmpty());
    QCOMPARE(doc::encodeLossless(solid, nullptr).left(4), QByteArray("\x89PNG", 4));
    doc::LoadedImage solidLoaded;
    solidLoaded.image = solid;
    solidLoaded.encoded = doc::encodePng(solid);
    solidLoaded.format = QStringLiteral("png");
    doc::applyStorageMode(solidLoaded, doc::StorageMode::Lossless);
    QCOMPARE(solidLoaded.format, QStringLiteral("png"));
}

QTEST_GUILESS_MAIN(TestImageIo)

#include "test_image_io.moc"
