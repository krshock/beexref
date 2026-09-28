#include <QColor>
#include <QImage>
#include <QPainter>
#include <QtTest>

#include "ui/grayscale.h"
#include "ui/theme.h"

namespace {

// One pixel of the given colour, ARGB32.
QImage pixelImage(const QColor &color)
{
    QImage image(1, 1, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, color);
    return image;
}

int grayAt(const QImage &image, int x = 0, int y = 0)
{
    return qGray(image.pixel(x, y));
}

} // namespace

class TestGrayscale : public QObject
{
    Q_OBJECT

private slots:
    void methodsAreTheRegistry();
    void everyMethodKeepsTheContract();
    void knownValuesPerMethod();
    void classicMatchesThePainterPath();
    void alphaCompositesOntoTheCanvas();
    void unknownMethodFallsBackToClassic();
};

void TestGrayscale::methodsAreTheRegistry()
{
    const QVector<ui::GrayscaleMethod> &methods = ui::grayscaleMethods();
    QCOMPARE(methods.size(), 7);
    QCOMPARE(methods.first().id, QStringLiteral("classic"));
    QCOMPARE(ui::defaultGrayscaleMethod(), QStringLiteral("classic"));
    for (const ui::GrayscaleMethod &method : methods) {
        QVERIFY(!method.id.isEmpty());
        QVERIFY(!method.label.isEmpty());
        QVERIFY(!method.help.isEmpty());
        QVERIFY(method.convert != nullptr);
        QCOMPARE(ui::grayscaleMethod(method.id), &method);
    }
    QCOMPARE(ui::grayscaleMethod(QStringLiteral("nope")), nullptr);
}

void TestGrayscale::everyMethodKeepsTheContract()
{
    // A source with opaque, translucent and transparent pixels, in the
    // formats a level can arrive in.
    QImage source(4, 2, QImage::Format_ARGB32);
    source.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    source.setPixelColor(1, 0, QColor(0, 255, 0, 128));
    source.setPixelColor(2, 0, QColor(0, 0, 255, 0));
    source.setPixelColor(3, 0, QColor(10, 20, 30, 255));
    source.setPixelColor(0, 1, QColor(200, 200, 200, 255));
    source.setPixelColor(1, 1, QColor(0, 0, 0, 255));
    source.setPixelColor(2, 1, QColor(255, 255, 255, 255));
    source.setPixelColor(3, 1, QColor(1, 2, 3, 77));

    for (const ui::GrayscaleMethod &method : ui::grayscaleMethods()) {
        const QImage gray = ui::grayscaleImage(source, method.id);
        QVERIFY2(!gray.isNull(), qPrintable(method.id));
        QCOMPARE(gray.size(), source.size());
        QCOMPARE(gray.format(), QImage::Format_Grayscale8);
        QVERIFY(gray.bytesPerLine() >= gray.width());
        // Opaque output: Grayscale8 has no alpha, and no pixel is left
        // unset.
        for (int y = 0; y < gray.height(); ++y) {
            for (int x = 0; x < gray.width(); ++x)
                QVERIFY(grayAt(gray, x, y) >= 0);
        }

        // A premultiplied level (the other format a decode can hand over)
        // keeps the same contract.
        const QImage premultiplied = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage fromPremultiplied = ui::grayscaleImage(premultiplied, method.id);
        QCOMPARE(fromPremultiplied.size(), source.size());
        QCOMPARE(fromPremultiplied.format(), QImage::Format_Grayscale8);
    }

    // Opaque images convert identically whatever the channel format is:
    // RGB32 and premultiplied ARGB32 are the same pixels.
    QImage opaque(3, 1, QImage::Format_ARGB32);
    opaque.setPixelColor(0, 0, QColor(255, 0, 0));
    opaque.setPixelColor(1, 0, QColor(0, 128, 255));
    opaque.setPixelColor(2, 0, QColor(10, 20, 30));
    for (const ui::GrayscaleMethod &method : ui::grayscaleMethods()) {
        const QImage fromArgb = ui::grayscaleImage(opaque, method.id);
        QCOMPARE(ui::grayscaleImage(opaque.convertToFormat(QImage::Format_RGB32), method.id),
                 fromArgb);
        QCOMPARE(ui::grayscaleImage(
                     opaque.convertToFormat(QImage::Format_ARGB32_Premultiplied), method.id),
                 fromArgb);
    }

    // Null in, null out.
    QVERIFY(ui::grayscaleImage(QImage(), QStringLiteral("classic")).isNull());
}

