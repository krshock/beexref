#include <QAction>
#include <QFile>
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QMimeData>
#include <QDialog>
#include <QMouseEvent>
#include <QScrollBar>
#include <QCheckBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>
#include <QWheelEvent>

#include <cmath>

#include "doc/document.h"
#include "doc/item.h"
#include "doc/source.h"
#include "doc/undo.h"
#include "constants.h"
#include "settings.h"
#include "ui/input_controller.h"
#include "ui/main_window.h"
#include "ui/color_gamut.h"
#include "ui/hud.h"
#include "ui/settings_dialog.h"
#include "ui/grayscale.h"
#include "ui/opacity_dialog.h"
#include "ui/rendering.h"
#include "ui/selection_ops.h"
#include "ui/scene.h"
#include "ui/scene_item.h"
#include "ui/theme.h"
#include "ui/view.h"

#include "settings.h"

using ui::SceneItem;

namespace {

// Captures the regions Qt asks the viewport to repaint. The selection
// overlay's output is not tracked by the scene, so this is what proves
// the old outline and handles get erased instead of trailing.
class PaintRegionSpy : public QObject
{
public:
    QRegion region;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) {
            if (auto *paint = static_cast<QPaintEvent *>(event))
                region += paint->region();
        }
        return QObject::eventFilter(watched, event);
    }
};

// Paints one canvas item on its own, so tests can inspect the actual
// pixels without a view or a scene.
QImage renderItem(SceneItem *view, const QSize &size)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(ui::theme::canvas);
    QPainter painter(&image);
    view->paint(&painter, nullptr, nullptr);
    painter.end();
    return image;
}

QAction *actionByText(ui::MainWindow &window, const QString &text)
{
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
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
    void scaleHandleScalesAroundOppositeCorner();
    void rotateGestureRotatesAndUndoes();
    void flipActionMirrorsAroundTheCentre();
    void movingAnItemDoesNotScrollTheView();
    void movingAnItemRepaintsTheSelectionHandles();
    void grayscaleImageFlattensOntoTheCanvas();
    void grayscaleIsPaintedAndUndone();
    void grayscaleLevelsRebuildWhenTheLevelChanges();
    void opacityAppliesToImagesOnlyAndUndoes();
    void opacityDialogReportsAndEmits();
    void grayscaleActionFollowsTheSelection();
    void cropShrinksTheItemToTheCrop();
    void cropModeDragsAndConfirms();
    void cropModeCancelPaths();
    void resetCropAndTransforms();
    void cropActionStartsAndUndoCancels();
    void resetActionsAreSingleUndoSteps();
    void doubleClickFitsTheItem();
    void sampleColorReadsTheDisplayedPixel();
    void sampleModeCopiesTheColorUnderThePointer();
    void gamutDialogCanCloseWhileCounting();
    void gamutPlotFiltersDotsByThreshold();
    void gamutDialogSliderUpdatesContinuously();
    void actionsFollowTheSelectionState();
    void selectAllAndDeselectAll();
    void deleteSelectionUndoes();
    void raiseAndLowerChangeZOrder();
    void newSceneClearsTheBoard();
    void hudToastsAppearAndExpire();
    void settingsDialogWritesAndRestores();
    void settingsActionOpensTheDialog();
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

void TestUiScene::scaleHandleScalesAroundOppositeCorner()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(200, 100, Qt::red);
    item->x = 0;
    item->y = 0;
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    doc::UndoStack stack(document.get());
    view.setUndoStack(&stack);
    view.resize(600, 400);
    view.fitScene();

    SceneItem *viewItem = scene->pixmapItemViews().first();
    viewItem->setSelected(true);
    QCOMPARE(scene->selectionBounds(), QRectF(0, 0, 200, 100));

    // Press a little inside the bottom-right corner and drag outward.
    const QPoint press = view.mapFromScene(QPointF(197, 97));
    const QPoint move = view.mapFromScene(QPointF(320, 197));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, move, Qt::NoButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, move, Qt::LeftButton, Qt::NoButton);

    QVERIFY2(item->scale > 1.0, qPrintable(QStringLiteral("scale=%1").arg(item->scale)));
    // The opposite (top-left) corner stays put.
    QCOMPARE(item->x, 0.0);
    QCOMPARE(item->y, 0.0);
    QVERIFY(stack.canUndo());

    QVERIFY(stack.undo());
    scene->syncDocument();
    QCOMPARE(item->scale, 1.0);
    QCOMPARE(item->x, 0.0);
}

void TestUiScene::rotateGestureRotatesAndUndoes()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(200, 100, Qt::green);
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    doc::UndoStack stack(document.get());
    view.setUndoStack(&stack);
    view.resize(600, 400);
    view.fitScene();

    SceneItem *viewItem = scene->pixmapItemViews().first();
    viewItem->setSelected(true);

    // The rotation band sits between 10 and 20 device pixels outside
    // the corner; convert that to scene units for the actual zoom.
    const double viewScale = view.transform().m11();
    const QPointF unit(1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0));
    const QPointF pressScene =
        QPointF(200, 100) + unit * (15.0 / viewScale);
    const QPoint press = view.mapFromScene(pressScene);
    const QPoint move = view.mapFromScene(QPointF(240, 60));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, move, Qt::NoButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, move, Qt::LeftButton, Qt::NoButton);

    QVERIFY2(std::abs(item->rotation) > 1.0,
             qPrintable(QStringLiteral("rotation=%1").arg(item->rotation)));
    QVERIFY(stack.canUndo());
    QVERIFY(stack.undo());
    scene->syncDocument();
    QCOMPARE(item->rotation, 0.0);
}

void TestUiScene::flipActionMirrorsAroundTheCentre()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(200, 100, Qt::blue);
    document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    SceneItem *view = scene.pixmapItemViews().first();
    view->setSelected(true);

    ui::selection::flip(scene, stack, false);
    QCOMPARE(item->flip, -1.0);
    QVERIFY(stack.undo());
    QCOMPARE(item->flip, 1.0);

    ui::selection::flip(scene, stack, true);
    QCOMPARE(item->flip, -1.0);
    QCOMPARE(item->rotation, 180.0);
    QVERIFY(stack.undo());
    QCOMPARE(item->rotation, 0.0);
}

