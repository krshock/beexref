#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "cache/session_cache.h"
#include "doc/document.h"
#include "doc/undo.h"
#include "ui/level_loader.h"
#include "ui/lod_manager.h"
#include "ui/scene.h"
#include "ui/scene_item.h"

using ui::SceneItem;

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

doc::ItemPtr bigItem(int width, int height, const QColor &color)
{
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->source = std::make_shared<doc::BytesSource>(makePng(width, height, color));
    item->format = QStringLiteral("png");
    item->setOriginalSize(QSize(width, height));
    return item;
}

// Three big images spread across a wide viewport: red at x=0, green at
// x=2000, blue at x=4000, all visible from a viewport of 6000 width.
std::shared_ptr<doc::Document> threeVisibleItems()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const QVector<QPair<double, QColor>> spec = {
        {0, Qt::darkRed}, {2000, Qt::darkGreen}, {4000, Qt::darkBlue}};
    for (const auto &entry : spec) {
        const doc::ItemPtr item = bigItem(1200, 900, entry.second);
        item->x = entry.first;
        document->addItem(item);
    }
    return document;
}

// The dominant channel of a decoded level, to identify an image.
QChar dominantChannel(const QImage &image)
{
    const QColor colour = image.pixelColor(image.width() / 2, image.height() / 2);
    if (colour.red() >= colour.green() && colour.red() >= colour.blue())
        return QLatin1Char('r');
    if (colour.green() >= colour.blue())
        return QLatin1Char('g');
    return QLatin1Char('b');
}

QChar decodedChannel(const QSignalSpy &ready, int index)
{
    return dominantChannel(ready.at(index).at(1).value<QImage>());
}

SceneItem *firstPixmap(ui::Scene &scene)
{
    const QVector<SceneItem *> items = scene.pixmapItemViews();
    return items.isEmpty() ? nullptr : items.first();
}

} // namespace

class TestLodManager : public QObject
{
    Q_OBJECT

private slots:
    void defersUpgradesUntilInteraction();
    void cullsNeverVisibleItemsToCoarsest();
    void ramBudgetDowngradesLevels();
    void failedDecodesBackOff();
    void singleMethodLoadsFullSize();
    void undoWhileLevelDecodes();
    void cacheServesLevelsWithoutDecoding();
    void coalescesBurstsPerItem();
    void holdDelaysEvaluationsUntilItExpires();
    void selectedRequestsRunFirst();
    void nearestVisibleItemsDecodeFirst();
    void selectedItemDecodesFirst();
    void priorityPointOrdersByDistance();
    void deprioritizedRequestsQueueBehindVisibleOnes();
    void offScreenDecodesWaitBehindVisibleOnes();
    void retainedFloorCopyAvoidsDecodeOnCull();
    void coarsestDecodeIsRetainedForLaterCulls();
    void residentLodBytesCountsSharedFloorOnce();
    void reservedBytesTrackInFlightDecodes();
    void offScreenDowngradesOrderByBytesFreed();
    void primaryBudgetSkipsRequestsThatDoNotFit();
    void ramCacheServesRepeatLevelsAndEvicts();
};

void TestLodManager::defersUpgradesUntilInteraction()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::red));

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();
    manager.reset(); // a fresh load defers upgrades

    SceneItem *item = firstPixmap(scene);
    QVERIFY(item);
    QCOMPARE(item->levels().size(), 5); // 1, 0.5, 0.25, 0.125, 0.0625 all above 64 px

    QTest::qWait(200);
    // Deferred: no level was requested yet.
    QCOMPARE(item->levelFraction(), 0.0);

    // The first interaction upgrades what is visible: needed is
    // 2000 * 0.1 * 1.2 = 240 px wide, covered by the 0.125 level.
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(item->levelFraction(), 0.125, 5000);
    QCOMPARE(item->level().size(), QSize(250, 125));
}

void TestLodManager::cullsNeverVisibleItemsToCoarsest()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::blue));

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    // The viewport is far away, so the item was never visible.
    manager.setViewState(QRectF(100000, 100000, 400, 300), 0.1);
    manager.evaluateNow();

    SceneItem *item = firstPixmap(scene);
    QVERIFY(item);
    const double coarsest = item->coarsestFraction();
    QCOMPARE(coarsest, 0.0625);
    QTRY_COMPARE_WITH_TIMEOUT(item->levelFraction(), coarsest, 5000);
}

