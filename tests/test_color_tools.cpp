#include <QtTest>

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QSignalSpy>

#include <cmath>

#include "doc/item.h"
#include "doc/source.h"
#include "ui/color_gamut.h"
#include "ui/color_tools.h"

using ui::colors::GamutKey;

namespace {

QByteArray pngBytes(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

class TestColorTools : public QObject
{
    Q_OBJECT

private slots:
    void hexFormatsOpaqueAndAlpha();
    void gamutSampleStepLimitsSampling();
    void gamutHistogramCountsHueSaturation();
    void gamutHistogramSkipsSampledPixels();
    void gamutDotPositionFollowsHueAndSaturation();
    void gamutThreadDecodesTheSource();
};

void TestColorTools::hexFormatsOpaqueAndAlpha()
{
    // #rrggbb when opaque, plus the alpha byte when not, like the
    // reference's qcolor_to_hex.
    QCOMPARE(ui::colors::hex(QColor(255, 0, 128)), QStringLiteral("#ff0080"));
    QCOMPARE(ui::colors::hex(QColor(255, 0, 128, 128)), QStringLiteral("#ff008080"));
    QCOMPARE(ui::colors::hex(QColor(255, 255, 255, 0)), QStringLiteral("#ffffff00"));
}

void TestColorTools::gamutSampleStepLimitsSampling()
{
    // Up to a thousand samples per side, like the reference.
    QCOMPARE(ui::colors::gamutSampleStep(1000, 500), 1);
    QCOMPARE(ui::colors::gamutSampleStep(2000, 1000), 2);
    QCOMPARE(ui::colors::gamutSampleStep(500, 3000), 3);
}

void TestColorTools::gamutHistogramCountsHueSaturation()
{
    QImage image(4, 2, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, QColor(255, 0, 0));     // red: hue 0
    image.setPixelColor(1, 0, QColor(0, 255, 0));     // green: hue 120
    image.setPixelColor(2, 0, QColor(128, 128, 128)); // gray: hue -1
    image.setPixelColor(3, 0, QColor(255, 255, 255)); // white: ignored
    image.setPixelColor(0, 1, QColor(250, 250, 250)); // near white: ignored
    image.setPixelColor(1, 1, QColor(5, 5, 5));       // near black: ignored
    image.setPixelColor(2, 1, QColor(0, 0, 0, 0));    // transparent: ignored
    image.setPixelColor(3, 1, QColor(6, 6, 6));       // dark gray: counted

    const QHash<GamutKey, int> gamut = ui::colors::gamutHistogram(image);
    QCOMPARE(gamut.size(), 3);
    QCOMPARE(gamut.value(GamutKey{0, 255}), 1);
    QCOMPARE(gamut.value(GamutKey{120, 255}), 1);
    // The grey pixels share the achromatic bucket.
    QCOMPARE(gamut.value(GamutKey{-1, 0}), 2);
}

void TestColorTools::gamutHistogramSkipsSampledPixels()
{
    QImage image(2001, 1, QImage::Format_ARGB32);
    image.fill(QColor(0, 0, 0, 0));
    image.setPixelColor(0, 0, QColor(255, 0, 0));
    image.setPixelColor(1, 0, QColor(0, 255, 0));

    // The step for this width is 2, so only even columns are read.
    const QHash<GamutKey, int> gamut = ui::colors::gamutHistogram(image);
    QCOMPARE(gamut.size(), 1);
    QCOMPARE(gamut.value(GamutKey{0, 255}), 1);
}

void TestColorTools::gamutDotPositionFollowsHueAndSaturation()
{
    const int radius = 100;
    // Hue 0 points left of the centre, hue 90 upwards (the formula
    // starts at -90 degrees), like the reference.
    const QPointF red = ui::colors::gamutDotPosition(GamutKey{0, 255}, radius);
    QVERIFY(std::abs(red.x() - 0.0) < 1.0e-9);
    QVERIFY(std::abs(red.y() - 100.0) < 1.0e-9);

    const QPointF quarter = ui::colors::gamutDotPosition(GamutKey{90, 255}, radius);
    QVERIFY(std::abs(quarter.x() - 100.0) < 1.0e-9);
    QVERIFY(std::abs(quarter.y() - 0.0) < 1.0e-9);

    // Achromatic pixels sit in the middle; half saturation halfway out.
    QCOMPARE(ui::colors::gamutDotPosition(GamutKey{-1, 0}, radius), QPointF(100, 100));
    const QPointF half = ui::colors::gamutDotPosition(GamutKey{0, 128}, radius);
    QVERIFY(std::abs(half.x() - 50.0) < 1.0);
}

void TestColorTools::gamutThreadDecodesTheSource()
{
    QImage image(2, 1, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, QColor(255, 0, 0));
    image.setPixelColor(1, 0, QColor(0, 255, 0));

    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->source = std::make_shared<doc::BytesSource>(pngBytes(image));

    ui::GamutThread thread(item->source, QImage());
    QSignalSpy spy(&thread, &ui::GamutThread::gamutReady);
    thread.start();
    QVERIFY(spy.wait(5000));

    QCOMPARE(thread.gamut().value(GamutKey{0, 255}), 1);
    QCOMPARE(thread.gamut().value(GamutKey{120, 255}), 1);
}

QTEST_GUILESS_MAIN(TestColorTools)

#include "test_color_tools.moc"