void TestUiScene::movingAnItemDoesNotScrollTheView()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(200, 100, Qt::darkRed);
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(400, 300);
    view.fitScene();

    const QPointF centreBefore = view.mapToScene(view.viewport()->rect().center());

    SceneItem *viewItem = scene->pixmapItemViews().first();
    const QPoint itemCentre = view.mapFromScene(viewItem->sceneBoundingRect().center());
    sendMouse(view.viewport(), QEvent::MouseButtonPress, itemCentre, Qt::LeftButton,
              Qt::LeftButton);
    // Many small steps: a per-event view shift would compound here.
    for (int step = 1; step <= 30; ++step) {
        sendMouse(view.viewport(), QEvent::MouseMove, itemCentre + QPoint(step * 2, step),
                  Qt::NoButton, Qt::LeftButton);
    }
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, itemCentre + QPoint(60, 30),
              Qt::LeftButton, Qt::NoButton);

    // Dragging an item must not pan the canvas: the visible centre may
    // only move by scrollbar rounding, under half a device pixel.
    const QPointF centreAfter = view.mapToScene(view.viewport()->rect().center());
    const double scale = view.transform().m11();
    const double driftX = std::abs(centreAfter.x() - centreBefore.x()) * scale;
    const double driftY = std::abs(centreAfter.y() - centreBefore.y()) * scale;
    QVERIFY2(driftX <= 0.5 + 1.0e-6 && driftY <= 0.5 + 1.0e-6,
             qPrintable(QStringLiteral("view drifted by %1,%2 device px")
                            .arg(driftX)
                            .arg(driftY)));
}

void TestUiScene::movingAnItemRepaintsTheSelectionHandles()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    // Rotated 45 degrees: the selection bounds, and with them the handle
    // dots, then reach well outside the item's own painting area, which
    // is what makes a missed repaint visible as a trail.
    const doc::ItemPtr item = pixmapItem(100, 50, Qt::darkBlue);
    item->rotation = 45.0;
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(600, 400);
    // Paint events only reach a visible widget.
    view.show();
    QTest::qWait(200);
    view.setTransform(QTransform::fromScale(2.0, 2.0));
    view.centerOn(QPointF(50, 25));
    QTest::qWait(200);

    SceneItem *viewItem = scene->pixmapItemViews().first();
    viewItem->setSelected(true);
    QTest::qWait(20);

    // A point four device pixels outside the bottom-right corner of the
    // selection bounds: the handle dot covers it, the item does not.
    const QPoint corner = view.mapFromScene(scene->selectionBounds().bottomRight());
    const QPoint oldHandle = corner + QPoint(4, 4);

    PaintRegionSpy spy;
    view.viewport()->installEventFilter(&spy);

    const QPoint grab = view.mapFromScene(QPointF(50, 25));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 8; ++step) {
        sendMouse(view.viewport(), QEvent::MouseMove, grab + QPoint(step * 10, step * 5),
                  Qt::NoButton, Qt::LeftButton);
    }
    // Flush the repaint the drag asked for, before the release grows
    // the scrollable rect (which repaints everything).
    QCoreApplication::processEvents();
    view.viewport()->removeEventFilter(&spy);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, grab + QPoint(80, 40), Qt::LeftButton,
              Qt::NoButton);
    QTest::qWait(20);

    QVERIFY2(spy.region.contains(oldHandle),
             qPrintable(QStringLiteral("old handle at %1,%2 was not repainted")
                            .arg(oldHandle.x())
                            .arg(oldHandle.y())));
    // A targeted repaint, not a full viewport refresh: the far corner
    // was never part of the item or its overlay.
    QVERIFY2(!spy.region.contains(QPoint(5, 5)), "the whole viewport was repainted");
}

void TestUiScene::grayscaleImageFlattensOntoTheCanvas()
{
    QImage source(3, 2, QImage::Format_ARGB32);
    source.fill(Qt::transparent);
    source.setPixelColor(0, 0, QColor(255, 255, 255, 255)); // opaque white
    source.setPixelColor(1, 0, QColor(255, 0, 0, 255));     // opaque red
    source.setPixelColor(2, 0, QColor(0, 0, 255, 0));       // transparent blue
    source.setPixelColor(0, 1, QColor(255, 255, 255, 128)); // half white
    source.setPixelColor(1, 1, QColor(0, 0, 0, 255));       // opaque black

    const QImage gray = ui::grayscaleImage(source);
    QCOMPARE(gray.size(), source.size());
    QCOMPARE(gray.format(), QImage::Format_Grayscale8);

    const int canvas = qGray(ui::theme::canvas.rgb());
    // Opaque white stays white, opaque black stays black.
    QCOMPARE(gray.pixelColor(0, 0).red(), 255);
    QCOMPARE(gray.pixelColor(1, 1).red(), 0);

    // Transparent pixels take the canvas colour: the reference fills
    // the grayscale image with it before drawing the colour image.
    QCOMPARE(gray.pixelColor(2, 0).red(), canvas);

    // A half transparent white blends from the canvas colour towards
    // white.
    const QColor blended = gray.pixelColor(0, 1);
    QVERIFY(blended.red() > canvas);
    QVERIFY(blended.red() < 255);

    // Colour becomes gray: red is darker than white and all channels
    // match.
    const QColor red = gray.pixelColor(1, 0);
    QCOMPARE(red.red(), red.green());
    QCOMPARE(red.green(), red.blue());
    QVERIFY(red.red() > 0);
    QVERIFY(red.red() < 255);
}