void TestLodManager::ramBudgetDowngradesLevels()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr first = bigItem(2000, 1000, Qt::green);
    const doc::ItemPtr second = bigItem(2000, 1000, Qt::yellow);
    second->x = 3000;
    document->addItem(first);
    document->addItem(second);

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    ui::LodSettings settings;
    settings.method = QStringLiteral("ram_budget");
    settings.budgetMB = 1; // far below the two full levels (16 MB)
    // The ram_budget method caps how much stays resident, not what may
    // be decoded, so admission is not the limiter here.
    settings.primaryBudgetMB = 0;
    manager.setSettings(settings);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 6000, 1000), 0.5);
    manager.evaluateNow();

    // Both items want the full level; the budget forces downgrades.
    QTRY_VERIFY_WITH_TIMEOUT(firstPixmap(scene)->levelFraction() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(scene.pixmapItemViews().size() == 2
                                 && scene.pixmapItemViews().at(1)->levelFraction() > 0,
                             5000);
    for (SceneItem *item : scene.pixmapItemViews()) {
        QVERIFY2(item->levelFraction() < 1.0,
                 qPrintable(QStringLiteral("fraction %1").arg(item->levelFraction())));
    }
}

void TestLodManager::failedDecodesBackOff()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->source = std::make_shared<doc::BytesSource>(QByteArrayLiteral("not an image"));
    item->setOriginalSize(QSize(2000, 1000));
    document->addItem(item);

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);

    for (int attempt = 0; attempt < 5; ++attempt) {
        manager.evaluateNow();
        QTest::qWait(80);
    }

    SceneItem *view = firstPixmap(scene);
    QVERIFY(view);
    QVERIFY2(view->failures() >= 3, qPrintable(QStringLiteral("failures %1").arg(view->failures())));
    QVERIFY(view->retryBlocked());

    // A blocked item is not requested again within the cooldown.
    const int requests = manager.stats().requests;
    manager.evaluateNow();
    QTest::qWait(80);
    QCOMPARE(manager.stats().requests, requests);
}

void TestLodManager::singleMethodLoadsFullSize()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(300, 200, Qt::magenta));

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    ui::LodSettings settings;
    settings.method = QStringLiteral("single");
    manager.setSettings(settings);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 300, 200), 1.0);
    manager.evaluateNow();

    SceneItem *item = firstPixmap(scene);
    QVERIFY(item);
    QCOMPARE(item->levels().size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(item->levelFraction(), 1.0, 5000);
    QCOMPARE(item->level().size(), QSize(300, 200));
}

void TestLodManager::undoWhileLevelDecodes()
{
    // Undoing an insert while its level is still decoding must drop the
    // scheduler state, not deliver into a deleted view.
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.5);

    doc::UndoStack stack(document.get());
    const doc::ItemPtr item = bigItem(2000, 1000, Qt::darkCyan);
    stack.push(std::make_unique<doc::AddItemsCommand>(QVector<doc::ItemPtr>{item}));
    scene.syncDocument();
    manager.evaluateNow(); // starts a decode for the new item
    QTest::qWait(20);

    QVERIFY(stack.undo());
    scene.syncDocument(); // deletes the view with the decode in flight
    QCOMPARE(document->items().size(), 0);
    QCOMPARE(scene.pixmapItemViews().size(), 0);
    QTest::qWait(400); // any queued decode result arrives here
    QCOMPARE(scene.itemViews().size(), 0);
}

