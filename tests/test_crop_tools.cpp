#include <QtTest>

#include "ui/crop_tools.h"

using namespace ui;

class TestCropTools : public QObject
{
    Q_OBJECT

private slots:
    void handleAndEdgeGeometry();
    void hitTestOrderAndInsideArea();
    void clampingKeepsTheRectangleSane();
    void draggingEachPart();
    void cursorsFollowRotation();
};

void TestCropTools::handleAndEdgeGeometry()
{
    const QRectF rect(0, 0, 200, 100);
    QCOMPARE(crop::handleRect(rect, crop::Part::TopLeft, 1.0), QRectF(0, 0, 15, 15));
    QCOMPARE(crop::handleRect(rect, crop::Part::TopRight, 1.0), QRectF(185, 0, 15, 15));
    QCOMPARE(crop::handleRect(rect, crop::Part::BottomLeft, 1.0), QRectF(0, 85, 15, 15));
    QCOMPARE(crop::handleRect(rect, crop::Part::BottomRight, 1.0), QRectF(185, 85, 15, 15));
    QCOMPARE(crop::edgeRect(rect, crop::Part::Top, 1.0), QRectF(15, 0, 170, 15));
    QCOMPARE(crop::edgeRect(rect, crop::Part::Bottom, 1.0), QRectF(15, 85, 170, 15));
    QCOMPARE(crop::edgeRect(rect, crop::Part::Left, 1.0), QRectF(0, 15, 15, 70));
    QCOMPARE(crop::edgeRect(rect, crop::Part::Right, 1.0), QRectF(185, 15, 15, 70));

    // Twice the on-screen scale halves the areas in item coordinates.
    QCOMPARE(crop::handleRect(rect, crop::Part::TopLeft, 2.0), QRectF(0, 0, 7.5, 7.5));
    // A zoomed-out item gets larger areas.
    QCOMPARE(crop::handleRect(rect, crop::Part::TopLeft, 0.5), QRectF(0, 0, 30, 30));
}

void TestCropTools::hitTestOrderAndInsideArea()
{
    const QRectF rect(0, 0, 200, 100);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(7, 7)), crop::Part::TopLeft);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(192, 7)), crop::Part::TopRight);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(7, 92)), crop::Part::BottomLeft);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(192, 92)), crop::Part::BottomRight);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(100, 7)), crop::Part::Top);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(100, 92)), crop::Part::Bottom);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(7, 50)), crop::Part::Left);
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(192, 50)), crop::Part::Right);
    // The middle of the rectangle is not a handle: clicking there
    // confirms the crop.
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(100, 50)), crop::Part::None);
    // Corners win over edges where they overlap.
    QCOMPARE(crop::hitTest(rect, 1.0, QPointF(10, 10)), crop::Part::TopLeft);
}

void TestCropTools::clampingKeepsTheRectangleSane()
{
    const QRectF rect(50, 50, 100, 100);
    const QRectF image(0, 0, 200, 200);

    // The top-left corner cannot cross the bottom-right one.
    QCOMPARE(crop::clampPoint(rect, crop::Part::TopLeft, QPointF(400, 400), image),
             QPointF(150, 150));
    // Nor leave the image.
    QCOMPARE(crop::clampPoint(rect, crop::Part::TopLeft, QPointF(-100, -100), image),
             QPointF(0, 0));
    QCOMPARE(crop::clampPoint(rect, crop::Part::BottomRight, QPointF(500, 500), image),
             QPointF(200, 200));
    QCOMPARE(crop::clampPoint(rect, crop::Part::TopRight, QPointF(500, -50), image),
             QPointF(200, 0));
    QCOMPARE(crop::clampPoint(rect, crop::Part::BottomLeft, QPointF(-50, 500), image),
             QPointF(0, 200));

    // Edges only move their own axis, within the opposite side.
    QCOMPARE(crop::clampPoint(rect, crop::Part::Top, QPointF(0, 400), image),
             QPointF(0, 150));
    QCOMPARE(crop::clampPoint(rect, crop::Part::Right, QPointF(999, 0), image),
             QPointF(200, 0));
}