void TestUiScene::grayscaleIsPaintedAndUndone()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr image = pixmapItem(8, 8, Qt::red);
    document->addItem(image);

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    SceneItem *view = scene.pixmapItemViews().first();
    QImage level(8, 8, QImage::Format_ARGB32);
    level.fill(Qt::red);
    view->setLevel(level, 1.0);
    view->setSelected(true);

    QCOMPARE(renderItem(view, QSize(8, 8)).pixelColor(4, 4), QColor(Qt::red));

    ui::selection::setGrayscale(scene, stack, true);
    QVERIFY(image->grayscale());
    QCOMPARE(view->displayLevel().format(), QImage::Format_Grayscale8);
    const QColor gray = renderItem(view, QSize(8, 8)).pixelColor(4, 4);
    QCOMPARE(gray.red(), gray.green());
    QCOMPARE(gray.green(), gray.blue());
    QVERIFY(gray.red() < 255);
    QVERIFY(stack.canUndo());

    QVERIFY(stack.undo());
    scene.syncDocument();
    QVERIFY(!image->grayscale());
    QCOMPARE(view->displayLevel().format(), QImage::Format_ARGB32);
    QCOMPARE(renderItem(view, QSize(8, 8)).pixelColor(4, 4), QColor(Qt::red));
}

void TestUiScene::grayscaleLevelsRebuildWhenTheLevelChanges()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr image = pixmapItem(8, 8, Qt::red);
    document->addItem(image);

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    SceneItem *view = scene.pixmapItemViews().first();
    QImage red(8, 8, QImage::Format_ARGB32);
    red.fill(Qt::red);
    view->setLevel(red, 1.0);
    view->setSelected(true);
    ui::selection::setGrayscale(scene, stack, true);
    const int redGray = renderItem(view, QSize(8, 8)).pixelColor(4, 4).red();

    // A new level (the LOD manager swapping fractions) must be
    // converted too, not just the first one.
    QImage blue(8, 8, QImage::Format_ARGB32);
    blue.fill(Qt::blue);
    view->setLevel(blue, 0.5);
    QCOMPARE(view->displayLevel().format(), QImage::Format_Grayscale8);
    const QColor gray = renderItem(view, QSize(8, 8)).pixelColor(4, 4);
    QCOMPARE(gray.red(), gray.green());
    QCOMPARE(gray.green(), gray.blue());
    QVERIFY(gray.red() != redGray);
}

void TestUiScene::opacityAppliesToImagesOnlyAndUndoes()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr image = pixmapItem(8, 8, Qt::red);
    const doc::ItemPtr note = textItem(QStringLiteral("note"));
    document->addItem(image);
    document->addItem(note);

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    SceneItem *imageView = nullptr;
    for (SceneItem *view : scene.itemViews()) {
        view->setSelected(true);
        if (view->isPixmap())
            imageView = view;
    }
    QVERIFY(imageView);

    ui::selection::setOpacity(scene, stack, 0.4);
    QCOMPARE(image->opacity(), 0.4);
    QCOMPARE(imageView->opacity(), 0.4);
    // The reference's ChangeOpacity only touches images.
    QCOMPARE(note->opacity(), 1.0);
    QVERIFY(stack.canUndo());

    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(image->opacity(), 1.0);
    QCOMPARE(imageView->opacity(), 1.0);

    // The dialog's live preview changes the model without recording
    // history.
    const int entries = stack.count();
    ui::selection::applyOpacity(scene, 0.8);
    QCOMPARE(image->opacity(), 0.8);
    QCOMPARE(imageView->opacity(), 0.8);
    QCOMPARE(note->opacity(), 1.0);
    QCOMPARE(stack.count(), entries);

    // Applying the value it already has changes nothing and records
    // nothing.
    ui::selection::setOpacity(scene, stack, 0.8);
    QCOMPARE(stack.count(), entries);
}

void TestUiScene::opacityDialogReportsAndEmits()
{
    ui::OpacityDialog dialog(nullptr, 60);
    QCOMPARE(dialog.percent(), 60);

    QSignalSpy spy(&dialog, &ui::OpacityDialog::percentChanged);
    dialog.setPercent(35);
    QCOMPARE(dialog.percent(), 35);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 35);
}

void TestUiScene::grayscaleActionFollowsTheSelection()
{
    ui::MainWindow window;
    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));

    QAction *grayscale = nullptr;
    QAction *undo = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("&Grayscale"))
            grayscale = action;
        else if (action->text() == QStringLiteral("&Undo"))
            undo = action;
    }
    QVERIFY(grayscale);
    QVERIFY(undo);

    // Nothing selected: the action is off and disabled.
    window.scene()->clearSelection();
    QVERIFY(!grayscale->isEnabled());
    QVERIFY(!grayscale->isChecked());

    SceneItem *view = window.scene()->pixmapItemViews().first();
    view->setSelected(true);
    QVERIFY(grayscale->isEnabled());
    QVERIFY(!grayscale->isChecked());

    grayscale->trigger();
    QVERIFY(view->item()->grayscale());
    QVERIFY(window.scene()->document()->isModified());

    // The check mark follows undo.
    undo->trigger();
    QVERIFY(!view->item()->grayscale());
}

void TestUiScene::cropShrinksTheItemToTheCrop()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(4, 2, Qt::red);
    document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    SceneItem *view = scene.pixmapItemViews().first();

    // Distinct pixels, so the crop's source mapping is checkable.
    QImage level(4, 2, QImage::Format_ARGB32);
    level.setPixelColor(0, 0, Qt::red);
    level.setPixelColor(1, 0, Qt::green);
    level.setPixelColor(2, 0, Qt::blue);
    level.setPixelColor(3, 0, Qt::yellow);
    level.setPixelColor(0, 1, Qt::cyan);
    level.setPixelColor(1, 1, Qt::magenta);
    level.setPixelColor(2, 1, Qt::white);
    level.setPixelColor(3, 1, Qt::black);
    view->setLevel(level, 1.0);

    item->setCrop(QRectF(1, 0, 2, 2));
    scene.syncDocument();
    QCOMPARE(view->boundingRect(), QRectF(1, 0, 2, 2));
    QCOMPARE(view->transformOriginPoint(), QRectF(1, 0, 2, 2).center());

    // Painting starts at the crop's top-left.
    QImage render(2, 2, QImage::Format_ARGB32);
    render.fill(ui::theme::canvas);
    QPainter painter(&render);
    painter.translate(-view->boundingRect().topLeft());
    view->paint(&painter, nullptr, nullptr);
    painter.end();
    QCOMPARE(render.pixelColor(0, 0), QColor(Qt::green));
    QCOMPARE(render.pixelColor(1, 0), QColor(Qt::blue));
    QCOMPARE(render.pixelColor(0, 1), QColor(Qt::magenta));
    QCOMPARE(render.pixelColor(1, 1), QColor(Qt::white));
}