void TestLodManager::cacheServesLevelsWithoutDecoding()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto cache = cache::SessionCache::create(dir.path());
    QVERIFY(cache->isAvailable());

    // A cached level is served even when the source cannot be decoded:
    // that is what makes culling and reopening cheap.
    const QByteArray png = makePng(20, 10, Qt::red);
    QVERIFY(cache->put(QStringLiteral("lod"), QStringLiteral("key1"), QStringLiteral("png"), png));

    ui::LevelLoader loader;
    loader.setLevelCache(cache);
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    QSignalSpy failed(&loader, &ui::LevelLoader::levelFailed);

    auto emptySource = std::make_shared<doc::BytesSource>(QByteArray());
    loader.request(1, emptySource, QSize(20, 10), QStringLiteral("smooth"),
                   QStringLiteral("key1"));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);

    // Without a cache entry the same request fails.
    loader.request(2, emptySource, QSize(20, 10), QStringLiteral("smooth"),
                   QStringLiteral("missing"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
}

void TestLodManager::coalescesBurstsPerItem()
{
    // A zoom burst posts one request per step; the worker must decode
    // only the latest one, as the reference worker does.
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    QSignalSpy failed(&loader, &ui::LevelLoader::levelFailed);
    QSignalSpy cancelled(&loader, &ui::LevelLoader::levelCancelled);

    auto source = std::make_shared<doc::BytesSource>(makePng(400, 300, Qt::darkBlue));
    for (quint64 requestId = 1; requestId <= 6; ++requestId) {
        loader.request(requestId, source, QSize(40 * int(requestId), 30 * int(requestId)),
                       QStringLiteral("fast"), QString(), QStringLiteral("item-uuid"));
    }

    QTRY_COMPARE_WITH_TIMEOUT(ready.count() + failed.count() + cancelled.count(), 6, 15000);
    QCOMPARE(failed.count(), 0);
    // The final request always runs; earlier ones are dropped, except
    // for one that may already have been decoding when the burst began.
    QVERIFY(ready.count() >= 1);
    QCOMPARE(ready.last().at(0).toULongLong(), quint64(6));
    QVERIFY(cancelled.count() >= 4);
}

void TestLodManager::holdDelaysEvaluationsUntilItExpires()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::red));

    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();
    manager.reset();

    SceneItem *item = firstPixmap(scene);
    QVERIFY(item);

    // While the zoom burst is held, neither schedule() nor evaluateNow()
    // does anything (the setup's own evaluation is the baseline).
    const int baseline = manager.stats().requests;
    manager.hold(60);
    QVERIFY(manager.holding());
    manager.schedule();
    manager.evaluateNow();
    QTest::qWait(20);
    QCOMPARE(manager.stats().requests, baseline);
    // No level was swapped in, even if the setup's own decode finished.
    QCOMPARE(item->levelFraction(), 0.0);

    // Restarting the window keeps the hold.
    manager.hold(60);
    QTest::qWait(40);
    QCOMPARE(manager.stats().requests, baseline);
    QVERIFY(manager.holding());

    // After it expires one evaluation runs and the visible item is
    // upgraded to the wanted level.
    QTRY_COMPARE_WITH_TIMEOUT(item->levelFraction(), 0.125, 5000);
    QVERIFY(!manager.holding());
    QVERIFY(manager.stats().requests > baseline);
}

void TestLodManager::deprioritizedRequestsQueueBehindVisibleOnes()
{
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);

    auto offScreen = std::make_shared<doc::BytesSource>(makePng(1200, 900, Qt::darkBlue));
    auto visible = std::make_shared<doc::BytesSource>(makePng(1200, 900, Qt::darkRed));
    // The deprioritized request is queued first; the visible one second.
    loader.request(1, offScreen, QSize(300, 225), QStringLiteral("fast"), QString(),
                   QStringLiteral("off-screen"), ui::RequestBand::Deferred);
    loader.request(2, visible, QSize(300, 225), QStringLiteral("fast"), QString(),
                   QStringLiteral("visible"), ui::RequestBand::Visible);

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 15000);
    // The normal-priority decode runs first, the deprioritized one after.
    QCOMPARE(ready.at(0).at(0).toULongLong(), quint64(2));
    QCOMPARE(ready.at(1).at(0).toULongLong(), quint64(1));
}

