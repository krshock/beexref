#include <QBuffer>
#include <QImage>
#include <QtTest>

#include "doc/document.h"
#include "doc/item.h"
#include "doc/source.h"
#include "doc/undo.h"
#include "ui/layout_ops.h"
#include "ui/scene.h"
#include "ui/scene_item.h"

using ui::SceneItem;

namespace {

doc::ItemPtr pixmapItem(int width, int height, const QString &filename = {}, qint64 id = 0)
{
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->filename = filename;
    item->id = id;
    item->setOriginalSize(QSize(width, height));
    // Placeholders without a source count as error items, which layout
    // operations ignore, so give it something decodable.
    QImage pixel(1, 1, QImage::Format_ARGB32);
    pixel.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixel.save(&buffer, "PNG");
    item->source = std::make_shared<doc::BytesSource>(bytes);
    item->format = QStringLiteral("png");
    return item;
}

// The scene bounding box of a view, which is what arranging moves.
QRectF boundsOf(SceneItem *view)
{
    return view->sceneBoundingRect();
}

} // namespace

class TestLayoutOps : public QObject
{
    Q_OBJECT

private slots:
    void orderedSelectionFollowsTheReference();
    void normalizeHeightWidthAndSize();
    void normalizeNeedsTwoItems();
    void arrangeHorizontalAndVertical();
    void arrangeSquareCentresTheGrid();
    void arrangeModeFromSettingValues();
};

void TestLayoutOps::orderedSelectionFollowsTheReference()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr namedB = pixmapItem(10, 10, QStringLiteral("b.png"));
    const doc::ItemPtr namedA = pixmapItem(10, 10, QStringLiteral("a.png"));
    const doc::ItemPtr id7 = pixmapItem(10, 10, QString(), 7);
    const doc::ItemPtr id3 = pixmapItem(10, 10, QString(), 3);
    const doc::ItemPtr plain = pixmapItem(10, 10);
    for (const doc::ItemPtr &item : {namedB, namedA, id7, id3, plain})
        document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    for (SceneItem *view : scene.itemViews())
        view->setSelected(true);

    const QVector<SceneItem *> ordered = ui::layout::orderedSelection(scene);
    QCOMPARE(ordered.size(), 5);
    // Filenames first (alphabetical), then save ids, then insertion
    // order.
    QCOMPARE(ordered.at(0)->item(), namedA);
    QCOMPARE(ordered.at(1)->item(), namedB);
    QCOMPARE(ordered.at(2)->item(), id3);
    QCOMPARE(ordered.at(3)->item(), id7);
    QCOMPARE(ordered.at(4)->item(), plain);
}

void TestLayoutOps::normalizeHeightWidthAndSize()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr first = pixmapItem(100, 50);
    first->x = 0;
    first->y = 0;
    const doc::ItemPtr second = pixmapItem(200, 100);
    second->x = 400;
    second->y = 0;
    const doc::ItemPtr third = pixmapItem(50, 50);
    third->x = 0;
    third->y = 400;
    for (const doc::ItemPtr &item : {first, second, third})
        document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    for (SceneItem *view : scene.itemViews())
        view->setSelected(true);

    const QPointF centreBefore = boundsOf(scene.itemViews().first()).center();
    ui::layout::normalize(scene, stack, ui::layout::Normalize::Height);
    const QVector<SceneItem *> views = scene.itemViews();

    // Every box now has the average height, around its own centre.
    const double average = (50.0 + 100.0 + 50.0) / 3.0;
    for (SceneItem *view : views)
        QVERIFY(std::abs(boundsOf(view).height() - average) < 1.0e-9);
    QVERIFY(std::abs(boundsOf(views.at(0)).center().x() - centreBefore.x()) < 1.0e-9);
    QVERIFY(std::abs(boundsOf(views.at(0)).center().y() - centreBefore.y()) < 1.0e-9);
    QCOMPARE(stack.count(), 1);

    QVERIFY(stack.undo());
    scene.syncDocument();
    QVERIFY(std::abs(boundsOf(views.at(0)).height() - 50.0) < 1.0e-9);
    QVERIFY(std::abs(boundsOf(views.at(1)).height() - 100.0) < 1.0e-9);

    // Width.
    ui::layout::normalize(scene, stack, ui::layout::Normalize::Width);
    const double averageWidth = (100.0 + 200.0 + 50.0) / 3.0;
    for (SceneItem *view : scene.itemViews())
        QVERIFY(std::abs(boundsOf(view).width() - averageWidth) < 1.0e-9);
    QVERIFY(stack.undo());
    scene.syncDocument();

    // Size means equal area.
    ui::layout::normalize(scene, stack, ui::layout::Normalize::Size);
    const double averageArea = (100.0 * 50 + 200.0 * 100 + 50.0 * 50) / 3.0;
    for (SceneItem *view : scene.itemViews()) {
        const QRectF box = boundsOf(view);
        QVERIFY(std::abs(box.width() * box.height() - averageArea) < 1.0e-6);
    }
    QVERIFY(stack.undo());
    scene.syncDocument();
    QVERIFY(std::abs(boundsOf(scene.itemViews().at(1)).width() - 200.0) < 1.0e-9);
}

void TestLayoutOps::normalizeNeedsTwoItems()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(100, 50));

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    scene.itemViews().first()->setSelected(true);

    ui::layout::normalize(scene, stack, ui::layout::Normalize::Height);
    QCOMPARE(stack.count(), 0);
}