void TestUiScene::cropModeDragsAndConfirms()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(200, 100, Qt::red);
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    doc::UndoStack stack(document.get());
    view.setUndoStack(&stack);
    view.resize(600, 400);
    view.setTransform(QTransform::fromScale(2.0, 2.0));

    SceneItem *viewItem = scene->pixmapItemViews().first();
    viewItem->setSelected(true);
    view.cropSelection();
    QVERIFY(view.cropActive());
    QVERIFY(viewItem->cropMode());
    QCOMPARE(viewItem->cropRect(), QRectF(0, 0, 200, 100));

    // View scale 2: the handles are 7.5 item units. Drag the bottom-right
    // one in by 40x20 item units (80x40 device pixels).
    const QPointF handleCentre = viewItem->cropRect().bottomRight() - QPointF(3.75, 3.75);
    const QPoint press = view.mapFromScene(viewItem->mapToScene(handleCentre));
    const QPoint move = press + QPoint(-80, -40);
    sendMouse(view.viewport(), QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseMove, move, Qt::NoButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, move, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(viewItem->cropRect(), QRectF(0, 0, 160, 80));

    QTest::keyClick(&view, Qt::Key_Return);
    QVERIFY(!view.cropActive());
    QVERIFY(!viewItem->cropMode());
    QVERIFY(item->hasCrop());
    QCOMPARE(item->crop(), QRectF(0, 0, 160, 80));
    QCOMPARE(viewItem->boundingRect(), QRectF(0, 0, 160, 80));
    QVERIFY(stack.canUndo());

    QVERIFY(stack.undo());
    scene->syncDocument();
    QVERIFY(!item->hasCrop());
    QCOMPARE(viewItem->boundingRect(), QRectF(0, 0, 200, 100));
}

void TestUiScene::cropModeCancelPaths()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(200, 100, Qt::red);
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    doc::UndoStack stack(document.get());
    view.setUndoStack(&stack);
    view.resize(600, 400);
    view.setTransform(QTransform::fromScale(2.0, 2.0));

    SceneItem *viewItem = scene->pixmapItemViews().first();
    viewItem->setSelected(true);
    const auto dragBottomRightIn = [&]() {
        const QPointF handleCentre = viewItem->cropRect().bottomRight() - QPointF(3.75, 3.75);
        const QPoint press = view.mapFromScene(viewItem->mapToScene(handleCentre));
        const QPoint move = press + QPoint(-140, -140);
        sendMouse(view.viewport(), QEvent::MouseButtonPress, press, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(view.viewport(), QEvent::MouseMove, move, Qt::NoButton, Qt::LeftButton);
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, move, Qt::LeftButton,
                  Qt::NoButton);
    };

    // Escape cancels and leaves no history.
    view.cropSelection();
    dragBottomRightIn();
    QCOMPARE(viewItem->cropRect(), QRectF(0, 0, 130, 30));
    QTest::keyClick(&view, Qt::Key_Escape);
    QVERIFY(!view.cropActive());
    QVERIFY(!item->hasCrop());
    QCOMPARE(stack.count(), 0);

    // A click outside the rectangle cancels too.
    view.cropSelection();
    dragBottomRightIn();
    const QPoint outside = view.mapFromScene(viewItem->mapToScene(QPointF(150, 80)));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, outside, Qt::LeftButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!view.cropActive());
    QVERIFY(!item->hasCrop());
    QCOMPARE(stack.count(), 0);

    // A click inside confirms what was dragged.
    view.cropSelection();
    dragBottomRightIn();
    const QPoint inside = view.mapFromScene(viewItem->mapToScene(QPointF(30, 15)));
    sendMouse(view.viewport(), QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
    sendMouse(view.viewport(), QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!view.cropActive());
    QCOMPARE(item->crop(), QRectF(0, 0, 130, 30));
    QCOMPARE(stack.count(), 1);
}

void TestUiScene::resetCropAndTransforms()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(200, 100, Qt::red);
    item->setCrop(QRectF(10, 10, 50, 30));
    item->scale = 2.0;
    item->rotation = 30.0;
    item->flip = -1.0;
    document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    SceneItem *view = scene.pixmapItemViews().first();
    view->setSelected(true);

    // Reset Crop puts the whole image back and is undoable.
    ui::selection::resetCrop(scene, stack);
    QCOMPARE(item->crop(), QRectF(0, 0, 200, 100));
    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(item->crop(), QRectF(10, 10, 50, 30));

    // Reset All also resets the crop, like the reference.
    ui::selection::resetTransforms(scene, stack);
    QCOMPARE(item->crop(), QRectF(0, 0, 200, 100));
    QCOMPARE(item->scale, 1.0);
    QCOMPARE(item->rotation, 0.0);
    QCOMPARE(item->flip, 1.0);
    QVERIFY(stack.undo());
    scene.syncDocument();
    QCOMPARE(item->crop(), QRectF(10, 10, 50, 30));
    QCOMPARE(item->scale, 2.0);
    QCOMPARE(item->rotation, 30.0);
    QCOMPARE(item->flip, -1.0);
}

