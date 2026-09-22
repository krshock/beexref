#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>
#include <QWheelEvent>

#include "doc/document.h"
#include "doc/item.h"
#include "doc/source.h"
#include "doc/undo.h"
#include "settings.h"
#include "ui/input_controller.h"
#include "ui/main_window.h"
#include "ui/rendering.h"
#include "ui/scene.h"
#include "ui/scene_item.h"
#include "ui/view.h"

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

doc::ItemPtr pixmapItem(int width, int height, const QColor &color)
{
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->source = std::make_shared<doc::BytesSource>(makePng(width, height, color));
    item->format = QStringLiteral("png");
    item->setOriginalSize(QSize(width, height));
    return item;
}

doc::ItemPtr textItem(const QString &text)
{
    auto item = std::make_shared<doc::Item>(doc::kTypeText);
    item->setText(text);
    return item;
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &position, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(position), QPointF(widget->mapToGlobal(position)), button,
                      buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void sendWheel(QWidget *widget, const QPoint &position, int delta, Qt::KeyboardModifiers modifiers)
{
    QWheelEvent event(QPointF(position), QPointF(widget->mapToGlobal(position)), QPoint(),
                      QPoint(0, delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
}

} // namespace

class TestUiScene : public QObject
{
    Q_OBJECT

private slots:
    void buildsItemsFromDocument();
    void floorBecomesPlaceholderLevel();
    void missingSourceIsErrorItem();
    void selectionBoundsCoversSelectedItems();
    void dragMovesItemAndModel();
    void fitSceneFramesTheItems();
    void zoomLimitsHold();
    void middleDragPansWithTheCursor();
    void wheelPanAxesMatchReference();
    void windowOpensBoardAndLoadsLevel();
    void newWindowHasUnsavedDocument();
    void smoothingSuspendsDuringInteraction();
    void sceneRectLeavesRoomToPan();
    void pansBeyondTheItemsWhenZoomedIn();
};

void TestUiScene::buildsItemsFromDocument()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(300, 200, Qt::red));
    document->addItem(textItem(QStringLiteral("note")));

    ui::Scene scene;
    scene.setDocument(document);
    QCOMPARE(scene.itemViews().size(), 2);
    QCOMPARE(scene.pixmapItemViews().size(), 1);

    SceneItem *pixmap = scene.pixmapItemViews().first();
    QCOMPARE(pixmap->boundingRect(), QRectF(0, 0, 300, 200));
    QVERIFY(!pixmap->isError());

    SceneItem *text = nullptr;
    for (SceneItem *view : scene.itemViews()) {
        if (view->isText())
            text = view;
    }
    QVERIFY(text);
    QVERIFY(text->boundingRect().width() > 0);
}

void TestUiScene::floorBecomesPlaceholderLevel()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(400, 300, Qt::blue);
    item->floorData = makePng(100, 75, Qt::blue);
    item->floorFraction = 0.25;
    item->floorFormat = QStringLiteral("png");
    document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    SceneItem *view = scene.pixmapItemViews().first();
    QCOMPARE(view->levelFraction(), 0.25);
    QCOMPARE(view->level().size(), QSize(100, 75));
    // Local geometry stays the original size, not the level size.
    QCOMPARE(view->boundingRect(), QRectF(0, 0, 400, 300));
}

void TestUiScene::missingSourceIsErrorItem()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->setOriginalSize(QSize(200, 100));
    document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    SceneItem *view = scene.pixmapItemViews().first();
    QVERIFY(view->isError());
    QVERIFY(view->levelUnavailable());
    QVERIFY(view->boundingRect().height() > 0);
}

void TestUiScene::selectionBoundsCoversSelectedItems()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr first = pixmapItem(100, 100, Qt::red);
    first->x = 0;
    first->y = 0;
    const doc::ItemPtr second = pixmapItem(100, 100, Qt::blue);
    second->x = 200;
    second->y = 50;
    document->addItem(first);
    document->addItem(second);

    ui::Scene scene;
    scene.setDocument(document);
    for (SceneItem *view : scene.itemViews())
        view->setSelected(true);
    QCOMPARE(scene.selectionBounds(), QRectF(0, 0, 300, 150));
}