void TestLayoutOps::arrangeHorizontalAndVertical()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr wide = pixmapItem(200, 100, QStringLiteral("b.png"));
    wide->x = 100;
    wide->y = 0;
    const doc::ItemPtr small = pixmapItem(50, 50, QStringLiteral("c.png"));
    small->x = 200;
    small->y = 100;
    const doc::ItemPtr tall = pixmapItem(100, 50, QStringLiteral("a.png"));
    tall->x = 300;
    tall->y = 50;
    for (const doc::ItemPtr &item : {wide, small, tall})
        document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    for (SceneItem *view : scene.itemViews())
        view->setSelected(true);

    SceneItem *wideView = scene.itemViewFor(wide);
    SceneItem *smallView = scene.itemViewFor(small);
    SceneItem *tallView = scene.itemViewFor(tall);
    QVERIFY(wideView && smallView && tallView);

    // The row is compacted along the current positions: b (x=100),
    // c (x=200), a (x=300), centred on the old selection centre.
    ui::layout::arrange(scene, stack, ui::layout::Arrange::Horizontal, 0);
    QCOMPARE(boundsOf(wideView).topLeft(), QPointF(75, 25));   // b, 200x100
    QCOMPARE(boundsOf(smallView).topLeft(), QPointF(275, 50));  // c, 50x50
    QCOMPARE(boundsOf(tallView).topLeft(), QPointF(325, 50));  // a, 100x50
    QCOMPARE(stack.count(), 1);

    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(boundsOf(wideView).topLeft(), QPointF(100, 0));

    // With a gap the items are spaced apart.
    ui::layout::arrange(scene, stack, ui::layout::Arrange::Horizontal, 20);
    QCOMPARE(boundsOf(wideView).topLeft(), QPointF(75, 25));
    QCOMPARE(boundsOf(smallView).topLeft(), QPointF(295, 50));
    QCOMPARE(boundsOf(tallView).topLeft(), QPointF(365, 50));
    QVERIFY(stack.undo());
    scene.syncDocument();

    // Vertical: compacted along the current y order: b (y=0),
    // a (y=50), c (y=100).
    ui::layout::arrange(scene, stack, ui::layout::Arrange::Vertical, 0);
    QCOMPARE(boundsOf(wideView).topLeft(), QPointF(150, -25)); // b
    QCOMPARE(boundsOf(tallView).topLeft(), QPointF(200, 75));  // a
    QCOMPARE(boundsOf(smallView).topLeft(), QPointF(225, 125)); // c
    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(boundsOf(wideView).topLeft(), QPointF(100, 0));
}

void TestLayoutOps::arrangeSquareCentresTheGrid()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr a = pixmapItem(100, 50, QStringLiteral("a.png"));
    const doc::ItemPtr b = pixmapItem(50, 100, QStringLiteral("b.png"));
    const doc::ItemPtr c = pixmapItem(80, 80, QStringLiteral("c.png"));
    const doc::ItemPtr d = pixmapItem(40, 40, QStringLiteral("d.png"));
    for (const doc::ItemPtr &item : {a, b, c, d}) {
        item->x = 0;
        item->y = 0;
        document->addItem(item);
    }

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    for (SceneItem *view : scene.itemViews())
        view->setSelected(true);

    // The selection centre starts at (50, 50); a 2x2 grid of 100x100
    // cells is centred on it, items centred in their cells, in
    // filename order.
    ui::layout::arrange(scene, stack, ui::layout::Arrange::Square, 0);
    SceneItem *aView = scene.itemViewFor(a);
    SceneItem *bView = scene.itemViewFor(b);
    SceneItem *cView = scene.itemViewFor(c);
    SceneItem *dView = scene.itemViewFor(d);
    QVERIFY(aView && bView && cView && dView);
    QCOMPARE(boundsOf(aView).topLeft(), QPointF(-50, -25)); // a
    QCOMPARE(boundsOf(bView).topLeft(), QPointF(75, -50));  // b
    QCOMPARE(boundsOf(cView).topLeft(), QPointF(-40, 60));  // c
    QCOMPARE(boundsOf(dView).topLeft(), QPointF(80, 80));   // d

    // The 2x2 cell grid spans (-50,-50) to (150,150), so its centre is
    // the previous selection centre (50, 50); each item sits centred in
    // its cell.
    QCOMPARE(boundsOf(aView).center(), QPointF(0, 0));
    QCOMPARE(boundsOf(bView).center(), QPointF(100, 0));
    QCOMPARE(boundsOf(cView).center(), QPointF(0, 100));
    QCOMPARE(boundsOf(dView).center(), QPointF(100, 100));
    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(boundsOf(aView).topLeft(), QPointF(0, 0));
}

void TestLayoutOps::arrangeModeFromSettingValues()
{
    using ui::layout::Arrange;
    QCOMPARE(ui::layout::arrangeModeFromSetting(QStringLiteral("horizontal")), Arrange::Horizontal);
    QCOMPARE(ui::layout::arrangeModeFromSetting(QStringLiteral("Vertical")), Arrange::Vertical);
    QCOMPARE(ui::layout::arrangeModeFromSetting(QStringLiteral("square")), Arrange::Square);
    // Optimal packing is not ported; the default falls back to square.
    QCOMPARE(ui::layout::arrangeModeFromSetting(QStringLiteral("optimal")), Arrange::Square);
    QCOMPARE(ui::layout::arrangeModeFromSetting(QString()), Arrange::Square);
    QCOMPARE(ui::layout::arrangeModeFromSetting(QStringLiteral("banana")), Arrange::Square);
}

QTEST_MAIN(TestLayoutOps)

#include "test_layout_ops.moc"
