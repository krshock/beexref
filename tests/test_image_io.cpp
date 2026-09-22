#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QImageReader>
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
    void bakesNonSquareOrientationAsPng();
    void rejectsUndecodableData();
    void clipboardImagesBecomePng();
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

void TestImageIo::bakesNonSquareOrientationAsPng()
{
    const QString path = asset(QStringLiteral("orientation6.jpg"));
    const doc::LoadedImage loaded = doc::loadImageFile(path);
    QVERIFY(loaded.isValid());

    // Stored 4x2 with orientation 6, so the display size changes and the
    // bytes are re-encoded losslessly.
    QCOMPARE(loaded.format, QStringLiteral("png"));
    QVERIFY(loaded.encoded.startsWith(QByteArray("\x89PNG", 4)));
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

void TestImageIo::clipboardImagesBecomePng()
{
    QImage image(4, 2, QImage::Format_ARGB32);
    image.fill(Qt::darkCyan);
    const doc::LoadedImage loaded = doc::imageToLoaded(image);
    QVERIFY(loaded.isValid());
    QCOMPARE(loaded.format, QStringLiteral("png"));
    QVERIFY(loaded.encoded.startsWith(QByteArray("\x89PNG", 4)));

    QImage decoded;
    QVERIFY(decoded.loadFromData(loaded.encoded));
    QCOMPARE(decoded.size(), image.size());
    QCOMPARE(decoded.pixelColor(1, 1), image.pixelColor(1, 1));
}

QTEST_GUILESS_MAIN(TestImageIo)

#include "test_image_io.moc"