void TestUiScene::dragMovesItemAndModel()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(100, 50, Qt::green);
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    doc::UndoStack stack(document.get());
    view.setUndoStack(&stack);
    view.resize(400, 300);
    view.fitScene();

    SceneItem *viewItem = scene->pixmapItemViews().first();
    const QPoint start = view.mapFromScene(viewItem->sceneBoundingRect().center());
    sendMouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    QVERIFY(viewItem->isSelected());

    sendMouse(view.viewport(), QEvent::MouseMove, start + QPoint(6, 0), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, start + QPoint(40, 10), Qt::NoButton,
              Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, start + QPoint(40, 10), Qt::LeftButton,
              Qt::NoButton);

    QVERIFY(item->x > 0);
    QCOMPARE(viewItem->pos().x(), item->x);
    QCOMPARE(viewItem->pos().y(), item->y);
    QVERIFY(document->isModified());

    // The whole drag is one undo step.
    QVERIFY(stack.canUndo());
    QVERIFY(stack.undo());
    scene->syncDocument();
    QCOMPARE(item->x, 0.0);
    QCOMPARE(item->y, 0.0);
    QCOMPARE(viewItem->pos().x(), 0.0);
    QVERIFY(stack.redo());
    scene->syncDocument();
    QVERIFY(item->x > 0);
    QCOMPARE(viewItem->pos().x(), item->x);
}

void TestUiScene::fitSceneFramesTheItems()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(2000, 1000, Qt::yellow));

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(400, 300);
    view.fitScene();

    const QRect viewRect = view.viewport()->rect();
    const QRect mapped = view.mapFromScene(scene->itemsBoundingRect()).boundingRect();
    QVERIFY(viewRect.contains(mapped.center()));
    QVERIFY(mapped.width() <= viewRect.width() + 1);
    QVERIFY(mapped.height() <= viewRect.height() + 1);
}

void TestUiScene::zoomLimitsHold()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(100, 100, Qt::cyan));

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(400, 300);
    view.fitScene();

    const double before = view.transform().m11();
    view.zoomAt(-100000, view.viewport()->rect().center());
    const double zoomedOut = view.transform().m11();
    QVERIFY(zoomedOut <= before);
    QVERIFY(zoomedOut > 0);

    view.zoomAt(100000, view.viewport()->rect().center());
    const double zoomedIn = view.transform().m11();
    QVERIFY(zoomedIn >= zoomedOut);
}

void TestUiScene::middleDragPansWithTheCursor()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(2000, 1000, Qt::darkGreen));

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(200, 150);
    view.fitScene();
    view.zoomAt(500, view.viewport()->rect().center());
    view.centerOn(1000, 500);

    const QPointF scenePoint(500, 250);
    const QPoint before = view.mapFromScene(scenePoint);
    const int beforeValue = view.horizontalScrollBar()->value();

    const QPoint start = view.viewport()->rect().center();
    sendMouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::MiddleButton,
              Qt::MiddleButton);
    sendMouse(view.viewport(), QEvent::MouseMove, start + QPoint(40, 0), Qt::NoButton,
              Qt::MiddleButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, start + QPoint(40, 0), Qt::MiddleButton,
              Qt::NoButton);

    // The canvas follows the cursor: a fixed scene point moves right on
    // screen and the scrollbar value decreases.
    const QPoint after = view.mapFromScene(scenePoint);
    QVERIFY2(after.x() > before.x() + 30,
             qPrintable(QStringLiteral("before=%1 after=%2").arg(before.x()).arg(after.x())));
    QVERIFY(view.horizontalScrollBar()->value() < beforeValue);
}

void TestUiScene::wheelPanAxesMatchReference()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(2000, 1000, Qt::darkMagenta));

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(200, 150);
    view.fitScene();
    view.zoomAt(500, view.viewport()->rect().center());
    view.centerOn(1000, 500);

    const QPoint center = view.viewport()->rect().center();

    // Reference quirk: Shift (pan_horizontal) drives the vertical
    // scrollbar, Shift+Ctrl (pan_vertical) drives the horizontal one.
    int horizontal = view.horizontalScrollBar()->value();
    int vertical = view.verticalScrollBar()->value();
    sendWheel(view.viewport(), center, 120, Qt::ShiftModifier);
    QCOMPARE(view.horizontalScrollBar()->value(), horizontal);
    QVERIFY(view.verticalScrollBar()->value() > vertical);

    horizontal = view.horizontalScrollBar()->value();
    vertical = view.verticalScrollBar()->value();
    sendWheel(view.viewport(), center, 120, Qt::ShiftModifier | Qt::ControlModifier);
    QVERIFY(view.horizontalScrollBar()->value() > horizontal);
    QCOMPARE(view.verticalScrollBar()->value(), vertical);
}