void TestUiScene::cropActionStartsAndUndoCancels()
{
    ui::MainWindow window;
    QImage image(20, 10, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));

    QAction *cropAction = nullptr;
    QAction *undo = nullptr;
    for (QAction *action : window.findChildren<QAction *>()) {
        if (action->text() == QStringLiteral("&Crop"))
            cropAction = action;
        else if (action->text() == QStringLiteral("&Undo"))
            undo = action;
    }
    QVERIFY(cropAction);
    QVERIFY(undo);

    SceneItem *view = window.scene()->pixmapItemViews().first();
    view->setSelected(true);
    cropAction->trigger();
    QVERIFY(window.view()->cropActive());
    QVERIFY(view->cropMode());

    // Undo cancels the crop session instead of editing under it.
    undo->trigger();
    QVERIFY(!window.view()->cropActive());
    QVERIFY(!view->cropMode());
}

void TestUiScene::resetActionsAreSingleUndoSteps()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    QVector<doc::ItemPtr> items;
    for (int i = 0; i < 3; ++i) {
        const doc::ItemPtr item = pixmapItem(40, 30, Qt::red);
        item->x = i * 100.0;
        item->scale = 2.0 + i;
        item->rotation = 15.0 * (i + 1);
        item->flip = -1.0;
        document->addItem(item);
        items.append(item);
    }

    ui::Scene scene;
    scene.setDocument(document);
    doc::UndoStack stack(document.get());
    for (SceneItem *view : scene.itemViews())
        view->setSelected(true);

    // Reset All is one history entry, and one undo brings every item
    // back: the reference pushes a single ResetTransforms command.
    ui::selection::resetTransforms(scene, stack);
    QCOMPARE(stack.count(), 1);
    for (const doc::ItemPtr &item : items) {
        QCOMPARE(item->scale, 1.0);
        QCOMPARE(item->rotation, 0.0);
        QCOMPARE(item->flip, 1.0);
    }
    QVERIFY(stack.undo());
    scene.syncDocument();
    for (int i = 0; i < items.size(); ++i) {
        QCOMPARE(items.at(i)->scale, 2.0 + i);
        QCOMPARE(items.at(i)->rotation, 15.0 * (i + 1));
        QCOMPARE(items.at(i)->flip, -1.0);
    }

    // Reset Scale: one entry for all three items.
    {
        doc::UndoStack fresh(document.get());
        ui::selection::resetScale(scene, fresh);
        QCOMPARE(fresh.count(), 1);
        for (const doc::ItemPtr &item : items)
            QCOMPARE(item->scale, 1.0);
        QVERIFY(fresh.undo());
        scene.syncDocument();
        for (int i = 0; i < items.size(); ++i)
            QCOMPARE(items.at(i)->scale, 2.0 + i);
    }

    // Reset Rotation: one entry as well.
    {
        doc::UndoStack fresh(document.get());
        ui::selection::resetRotation(scene, fresh);
        QCOMPARE(fresh.count(), 1);
        QVERIFY(fresh.undo());
        scene.syncDocument();
        for (int i = 0; i < items.size(); ++i)
            QCOMPARE(items.at(i)->rotation, 15.0 * (i + 1));
    }

    // Reset Flip: one entry, and nothing at all when every item is
    // already unflipped.
    {
        doc::UndoStack fresh(document.get());
        ui::selection::resetFlip(scene, fresh);
        QCOMPARE(fresh.count(), 1);
        QVERIFY(fresh.undo());
        scene.syncDocument();
        for (const doc::ItemPtr &item : items)
            QCOMPARE(item->flip, -1.0);
    }
    {
        for (const doc::ItemPtr &item : items)
            item->flip = 1.0;
        scene.syncDocument();
        doc::UndoStack fresh(document.get());
        ui::selection::resetFlip(scene, fresh);
        QCOMPARE(fresh.count(), 0);
    }
}

void TestUiScene::doubleClickFitsTheItem()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(40, 30, Qt::red);
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(600, 400);
    view.setTransform(QTransform::fromScale(0.5, 0.5));
    view.centerOn(QPointF(20, 15));

    SceneItem *viewItem = scene->pixmapItemViews().first();
    QVERIFY(!viewItem->isSelected());
    const double before = view.transform().m11();

    const QPoint centre = view.mapFromScene(viewItem->sceneBoundingRect().center());
    sendMouse(view.viewport(), QEvent::MouseButtonDblClick, centre, Qt::LeftButton,
              Qt::LeftButton);

    // The double-click selects the item and fits the view to it.
    QVERIFY(viewItem->isSelected());
    const double after = view.transform().m11();
    QVERIFY2(after > before, qPrintable(QStringLiteral("scale %1 -> %2").arg(before).arg(after)));

    const QRect mapped = view.mapFromScene(viewItem->sceneBoundingRect()).boundingRect();
    const QRect viewportRect = view.viewport()->rect();
    QVERIFY(mapped.width() <= viewportRect.width() + 1);
    QVERIFY(mapped.height() <= viewportRect.height() + 1);
    // fitInView leaves a small margin (a couple of pixels); the item
    // must still fill the viewport in one direction.
    QVERIFY(mapped.width() >= viewportRect.width() - 6
            || mapped.height() >= viewportRect.height() - 6);

    // Away from the item (in the fitting margin) the double-click
    // changes nothing.
    const double afterFit = view.transform().m11();
    QVERIFY(!mapped.contains(QPoint(0, 0)));
    sendMouse(view.viewport(), QEvent::MouseButtonDblClick, QPoint(0, 0), Qt::LeftButton,
              Qt::LeftButton);
    QCOMPARE(view.transform().m11(), afterFit);
}