void TestCropTools::draggingEachPart()
{
    const QRectF rect(50, 50, 100, 100);
    const QRectF image(0, 0, 200, 200);

    QCOMPARE(crop::draggedRect(rect, crop::Part::TopLeft, QPointF(-10, -10), image),
             QRectF(40, 40, 110, 110));
    QCOMPARE(crop::draggedRect(rect, crop::Part::BottomRight, QPointF(10, 20), image),
             QRectF(50, 50, 110, 120));
    QCOMPARE(crop::draggedRect(rect, crop::Part::TopRight, QPointF(5, -5), image),
             QRectF(50, 45, 105, 105));
    QCOMPARE(crop::draggedRect(rect, crop::Part::BottomLeft, QPointF(-5, 5), image),
             QRectF(45, 50, 105, 105));
    QCOMPARE(crop::draggedRect(rect, crop::Part::Top, QPointF(0, -10), image),
             QRectF(50, 40, 100, 110));
    QCOMPARE(crop::draggedRect(rect, crop::Part::Bottom, QPointF(0, 10), image),
             QRectF(50, 50, 100, 110));
    QCOMPARE(crop::draggedRect(rect, crop::Part::Left, QPointF(-10, 0), image),
             QRectF(40, 50, 110, 100));
    QCOMPARE(crop::draggedRect(rect, crop::Part::Right, QPointF(10, 0), image),
             QRectF(50, 50, 110, 100));

    // Dragging a corner past its opposite collapses the rectangle
    // instead of inverting it.
    const QRectF collapsed =
        crop::draggedRect(rect, crop::Part::BottomRight, QPointF(-500, -500), image);
    QCOMPARE(collapsed.topLeft(), QPointF(50, 50));
    QCOMPARE(collapsed.width(), 0.0);
    QCOMPARE(collapsed.height(), 0.0);
}

void TestCropTools::cursorsFollowRotation()
{
    // Unrotated corners use the same diagonal mapping as the selection
    // handles.
    QCOMPARE(crop::handleCursor(crop::Part::TopLeft, 0, false), Qt::SizeFDiagCursor);
    QCOMPARE(crop::handleCursor(crop::Part::BottomRight, 0, false), Qt::SizeFDiagCursor);
    QCOMPARE(crop::handleCursor(crop::Part::TopRight, 0, false), Qt::SizeBDiagCursor);
    QCOMPARE(crop::handleCursor(crop::Part::BottomLeft, 0, false), Qt::SizeBDiagCursor);
    // Rotated a quarter turn they swap.
    QCOMPARE(crop::handleCursor(crop::Part::TopLeft, 90, false), Qt::SizeBDiagCursor);
    // Flipping swaps them back.
    QCOMPARE(crop::handleCursor(crop::Part::TopLeft, 90, true), Qt::SizeFDiagCursor);

    // Edges: horizontal for top/bottom, unless the item lies sideways.
    QCOMPARE(crop::edgeCursor(crop::Part::Top, 0), Qt::SizeVerCursor);
    QCOMPARE(crop::edgeCursor(crop::Part::Left, 0), Qt::SizeHorCursor);
    QCOMPARE(crop::edgeCursor(crop::Part::Top, 90), Qt::SizeHorCursor);
    QCOMPARE(crop::edgeCursor(crop::Part::Left, 90), Qt::SizeVerCursor);
    QCOMPARE(crop::edgeCursor(crop::Part::Top, 270), Qt::SizeHorCursor);
    QCOMPARE(crop::edgeCursor(crop::Part::Right, 180), Qt::SizeHorCursor);
}

QTEST_MAIN(TestCropTools)

#include "test_crop_tools.moc"