void TestLodManager::offScreenDecodesWaitBehindVisibleOnes()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    // The visible item first, so the off-screen (topmost) one is
    // evaluated first; the off-screen one sits far to the right.
    document->addItem(bigItem(1200, 900, Qt::darkRed));
    const doc::ItemPtr offScreen = bigItem(1200, 900, Qt::darkBlue);
    offScreen->x = 5000;
    document->addItem(offScreen);

    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    // Only the first item's area is on screen.
    manager.setViewState(QRectF(0, 0, 1200, 900), 0.1);
    manager.evaluateNow();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 15000);
    // The visible item (red) decodes first, the off-screen one (blue)
    // after it, whatever order they were queued in.
    const QImage first = ready.at(0).at(1).value<QImage>();
    const QColor firstColour = first.pixelColor(first.width() / 2, first.height() / 2);
    QVERIFY2(firstColour.red() > firstColour.blue(),
             qPrintable(QStringLiteral("first decode was %1").arg(firstColour.name())));
    const QImage second = ready.at(1).at(1).value<QImage>();
    const QColor secondColour = second.pixelColor(second.width() / 2, second.height() / 2);
    QVERIFY(secondColour.blue() > secondColour.red());
}

void TestLodManager::nearestVisibleItemsDecodeFirst()
{
    auto document = threeVisibleItems();
    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 6000, 1200), 0.1);
    manager.evaluateNow();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 20000);
    // The viewport centre is at (3000, 600): green (x=2000) is nearest,
    // then blue (x=4000), then red (x=0).
    QCOMPARE(decodedChannel(ready, 0), QLatin1Char('g'));
    QCOMPARE(decodedChannel(ready, 1), QLatin1Char('b'));
    QCOMPARE(decodedChannel(ready, 2), QLatin1Char('r'));
}

void TestLodManager::selectedItemDecodesFirst()
{
    auto document = threeVisibleItems();
    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 6000, 1200), 0.1);

    // The farthest image (red) is selected: it decodes first even
    // though it is the worst by distance.
    SceneItem *red = scene.itemViewFor(document->items().value(0));
    QVERIFY(red);
    red->setSelected(true);
    manager.evaluateNow();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 20000);
    QCOMPARE(decodedChannel(ready, 0), QLatin1Char('r'));
}

void TestLodManager::priorityPointOrdersByDistance()
{
    auto document = threeVisibleItems();
    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 6000, 1200), 0.1);
    // An explicit point near the right-hand image.
    manager.setOrderOrigin(QPointF(5000, 450));
    manager.evaluateNow();

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 20000);
    QCOMPARE(decodedChannel(ready, 0), QLatin1Char('b'));
    QCOMPARE(decodedChannel(ready, 1), QLatin1Char('g'));
    QCOMPARE(decodedChannel(ready, 2), QLatin1Char('r'));
}

void TestLodManager::selectedRequestsRunFirst()
{
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);

    auto visible = std::make_shared<doc::BytesSource>(makePng(1200, 900, Qt::darkRed));
    auto selected = std::make_shared<doc::BytesSource>(makePng(1200, 900, Qt::darkBlue));
    // The selected request is queued last but runs first.
    loader.request(1, visible, QSize(300, 225), QStringLiteral("fast"), QString(),
                   QStringLiteral("visible"), ui::RequestBand::Visible);
    loader.request(2, selected, QSize(300, 225), QStringLiteral("fast"), QString(),
                   QStringLiteral("selected"), ui::RequestBand::Selected);

    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 15000);
    QCOMPARE(ready.at(0).at(0).toULongLong(), quint64(2));
    QCOMPARE(ready.at(1).at(0).toULongLong(), quint64(1));
}

void TestLodManager::retainedFloorCopyAvoidsDecodeOnCull()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = bigItem(2000, 1000, Qt::red);
    // A stored floor level, as a loaded board would carry.
    item->floorData = makePng(64, 32, Qt::darkGray);
    item->floorFraction = 64.0 / 2000.0;
    document->addItem(item);

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);

    SceneItem *view = firstPixmap(scene);
    QVERIFY(view);
    // The placeholder floor is the coarsest level, so its copy is kept.
    QCOMPARE(view->coarsestFraction(), item->floorFraction);
    QVERIFY(view->hasCoarsestCopy());

    // Bring it on screen and let it upgrade: that costs one decode.
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.125, 5000);
    const int decodesAfterUpgrade = manager.stats().decodes;
    QVERIFY(decodesAfterUpgrade >= 1);

    // Move the viewport far away. The pass in which it leaves keeps the
    // current level; the next pass culls it back to the floor from the
    // retained copy, without another decode.
    manager.setViewState(QRectF(100000, 100000, 400, 300), 0.1);
    manager.evaluateNow();
    QCOMPARE(view->levelFraction(), 0.125);
    manager.evaluateNow();
    QCOMPARE(view->levelFraction(), item->floorFraction);
    QCOMPARE(manager.stats().decodes, decodesAfterUpgrade);
}