void TestUiScene::sampleColorReadsTheDisplayedPixel()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(4, 2, Qt::red);
    document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    SceneItem *view = scene.pixmapItemViews().first();

    QImage level(4, 2, QImage::Format_ARGB32);
    level.setPixelColor(0, 0, QColor(255, 0, 0));
    level.setPixelColor(1, 0, QColor(0, 255, 0));
    level.setPixelColor(2, 0, QColor(0, 0, 255));
    level.setPixelColor(3, 0, QColor(255, 255, 0));
    level.setPixelColor(0, 1, QColor(0, 255, 255));
    level.setPixelColor(1, 1, QColor(255, 0, 255));
    level.setPixelColor(2, 1, QColor(255, 255, 255));
    level.setPixelColor(3, 1, QColor(0, 0, 0, 0)); // fully transparent
    view->setLevel(level, 1.0);

    QCOMPARE(view->sampleColorAt(view->mapToScene(QPointF(0.5, 0.5))), QColor(255, 0, 0));
    QCOMPARE(view->sampleColorAt(view->mapToScene(QPointF(1.5, 0.5))), QColor(0, 255, 0));
    QCOMPARE(view->sampleColorAt(view->mapToScene(QPointF(2.5, 1.5))), QColor(255, 255, 255));
    // A fully transparent pixel has no colour, and neither has a point
    // outside the item.
    QVERIFY(!view->sampleColorAt(view->mapToScene(QPointF(3.5, 1.5))).isValid());
    QVERIFY(!view->sampleColorAt(view->mapToScene(QPointF(4.5, 0.5))).isValid());

    // A coarser level is addressed through the fraction: local pixels
    // 0..3 map onto level pixels 0..1.
    QImage coarse(2, 1, QImage::Format_ARGB32);
    coarse.setPixelColor(0, 0, QColor(10, 20, 30));
    coarse.setPixelColor(1, 0, QColor(40, 50, 60));
    view->setLevel(coarse, 0.5);
    QCOMPARE(view->sampleColorAt(view->mapToScene(QPointF(0.5, 0.5))), QColor(10, 20, 30));
    QCOMPARE(view->sampleColorAt(view->mapToScene(QPointF(3.5, 0.5))), QColor(40, 50, 60));

    // Grayscale items sample their grey copy.
    item->setGrayscale(true);
    scene.syncDocument();
    const QColor grey = view->sampleColorAt(view->mapToScene(QPointF(0.5, 0.5)));
    QVERIFY(grey.isValid());
    QCOMPARE(grey.red(), grey.green());
    QCOMPARE(grey.green(), grey.blue());
    QCOMPARE(grey.alpha(), 255);
}

void TestUiScene::sampleModeCopiesTheColorUnderThePointer()
{
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(4, 2, Qt::red);
    document->addItem(item);

    ui::View view;
    auto *scene = new ui::Scene(&view);
    scene->setDocument(document);
    view.setBoardScene(scene);
    view.resize(600, 400);
    view.setTransform(QTransform::fromScale(2.0, 2.0));
    view.centerOn(QPointF(2, 1));

    SceneItem *viewItem = scene->pixmapItemViews().first();
    QImage level(4, 2, QImage::Format_ARGB32);
    level.fill(QColor(255, 0, 255));
    viewItem->setLevel(level, 1.0);

    QSignalSpy spy(&view, &ui::View::colorSampled);
    view.startSampleColor();
    QVERIFY(view.samplingColor());
    QCOMPARE(view.viewport()->cursor().shape(), Qt::CrossCursor);

    // The swatch follows the pointer with the colour under it.
    const QPoint centre = view.mapFromScene(viewItem->mapToScene(QPointF(2, 1)));
    sendMouse(view.viewport(), QEvent::MouseMove, centre, Qt::NoButton, Qt::NoButton);
    QCOMPARE(view.sampledColor(), QColor(255, 0, 255));

    // A left click reports it and leaves the mode.
    sendMouse(view.viewport(), QEvent::MouseButtonPress, centre, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().value<QColor>(), QColor(255, 0, 255));
    QVERIFY(!view.samplingColor());

    // Off the image there is no colour; clicking still leaves the mode
    // and reports nothing.
    view.startSampleColor();
    const QPoint empty(4, 4);
    sendMouse(view.viewport(), QEvent::MouseMove, empty, Qt::NoButton, Qt::NoButton);
    QVERIFY(!view.sampledColor().isValid());
    sendMouse(view.viewport(), QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!view.samplingColor());

    // Any key cancels the mode, like the reference.
    view.startSampleColor();
    QTest::keyClick(&view, Qt::Key_Escape);
    QVERIFY(!view.samplingColor());
}

void TestUiScene::gamutDialogCanCloseWhileCounting()
{
    // Destroying the dialog while the worker still counts must not
    // destroy a running thread.
    auto document = std::make_shared<doc::Document>(doc::Document::create());
    const doc::ItemPtr item = pixmapItem(64, 64, Qt::red);
    document->addItem(item);

    ui::Scene scene;
    scene.setDocument(document);
    SceneItem *view = scene.pixmapItemViews().first();
    QImage level(64, 64, QImage::Format_ARGB32);
    level.fill(Qt::red);
    view->setLevel(level, 1.0);

    {
        ui::GamutDialog dialog(nullptr, item, level);
        dialog.show();
    }
    QVERIFY(true);
}

void TestUiScene::gamutPlotFiltersDotsByThreshold()
{
    ui::GamutPlot plot;
    QHash<ui::colors::GamutKey, int> gamut;
    gamut.insert(ui::colors::GamutKey{0, 255}, 2);
    gamut.insert(ui::colors::GamutKey{120, 255}, 50);
    plot.setGamut(gamut);

    plot.setThreshold(20);
    QCOMPARE(plot.visibleDots(), 1);
    plot.setThreshold(1);
    QCOMPARE(plot.visibleDots(), 2);
    plot.setThreshold(100);
    QCOMPARE(plot.visibleDots(), 0);
    plot.setThreshold(50);
    QCOMPARE(plot.visibleDots(), 1);
}

