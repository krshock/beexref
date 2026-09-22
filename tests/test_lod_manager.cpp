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

QTEST_MAIN(TestLodManager)

#include "test_lod_manager.moc"
