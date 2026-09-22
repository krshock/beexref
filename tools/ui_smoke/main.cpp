// Offscreen UI smoke harness.
//
// Opens a board in the real MainWindow, drives it with in-process Qt
// events (no window system, no X server) and writes screenshots to an
// output directory for inspection. This replaces driving the app with
// xdotool on the user's desktop: nothing here can reach other
// applications.
//
//   QT_QPA_PLATFORM=offscreen ./beexref-ui-smoke [board] [outdir]
//
// Defaults: no board (new untitled document), /tmp/opencode/ui-smoke.
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QMimeData>
#include <QAction>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextStream>
#include <QWheelEvent>
#include <QtTest>

#include <cmath>

#include "doc/document.h"
#include "settings.h"
#include "ui/input_controller.h"
#include "ui/lod_manager.h"
#include "ui/main_window.h"
#include "ui/scene.h"
#include "ui/selection_ops.h"
#include "ui/scene_item.h"
#include "ui/view.h"

#include "doc/undo.h"
#include "util/memory.h"

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &position, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(position), QPointF(widget->mapToGlobal(position)), button,
                      buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void sendWheel(QWidget *widget, const QPoint &position, int delta)
{
    QWheelEvent event(QPointF(position), QPointF(widget->mapToGlobal(position)), QPoint(),
                      QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
}

void middleDrag(QWidget *widget, const QPoint &from, const QPoint &to)
{
    sendMouse(widget, QEvent::MouseButtonPress, from, Qt::MiddleButton, Qt::MiddleButton);
    sendMouse(widget, QEvent::MouseMove, from + (to - from) / 2, Qt::NoButton, Qt::MiddleButton);
    sendMouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::MiddleButton);
    sendMouse(widget, QEvent::MouseButtonRelease, to, Qt::MiddleButton, Qt::NoButton);
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

class Smoke
{
public:
    Smoke(ui::MainWindow &window, QString outputDir)
        : window_(window)
        , outputDir_(std::move(outputDir))
    {
    }

    bool run()
    {
        if (!QDir().mkpath(outputDir_))
            return false;

        const QSize size(1000, 700);
        window_.resize(size);
        window_.show();
        QTest::qWait(1500); // let the initial levels settle
        snapshot(QStringLiteral("01-open"));

        ui::View *view = window_.view();
        QWidget *viewport = view->viewport();
        const QPoint centre = viewport->rect().center();

        // Zoom in with the wheel, anchored at the centre.
        for (int i = 0; i < 14; ++i) {
            sendWheel(viewport, centre, 120);
            QTest::qWait(30);
        }
        QTest::qWait(2000);
        snapshot(QStringLiteral("02-zoomed"));

        // Pan with the middle button.
        middleDrag(viewport, centre, centre + QPoint(250, 150));
        QTest::qWait(1000);
        snapshot(QStringLiteral("03-panned"));

        // Back to the whole board, then paste there: a realistic paste
        // lands among the content, where undo does not have to shrink
        // the scrollable area out from under the view.
        view->fitScene();
        QTest::qWait(800);

        // Paste an image from the offscreen clipboard.
        const QByteArray png = makePng(600, 400, Qt::magenta);
        QImage pasted;
        pasted.loadFromData(png);
        QApplication::clipboard()->setImage(pasted);
        QTest::keyClick(&window_, Qt::Key_V, Qt::ControlModifier);
        QTest::qWait(1500);
        reportPastedItem();
        // A pasted 600x400 item is about a pixel wide at the board's fit
        // zoom; frame it as a user would (the inserted items are
        // selected, so fit-selection shows them).
        window_.view()->fitSelection();
        QTest::qWait(800);
        reportPastedItem();
        snapshot(QStringLiteral("04-pasted"));

        // Undo and redo it. The reference binds redo to Ctrl+Shift+Z.
        QTest::keyClick(&window_, Qt::Key_Z, Qt::ControlModifier);
        QTest::qWait(800);
        snapshot(QStringLiteral("05-undo-paste"));
        const int afterUndo = window_.scene()->itemViews().size();
        QTest::keyClick(&window_, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(800);
        const int afterRedo = window_.scene()->itemViews().size();
        if (afterRedo == afterUndo) {
            out() << "ERROR: redo did not restore the item (" << afterUndo << " -> " << afterRedo
                  << ")\n";
            return false;
        }
        out() << "items after redo: " << afterRedo << "\n";
        snapshot(QStringLiteral("06-redo-paste"));

        // Scale the pasted item with its bottom-right handle, then undo.
        ui::SceneItem *target = nullptr;
        for (ui::SceneItem *candidate : window_.scene()->pixmapItemViews()) {
            if (!target || candidate->zValue() > target->zValue())
                target = candidate;
        }
        if (target) {
            target->setSelected(true);
            window_.view()->fitSelection();
            QTest::qWait(400);

            const QRectF bounds = window_.scene()->selectionBounds();
            const double viewScale = view->transform().m11();
            const QPointF unit(1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0));
            const QPoint press = view->mapFromScene(bounds.bottomRight()
                                                    - unit * (4.0 / viewScale));
            const QPoint move = view->mapFromScene(bounds.bottomRight()
                                                   + unit * (120.0 / viewScale));
            sendMouse(viewport, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
            sendMouse(viewport, QEvent::MouseMove, move, Qt::NoButton, Qt::LeftButton);
            sendMouse(viewport, QEvent::MouseButtonRelease, move, Qt::LeftButton, Qt::NoButton);
            QTest::qWait(800);
            out() << "scale after handle drag: " << QString::number(target->item()->scale, 'f', 3)
                  << "\n";
            snapshot(QStringLiteral("07-scaled"));

            QTest::keyClick(&window_, Qt::Key_Z, Qt::ControlModifier);
            QTest::qWait(800);
            out() << "scale after undo: " << QString::number(target->item()->scale, 'f', 3) << "\n";
            snapshot(QStringLiteral("08-scale-undone"));

            // Drag the item and confirm the canvas does not move.
            const QPointF centreBefore = view->mapToScene(viewport->rect().center());
            const QPoint grab =
                view->mapFromScene(window_.scene()->selectionBounds().center());
            sendMouse(viewport, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
            for (int step = 1; step <= 40; ++step) {
                sendMouse(viewport, QEvent::MouseMove, grab + QPoint(step, step / 2), Qt::NoButton,
                          Qt::LeftButton);
            }
            sendMouse(viewport, QEvent::MouseButtonRelease, grab + QPoint(40, 20), Qt::LeftButton,
                      Qt::NoButton);
            QTest::qWait(800);
            const QPointF centreAfter = view->mapToScene(viewport->rect().center());
            const double scale = view->transform().m11();
            out() << "view drift during item drag: "
                  << QString::number((centreAfter.x() - centreBefore.x()) * scale, 'f', 3) << ","
                  << QString::number((centreAfter.y() - centreBefore.y()) * scale, 'f', 3)
                  << " device px\n";
            snapshot(QStringLiteral("09-moved"));

            // E2: grayscale and opacity on the same item, with the
            // centre pixel read back from a real repaint.
            doc::UndoStack localStack(window_.scene()->document().get());
            auto centrePixel = [&]() {
                const QImage shot = view->viewport()->grab().toImage();
                return shot.pixelColor(view->mapFromScene(target->sceneBoundingRect().center()));
            };

            const QColor before = centrePixel();
            ui::selection::setGrayscale(*window_.scene(), localStack, true);
            QTest::qWait(600);
            const QColor gray = centrePixel();
            out() << "grayscale: model=" << target->item()->grayscale()
                  << " grayLevel=" << (target->displayLevel().format() == QImage::Format_Grayscale8)
                  << " pixel=" << gray.name()
                  << " grey=" << (gray.red() == gray.green() && gray.green() == gray.blue()) << "\n";
            snapshot(QStringLiteral("10-grayscale"));

            ui::selection::setOpacity(*window_.scene(), localStack, 0.5);
            QTest::qWait(600);
            const QColor faded = centrePixel();
            out() << "opacity: model=" << target->item()->opacity() << " pixel=" << faded.name()
                  << "\n";
            snapshot(QStringLiteral("11-opacity"));

            localStack.undo();
            localStack.undo();
            window_.scene()->syncDocument();
            QTest::qWait(600);
            const QColor restored = centrePixel();
            out() << "adjustments undone: model=" << target->item()->opacity() << "/"
                  << target->item()->grayscale() << " pixel=" << restored.name()
                  << " matches before=" << (restored == before) << "\n";
            snapshot(QStringLiteral("12-adjustments-undone"));

            // E3: crop with the bottom-right handle, confirm with
            // Enter, then undo.
            window_.view()->cropSelection();
            out() << "crop mode: " << window_.view()->cropActive() << "\n";
            const QRectF cropStart = target->cropRect();
            const double cropScale = view->transform().m11() * target->item()->scale;
            const QPointF handle =
                cropStart.bottomRight() - QPointF(7.5 / cropScale, 7.5 / cropScale);
            const QPoint cropPress = view->mapFromScene(target->mapToScene(handle));
            const QPoint cropMove = cropPress + QPoint(-200, -140);
            sendMouse(viewport, QEvent::MouseButtonPress, cropPress, Qt::LeftButton,
                      Qt::LeftButton);
            sendMouse(viewport, QEvent::MouseMove, cropMove, Qt::NoButton, Qt::LeftButton);
            sendMouse(viewport, QEvent::MouseButtonRelease, cropMove, Qt::LeftButton,
                      Qt::NoButton);
            QTest::qWait(400);
            snapshot(QStringLiteral("13-crop-drag"));

            QTest::keyClick(view, Qt::Key_Return);
            QTest::qWait(600);
            const QRectF crop = target->item()->crop();
            out() << "crop: has=" << target->item()->hasCrop() << " rect=" << crop.x() << ","
                  << crop.y() << " " << crop.width() << "x" << crop.height()
                  << " bounds=" << target->boundingRect().width() << "x"
                  << target->boundingRect().height() << "\n";
            snapshot(QStringLiteral("14-cropped"));

            QTest::keyClick(&window_, Qt::Key_Z, Qt::ControlModifier);
            QTest::qWait(600);
            out() << "crop undone: has=" << target->item()->hasCrop()
                  << " bounds=" << target->boundingRect().width() << "x"
                  << target->boundingRect().height() << "\n";
            snapshot(QStringLiteral("15-crop-undone"));

            // E4: normalize and arrange the three topmost items through
            // the menu actions, then undo both.
            QVector<ui::SceneItem *> picked;
            const QVector<ui::SceneItem *> all = window_.scene()->pixmapItemViews();
            for (int i = 0; i < 3 && i < all.size(); ++i)
                picked.append(all.at(i));
            window_.scene()->clearSelection();
            for (ui::SceneItem *view : picked)
                view->setSelected(true);

            QAction *normalizeHeight = nullptr;
            QAction *arrangeHorizontal = nullptr;
            const QList<QAction *> actions = window_.findChildren<QAction *>();
            for (QAction *action : actions) {
                if (action->text() == QStringLiteral("&Height"))
                    normalizeHeight = action;
                else if (action->text() == QStringLiteral("&Horizontal (by filename)"))
                    arrangeHorizontal = action;
            }
            auto layoutBoxes = [&]() {
                QStringList out;
                for (ui::SceneItem *view : picked) {
                    const QRectF box = view->sceneBoundingRect();
                    out << QStringLiteral("%1,%2 %3x%4")
                               .arg(box.x(), 0, 'f', 1)
                               .arg(box.y(), 0, 'f', 1)
                               .arg(box.width(), 0, 'f', 1)
                               .arg(box.height(), 0, 'f', 1);
                }
                return out.join(QStringLiteral(" | "));
            };

            const QString layoutBefore = layoutBoxes();
            normalizeHeight->trigger();
            QTest::qWait(500);
            snapshot(QStringLiteral("16-normalized"));
            arrangeHorizontal->trigger();
            QTest::qWait(500);
            snapshot(QStringLiteral("17-arranged"));
            out() << "layout before: " << layoutBefore << "\n";
            out() << "layout after:  " << layoutBoxes() << "\n";

            QTest::keyClick(&window_, Qt::Key_Z, Qt::ControlModifier);
            QTest::keyClick(&window_, Qt::Key_Z, Qt::ControlModifier);
            QTest::qWait(800);
            snapshot(QStringLiteral("18-layout-undone"));
            out() << "layout undone: " << layoutBoxes() << "\n";
            out() << "layout restored: " << (layoutBoxes() == layoutBefore) << "\n";
        }

        const ui::LodManager::Stats stats = view->lodManager()->stats();
        out() << "items=" << window_.scene()->itemViews().size()
              << " pixmaps=" << window_.scene()->pixmapItemViews().size()
              << " level_mb=" << QString::number(stats.levelMB, 'f', 1)
              << " cache_mb=" << QString::number(stats.cacheMB, 'f', 1)
              << " decodes=" << stats.decodes << " cancelled=" << stats.cancelled
              << " rss_mb=" << QString::number(util::processRssBytes() / 1024.0 / 1024.0, 'f', 1)
              << "\n";
        out() << "screenshots in " << outputDir_ << "\n";
        out().flush();
        return true;
    }

private:
    // Prints where a freshly pasted (selected) item sits relative to the
    // visible area, so a screenshot that does not show it is explained.
    void reportPastedItem()
    {
        ui::View *view = window_.view();
        const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
        out() << "visible scene rect: " << visible.x() << "," << visible.y() << " "
              << visible.width() << "x" << visible.height() << "\n";
        for (ui::SceneItem *item : window_.scene()->selectedItemViews()) {
            const QRectF rect = item->sceneBoundingRect();
            out() << "selected item rect: " << rect.x() << "," << rect.y() << " "
                  << rect.width() << "x" << rect.height()
                  << " intersects=" << (rect.intersects(visible) ? "yes" : "no")
                  << " z=" << item->zValue() << "\n";
        }
        out().flush();
    }

    void snapshot(const QString &name)
    {
        const QString path = outputDir_ + QLatin1Char('/') + name + QStringLiteral(".png");
        const ui::View *view = window_.view();
        const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
        const QRectF sceneRect = view->scene()->sceneRect();
        out() << name << " scale=" << QString::number(view->transform().m11(), 'f', 4)
              << " centre=" << QString::number(visible.center().x(), 'f', 0) << ","
              << QString::number(visible.center().y(), 'f', 0)
              << " sceneRect=" << QString::number(sceneRect.x(), 'f', 0) << ","
              << QString::number(sceneRect.y(), 'f', 0) << " "
              << QString::number(sceneRect.width(), 'f', 0) << "x"
              << QString::number(sceneRect.height(), 'f', 0)
              << " hscroll=" << view->horizontalScrollBar()->value() << "/"
              << view->horizontalScrollBar()->maximum() << " -> " << path << "\n";
        window_.grab().save(path);
        out().flush();
    }

    ui::MainWindow &window_;
    QString outputDir_;
};

} // namespace

int main(int argc, char *argv[])
{
    util::configureAllocator();
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("BeeXRef"));

    const QStringList arguments = QCoreApplication::arguments().mid(1);
    const QString board = arguments.value(0);
    const QString outputDir = arguments.value(1, QStringLiteral("/tmp/opencode/ui-smoke"));

    // Keep the run out of the user's real settings and cache.
    settings::setSettingsDir(outputDir + QStringLiteral("/settings"));

    ui::MainWindow window;
    if (!board.isEmpty() && !window.openBoard(board)) {
        out() << "could not open " << board << "\n";
        return 1;
    }

    Smoke smoke(window, outputDir);
    return smoke.run() ? 0 : 1;
}