void TestUiScene::gamutDialogSliderUpdatesContinuously()
{
    // The wheel follows the slider while it is dragged (the Go port's
    // behaviour), not only when the handle is released.
    ui::GamutDialog dialog(nullptr, doc::ItemPtr(), QImage(2, 1, QImage::Format_ARGB32));
    auto *slider = dialog.findChild<QSlider *>();
    QVERIFY(slider);
    QVERIFY(slider->hasTracking());
    QCOMPARE(slider->minimum(), 0);
    QCOMPARE(slider->maximum(), 500);
}

void TestUiScene::actionsFollowTheSelectionState()
{
    ui::MainWindow window;
    QAction *selectAllAction = actionByText(window, QStringLiteral("&Select All"));
    QAction *deleteAction = actionByText(window, QStringLiteral("&Delete"));
    QAction *cropAction = actionByText(window, QStringLiteral("&Crop"));
    QAction *gamutAction = actionByText(window, QStringLiteral("Show &Color Gamut"));
    QAction *sampleAction = actionByText(window, QStringLiteral("Sample Color"));
    QAction *normalizeAction = actionByText(window, QStringLiteral("&Height"));
    QAction *undoAction = actionByText(window, QStringLiteral("&Undo"));
    QAction *fitSelectionAction = actionByText(window, QStringLiteral("Fit &Selection"));
    QAction *optimalAction = actionByText(window, QStringLiteral("&Optimal"));
    QAction *fitSceneAction = actionByText(window, QStringLiteral("&Fit Scene"));
    QVERIFY(selectAllAction && deleteAction && cropAction && gamutAction && sampleAction
            && normalizeAction && undoAction && fitSelectionAction && optimalAction
            && fitSceneAction);

    // Nothing on the board: only always-active actions (and Fit Scene)
    // are enabled.
    QVERIFY(selectAllAction->isEnabled());
    QVERIFY(fitSceneAction->isEnabled());
    QVERIFY(!undoAction->isEnabled());
    QVERIFY(!deleteAction->isEnabled());
    QVERIFY(!cropAction->isEnabled());
    QVERIFY(!gamutAction->isEnabled());
    QVERIFY(!sampleAction->isEnabled());
    QVERIFY(!normalizeAction->isEnabled());
    QVERIFY(!fitSelectionAction->isEnabled());
    QVERIFY(!optimalAction->isEnabled());

    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));
    SceneItem *view = window.scene()->pixmapItemViews().first();
    view->setSelected(true);

    QVERIFY(undoAction->isEnabled());
    QVERIFY(deleteAction->isEnabled());
    QVERIFY(cropAction->isEnabled());
    QVERIFY(gamutAction->isEnabled());
    QVERIFY(sampleAction->isEnabled());
    QVERIFY(normalizeAction->isEnabled());
    QVERIFY(fitSelectionAction->isEnabled());
    QVERIFY(optimalAction->isEnabled());

    // The grayscale check mark follows the first selected image.
    QAction *grayscaleAction = actionByText(window, QStringLiteral("&Grayscale"));
    QVERIFY(grayscaleAction);
    QVERIFY(!grayscaleAction->isChecked());
    grayscaleAction->trigger();
    QVERIFY(view->item()->grayscale());
    QVERIFY(grayscaleAction->isChecked());

    // Adding a text item to the selection turns the single-image group
    // off while the selection group stays on.
    const doc::ItemPtr note = textItem(QStringLiteral("note"));
    window.scene()->document()->addItem(note);
    window.scene()->syncDocument();
    window.scene()->itemViewFor(note)->setSelected(true);
    QVERIFY(deleteAction->isEnabled());
    QVERIFY(!cropAction->isEnabled());
    QVERIFY(!gamutAction->isEnabled());
}

void TestUiScene::selectAllAndDeselectAll()
{
    ui::MainWindow window;
    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));
    window.input()->insertMimeData(mime, QPointF(80, 10));
    QCOMPARE(window.scene()->pixmapItemViews().size(), 2);
    window.scene()->clearSelection();

    actionByText(window, QStringLiteral("&Select All"))->trigger();
    QCOMPARE(window.scene()->selectedItemViews().size(), 2);

    actionByText(window, QStringLiteral("Deselect &All"))->trigger();
    QVERIFY(window.scene()->selectedItemViews().isEmpty());
}

void TestUiScene::deleteSelectionUndoes()
{
    ui::MainWindow window;
    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));
    window.input()->insertMimeData(mime, QPointF(80, 10));
    QCOMPARE(window.scene()->pixmapItemViews().size(), 2);

    const doc::ItemPtr removed = window.scene()->pixmapItemViews().first()->item();
    window.scene()->clearSelection();
    window.scene()->itemViewFor(removed)->setSelected(true);

    actionByText(window, QStringLiteral("&Delete"))->trigger();
    QCOMPARE(window.scene()->pixmapItemViews().size(), 1);
    QCOMPARE(window.scene()->document()->items().size(), 1);

    // One undo brings it back and selects it again.
    actionByText(window, QStringLiteral("&Undo"))->trigger();
    QCOMPARE(window.scene()->document()->items().size(), 2);
    const QVector<SceneItem *> selected = window.scene()->selectedItemViews();
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first()->item(), removed);
}

void TestUiScene::raiseAndLowerChangeZOrder()
{
    ui::MainWindow window;
    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));
    window.input()->insertMimeData(mime, QPointF(60, 10));
    window.input()->insertMimeData(mime, QPointF(110, 10));
    QCOMPARE(window.scene()->document()->items().size(), 3);

    QVector<doc::ItemPtr> items;
    for (const doc::ItemPtr &item : window.scene()->document()->items())
        items.append(item);
    // Inserted items stack upwards already; the order is what matters.
    const double firstZ = items.at(0)->z;
    const double secondZ = items.at(1)->z;
    const double thirdZ = items.at(2)->z;
    QVERIFY(firstZ < secondZ && secondZ < thirdZ);

    window.scene()->clearSelection();
    window.scene()->itemViewFor(items.at(0))->setSelected(true);
    actionByText(window, QStringLiteral("&Raise to Top"))->trigger();
    QVERIFY(items.at(0)->z > thirdZ);
    QCOMPARE(items.at(1)->z, secondZ);
    QCOMPARE(items.at(2)->z, thirdZ);

    // One undo step restores the whole selection's z.
    actionByText(window, QStringLiteral("&Undo"))->trigger();
    QCOMPARE(items.at(0)->z, firstZ);

    // Lowering another item puts it below everything, including the
    // earlier positions.
    window.scene()->clearSelection();
    window.scene()->itemViewFor(items.at(1))->setSelected(true);
    actionByText(window, QStringLiteral("Lower to Bottom"))->trigger();
    QVERIFY(items.at(1)->z < items.at(0)->z);
    QVERIFY(items.at(1)->z < items.at(2)->z);
}