void TestLodManager::coarsestDecodeIsRetainedForLaterCulls()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::red));

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);

    SceneItem *view = firstPixmap(scene);
    QVERIFY(view);
    // No stored floor: the coarsest level is a ladder fraction.
    QCOMPARE(view->coarsestFraction(), 0.0625);
    QVERIFY(!view->hasCoarsestCopy());

    // Visible: upgrades from the ladder, one decode.
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.125, 5000);
    const int afterUpgrade = manager.stats().decodes;

    // Culled to the coarsest: this still needs one decode, and keeps it.
    manager.setViewState(QRectF(100000, 100000, 400, 300), 0.1);
    manager.evaluateNow();
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.0625, 5000);
    QVERIFY(view->hasCoarsestCopy());
    const int afterFirstCull = manager.stats().decodes;
    QVERIFY(afterFirstCull > afterUpgrade);

    // Back on screen and off again: the second cull is a swap.
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.125, 5000);
    manager.setViewState(QRectF(100000, 100000, 400, 300), 0.1);
    manager.evaluateNow();
    manager.evaluateNow();
    QCOMPARE(view->levelFraction(), 0.0625);
    QCOMPARE(manager.stats().decodes, afterFirstCull + 1);
}

void TestLodManager::residentLodBytesCountsSharedFloorOnce()
{
    auto model = std::make_shared<doc::Item>(doc::kTypePixmap);
    model->setOriginalSize(QSize(2000, 1000));

    SceneItem view(model);
    const double floorFraction = 0.032;
    view.setLevels({{floorFraction, QSize(64, 32)}, {0.125, QSize(250, 125)}});

    const QImage floor(64, 32, QImage::Format_ARGB32);
    view.setLevel(floor, floorFraction);
    view.rememberCoarsestLevel();
    QVERIFY(view.hasCoarsestCopy());
    // Displayed and coarsest level share their pixels: counted once.
    QCOMPARE(view.residentLodBytes(), qint64(64) * 32 * 4);

    const QImage fine(250, 125, QImage::Format_ARGB32);
    view.setLevel(fine, 0.125);
    // The retained floor is now a separate buffer.
    QCOMPARE(view.residentLodBytes(), qint64(250) * 125 * 4 + qint64(64) * 32 * 4);
}

void TestLodManager::reservedBytesTrackInFlightDecodes()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::red));

    ui::Scene scene;
    ui::LevelLoader loader;
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();

    // The decode is queued but its result has not been delivered yet.
    QVERIFY(manager.stats().lodReservedMB > 0);
    QCOMPARE(manager.stats().lodCommittedMB, 0.0);

    SceneItem *view = firstPixmap(scene);
    QVERIFY(view);
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.125, 5000);

    // Resolved: the reservation moved into the committed bytes.
    const ui::LodManager::Stats settled = manager.stats();
    QCOMPARE(settled.lodReservedMB, 0.0);
    QVERIFY(settled.lodCommittedMB > 0.0);
    QCOMPARE(settled.lodCommittedMB, view->residentLodBytes() / (1024.0 * 1024.0));
}