void TestUiScene::windowOpensBoardAndLoadsLevel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    settings::setSettingsDir(dir.path());

    const QString path = dir.filePath(QStringLiteral("board.beex"));
    {
        auto document = doc::Document::create();
        document.addItem(pixmapItem(300, 200, Qt::darkRed));
        QVERIFY(document.save(path).isOk());
    }

    ui::MainWindow window;
    window.resize(400, 300);
    window.show();
    QVERIFY(window.openBoard(path));
    QCOMPARE(window.scene()->itemViews().size(), 1);
    QCOMPARE(window.scene()->pixmapItemViews().size(), 1);

    SceneItem *view = window.scene()->pixmapItemViews().first();
    QTRY_VERIFY_WITH_TIMEOUT(!view->level().isNull(), 5000);
    QVERIFY(view->levelFraction() > 0.0);
    QVERIFY(view->levelFraction() <= 1.0);
    QVERIFY(!view->levelUnavailable());

    // Title diagnostics: name plus RAM and, for saved .beex boards, the
    // file size, as the reference shows them.
    const QString title = window.windowTitle();
    QVERIFY2(title.contains(QStringLiteral("RAM ")), qPrintable(title));
    QVERIFY2(title.contains(QStringLiteral("board.beex")), qPrintable(title));
    QVERIFY2(title.contains(QStringLiteral("|")), qPrintable(title));

    settings::setSettingsDir(QString());
}

void TestUiScene::newWindowHasUnsavedDocument()
{
    // A window always has a document, so paste, drops and undo/redo
    // work before any file is opened.
    ui::MainWindow window;
    QVERIFY(window.scene());
    QCOMPARE(window.scene()->itemViews().size(), 0);
    QVERIFY(window.scene()->document());

    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));

    QCOMPARE(window.scene()->pixmapItemViews().size(), 1);
    QVERIFY(window.scene()->document()->isModified());
}

void TestUiScene::smoothingSuspendsDuringInteraction()
{
    // Repaints during drag/pan/zoom skip the bilinear filter and the
    // filter returns when the input settles, as in the reference.
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(2000, 1000, Qt::gray));

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(400, 300);
    view.fitScene();

    // Test hygiene: a view destroyed right after a wheel event can
    // leave the transient flag set, since its restore timer dies with
    // it. The app's single view lives on, so this is test-only.
    ui::rendering::setSmoothingSuspended(false);
    QVERIFY(!ui::rendering::smoothingSuspended());

    sendWheel(view.viewport(), view.viewport()->rect().center(), 120, Qt::NoModifier);
    QVERIFY(ui::rendering::smoothingSuspended());
    QTRY_VERIFY_WITH_TIMEOUT(!ui::rendering::smoothingSuspended(), 2000);

    const QPoint center = view.viewport()->rect().center();
    sendMouse(view.viewport(), QEvent::MouseButtonPress, center, Qt::MiddleButton,
              Qt::MiddleButton);
    QVERIFY(ui::rendering::smoothingSuspended());
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, center, Qt::MiddleButton,
              Qt::NoButton);
    QVERIFY(!ui::rendering::smoothingSuspended());
}

void TestUiScene::sceneRectLeavesRoomToPan()
{
    // The reference expands the scrollable area by one viewport per
    // side, so panning works even when every item is on screen.
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(100, 50, Qt::red));

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(400, 300);
    view.fitScene();

    const QRectF items = scene->itemsBoundingRect();
    const QRectF rect = scene->sceneRect();
    QVERIFY(rect.width() > items.width());
    QVERIFY(rect.height() > items.height());
    QVERIFY(rect.contains(items));

    // The scrollbars have range, so a middle drag can move the view.
    QVERIFY(view.horizontalScrollBar()->maximum() > 0);
    const int before = view.horizontalScrollBar()->value();
    const QPoint start = view.viewport()->rect().center();
    sendMouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::MiddleButton,
              Qt::MiddleButton);
    sendMouse(view.viewport(), QEvent::MouseMove, start - QPoint(60, 0), Qt::NoButton,
              Qt::MiddleButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, start - QPoint(60, 0), Qt::MiddleButton,
              Qt::NoButton);
    QVERIFY(view.horizontalScrollBar()->value() > before);
}

void TestUiScene::pansBeyondTheItemsWhenZoomedIn()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    document->addItem(pixmapItem(2000, 1000, Qt::darkBlue));

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(400, 300);
    view.fitScene();
    view.zoomAt(800, view.viewport()->rect().center());

    // Pan to the scroll limit; the viewport centre may then leave the
    // items' bounding box, because the scene rect extends a full
    // viewport beyond it.
    const QPoint start = view.viewport()->rect().center();
    for (int i = 0; i < 8; ++i) {
        sendMouse(view.viewport(), QEvent::MouseButtonPress, start, Qt::MiddleButton,
                  Qt::MiddleButton);
        sendMouse(view.viewport(), QEvent::MouseMove, start - QPoint(300, 0), Qt::NoButton,
                  Qt::MiddleButton);
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, start - QPoint(300, 0),
                  Qt::MiddleButton, Qt::NoButton);
    }

    const QPointF centre = view.mapToScene(view.viewport()->rect().center());
    QVERIFY2(centre.x() > scene->itemsBoundingRect().right(),
             qPrintable(QStringLiteral("centre=%1 right=%2")
                            .arg(centre.x())
                            .arg(scene->itemsBoundingRect().right())));
}

QTEST_MAIN(TestUiScene)

#include "test_ui_scene.moc"