void TestUiScene::newSceneClearsTheBoard()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    settings::setSettingsDir(dir.path());
    {
        // Skip the unsaved-changes question, as a user disabling it would.
        settings::File file(settings::iniPath());
        file.load();
        file.setValue(QStringLiteral("Save"), QStringLiteral("confirm_close_unsaved"),
                      QStringLiteral("false"));
        QVERIFY(file.sync());
    }

    ui::MainWindow window;
    QImage image(6, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    window.input()->insertMimeData(mime, QPointF(10, 10));
    QCOMPARE(window.scene()->pixmapItemViews().size(), 1);
    QVERIFY(window.scene()->document()->isModified());

    actionByText(window, QStringLiteral("&New Scene"))->trigger();
    QCOMPARE(window.scene()->pixmapItemViews().size(), 0);
    QVERIFY(!window.scene()->document()->isModified());
    QVERIFY(!actionByText(window, QStringLiteral("&Undo"))->isEnabled());

    settings::setSettingsDir(QString());
}

void TestUiScene::hudToastsAppearAndExpire()
{
    ui::View view;
    ui::hud::toast(&view, QStringLiteral("hello"), 50);
    QList<QWidget *> toasts = view.findChildren<QWidget *>(QStringLiteral("HUDToast"));
    QCOMPARE(toasts.size(), 1);
    QVERIFY(!toasts.first()->isHidden());

    // A second toast stacks under the first.
    ui::hud::toast(&view, QStringLiteral("world"), 50);
    toasts = view.findChildren<QWidget *>(QStringLiteral("HUDToast"));
    QCOMPARE(toasts.size(), 2);
    QVERIFY(toasts.at(1)->y() > toasts.at(0)->y());

    QTest::qWait(250);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(view.findChildren<QWidget *>(QStringLiteral("HUDToast")).isEmpty());
}

void TestUiScene::settingsDialogWritesAndRestores()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    settings::setSettingsDir(dir.path());

    ui::SettingsDialog dialog;
    QSignalSpy spy(&dialog, &ui::SettingsDialog::settingChanged);

    // An integer field writes through immediately.
    auto *gap = dialog.findChild<QSpinBox *>(QStringLiteral("Items/arrange_gap"));
    QVERIFY(gap);
    gap->setValue(25);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("Items/arrange_gap"));
    {
        settings::File file(settings::iniPath());
        file.load();
        QCOMPARE(file.value(QStringLiteral("Items"), QStringLiteral("arrange_gap")),
                 QStringLiteral("25"));
    }

    // So does a checkbox, a radio option and a line edit.
    auto *confirm = dialog.findChild<QCheckBox *>(QStringLiteral("Save/confirm_close_unsaved"));
    QVERIFY(confirm);
    confirm->setChecked(false);
    auto *budgetMethod = dialog.findChild<QRadioButton *>(QStringLiteral("ram_budget"));
    QVERIFY(budgetMethod);
    budgetMethod->setChecked(true);
    auto *fractions = dialog.findChild<QLineEdit *>(QStringLiteral("Items/lod_fractions"));
    QVERIFY(fractions);
    fractions->setText(QStringLiteral("1,0.5"));
    {
        settings::File file(settings::iniPath());
        file.load();
        QCOMPARE(file.value(QStringLiteral("Save"), QStringLiteral("confirm_close_unsaved")),
                 QStringLiteral("false"));
        QCOMPARE(file.value(QStringLiteral("Items"), QStringLiteral("lod_method")),
                 QStringLiteral("ram_budget"));
        QCOMPARE(file.value(QStringLiteral("Items"), QStringLiteral("lod_fractions")),
                 QStringLiteral("1,0.5"));
    }

    // A changed group shows the marker in its title.
    auto *gapGroup = dialog.findChild<QGroupBox *>(QStringLiteral("Items/arrange_gap"));
    QVERIFY(gapGroup);
    QVERIFY(gapGroup->title().contains(QString::fromUtf8(constants::kChangedSymbol)));

    // Restoring defaults clears the entries and the widgets.
    QSignalSpy restoredSpy(&dialog, &ui::SettingsDialog::settingsRestored);
    dialog.restoreDefaults();
    QCOMPARE(restoredSpy.count(), 1);
    {
        settings::File file(settings::iniPath());
        file.load();
        QVERIFY(!file.contains(QStringLiteral("Items"), QStringLiteral("arrange_gap")));
        QVERIFY(!file.contains(QStringLiteral("Items"), QStringLiteral("lod_method")));
    }
    QCOMPARE(gap->value(), 0);
    QVERIFY(!gapGroup->title().contains(QString::fromUtf8(constants::kChangedSymbol)));

    settings::setSettingsDir(QString());
}

void TestUiScene::settingsActionOpensTheDialog()
{
    ui::MainWindow window;
    actionByText(window, QStringLiteral("&Settings"))->trigger();

    QDialog *settings = nullptr;
    for (QDialog *candidate : window.findChildren<QDialog *>()) {
        if (candidate->windowTitle().endsWith(QStringLiteral("Settings")))
            settings = candidate;
    }
    QVERIFY(settings);
    QVERIFY(settings->isVisible() || !settings->isHidden());
}

QTEST_MAIN(TestUiScene)

#include "test_ui_scene.moc"