void TestLodManager::offScreenDowngradesOrderByBytesFreed()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::darkRed));
    const doc::ItemPtr small = bigItem(400, 200, Qt::darkBlue);
    small->x = 4000;
    document->addItem(small);

    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    scene.setDocument(document);

    // Both on screen, so both upgrade to full size.
    manager.setViewState(QRectF(0, 0, 5000, 1200), 0.5);
    manager.evaluateNow();
    SceneItem *red = scene.itemViewFor(document->items().value(0));
    SceneItem *blue = scene.itemViewFor(document->items().value(1));
    QVERIFY(red && blue);
    QTRY_COMPARE_WITH_TIMEOUT(red->levelFraction(), 1.0, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(blue->levelFraction(), 1.0, 5000);
    const int afterUpgrades = ready.count();
    QCOMPARE(afterUpgrades, 2);

    // Both leave the viewport. By distance the small blue one is nearer
    // to the far centre, but the big red one frees far more RAM, so its
    // downgrade decodes first.
    manager.setViewState(QRectF(100000, 100000, 400, 300), 0.5);
    manager.evaluateNow();
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), afterUpgrades + 2, 15000);
    QCOMPARE(decodedChannel(ready, afterUpgrades), QLatin1Char('r'));
    QCOMPARE(decodedChannel(ready, afterUpgrades + 1), QLatin1Char('b'));
}

void TestLodManager::primaryBudgetSkipsRequestsThatDoNotFit()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::red));

    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    manager.setScene(&scene);
    manager.setLoader(&loader);
    // 1 MB total: below what the full level (8 MB) needs, so the request
    // is refused instead of overflowing the budget.
    ui::LodSettings tight;
    tight.method = QStringLiteral("fixed");
    tight.primaryBudgetMB = 1;
    manager.setSettings(tight);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 2000, 1000), 1.0);
    manager.evaluateNow();
    QTest::qWait(200);

    SceneItem *view = firstPixmap(scene);
    QVERIFY(view);
    QCOMPARE(view->levelFraction(), 0.0);
    QCOMPARE(manager.stats().requests, 0);
    QVERIFY(manager.stats().admissionRejects > 0);
    QCOMPARE(ready.count(), 0);

    // With no budget the same evaluation requests the level.
    ui::LodSettings open;
    open.method = QStringLiteral("fixed");
    open.primaryBudgetMB = 0;
    manager.setSettings(open);
    manager.evaluateNow();
    QTRY_VERIFY_WITH_TIMEOUT(manager.stats().requests > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 5000);
}

void TestLodManager::ramCacheServesRepeatLevelsAndEvicts()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(bigItem(2000, 1000, Qt::red));

    ui::Scene scene;
    ui::LevelLoader loader;
    QSignalSpy ready(&loader, &ui::LevelLoader::levelReady);
    ui::LodManager manager;
    ui::LodSettings settings;
    settings.method = QStringLiteral("fixed");
    settings.primaryBudgetMB = 0;
    settings.ramCacheMB = 1; // the 0.125 level (about 122 KB) fits
    manager.setScene(&scene);
    manager.setLoader(&loader);
    manager.setSettings(settings);
    scene.setDocument(document);
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();

    SceneItem *view = firstPixmap(scene);
    QVERIFY(view);
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.125, 5000);
    // Give the worker its delayed cost pass a moment to run.
    QTest::qWait(300);
    QVERIFY(manager.stats().lodRamCacheMB > 0);

    // Off screen and back. Leaving keeps the current level for one pass
    // and only the next pass culls; coming back re-requests 0.125, which
    // the worker serves from its RAM cache instead of decoding. The
    // session cache is empty, so a decode would be a real one.
    manager.setViewState(QRectF(100000, 100000, 400, 300), 0.1);
    manager.evaluateNow();
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.0625, 5000);
    QTest::qWait(100);
    const int decodesBeforeReturn = manager.stats().decodes;
    manager.setViewState(QRectF(0, 0, 2000, 1000), 0.1);
    manager.evaluateNow();
    QTRY_COMPARE_WITH_TIMEOUT(view->levelFraction(), 0.125, 5000);
    // One more decode (the cull to 0.0625); the return did not decode.
    QVERIFY(manager.stats().decodes <= decodesBeforeReturn + 1);

    // Tighten the cache to nothing: it empties once the cost pass runs.
    settings.ramCacheMB = 0;
    manager.setSettings(settings);
    QTRY_COMPARE_WITH_TIMEOUT(manager.stats().lodRamCacheMB, 0.0, 5000);
}

QTEST_MAIN(TestLodManager)

#include "test_lod_manager.moc"