void TestGrayscale::knownValuesPerMethod()
{
    const QColor red(255, 0, 0);
    const QColor green(0, 255, 0);
    const QColor blue(0, 0, 255);
    const QColor black(0, 0, 0);
    const QColor white(255, 255, 255);
    const QColor mid(128, 128, 128);

    const auto value = [](const char *id, const QColor &color) {
        return grayAt(ui::grayscaleImage(pixelImage(color), QString::fromLatin1(id)));
    };

    // Classic is Qt's painter conversion, which is gamma-corrected
    // (linear-light) Rec.709 luminance -- not the classic qGray integer
    // formula. These are the values it has always produced.
    QCOMPARE(value("classic", red), 127);
    QCOMPARE(value("classic", green), 220);
    QCOMPARE(value("classic", blue), 76);
    QCOMPARE(value("classic", mid), 128);

    // BT.601 luma: 0.299/0.587/0.114 in 8-bit fixed point.
    QCOMPARE(value("bt601", red), 77);
    QCOMPARE(value("bt601", green), 149);
    QCOMPARE(value("bt601", blue), 29);

    // BT.709 luma: 0.2126/0.7152/0.0722.
    QCOMPARE(value("bt709", red), 54);
    QCOMPARE(value("bt709", green), 182);
    QCOMPARE(value("bt709", blue), 19);

    // Average, lightness, max and min are exact integer math.
    QCOMPARE(value("average", red), 85);
    QCOMPARE(value("average", white), 255);
    QCOMPARE(value("average", mid), 128);
    QCOMPARE(value("lightness", red), 127);
    QCOMPARE(value("lightness", white), 255);
    QCOMPARE(value("max", red), 255);
    QCOMPARE(value("max", mid), 128);
    QCOMPARE(value("min", red), 0);
    QCOMPARE(value("min", white), 255);

    // Black and white agree in every method.
    for (const ui::GrayscaleMethod &method : ui::grayscaleMethods()) {
        QCOMPARE(grayAt(ui::grayscaleImage(pixelImage(black), method.id)), 0);
        QCOMPARE(grayAt(ui::grayscaleImage(pixelImage(white), method.id)), 255);
    }
}

void TestGrayscale::classicMatchesThePainterPath()
{
    // The default must stay byte-identical to the conversion BeeXRef
    // always used: a Grayscale8 canvas filled with the canvas colour and
    // the image drawn onto it.
    QImage source(6, 4, QImage::Format_ARGB32);
    source.fill(QColor(0, 0, 0, 0));
    source.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    source.setPixelColor(1, 1, QColor(0, 255, 0, 128));
    source.setPixelColor(2, 2, QColor(0, 0, 255, 64));
    source.setPixelColor(3, 3, QColor(255, 255, 255, 255));

    QImage reference(source.size(), QImage::Format_Grayscale8);
    reference.fill(ui::theme::canvas);
    QPainter painter(&reference);
    painter.drawImage(0, 0, source);
    painter.end();

    QCOMPARE(ui::grayscaleImage(source, QStringLiteral("classic")), reference);
}

void TestGrayscale::alphaCompositesOntoTheCanvas()
{
    // Half-transparent red over the canvas colour: (158, 30, 30), then
    // the formula. Max, average and min are exact; classic goes through
    // Qt's painter, so it is only checked for the right ballpark.
    const QImage source = pixelImage(QColor(255, 0, 0, 128));
    const auto value = [&source](const char *id) {
        return grayAt(ui::grayscaleImage(source, QString::fromLatin1(id)));
    };
    QCOMPARE(value("max"), 158);
    QCOMPARE(value("average"), 72);
    QCOMPARE(value("min"), 30);
    // Classic goes through Qt's painter, which rounds its own way; it is
    // the linear-light value for the composited pixel.
    QCOMPARE(value("classic"), 81);
}

void TestGrayscale::unknownMethodFallsBackToClassic()
{
    QImage source(3, 1, QImage::Format_ARGB32);
    source.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    source.setPixelColor(1, 0, QColor(0, 255, 0, 200));
    source.setPixelColor(2, 0, QColor(0, 0, 255, 0));

    const QImage classic = ui::grayscaleImage(source, QStringLiteral("classic"));
    QCOMPARE(ui::grayscaleImage(source, QStringLiteral("nope")), classic);
    QCOMPARE(ui::grayscaleImage(source, QString()), classic);
}

QTEST_MAIN(TestGrayscale)

#include "test_grayscale.moc"
