#include <QtTest>

#include <cmath>

#include "ui/selection_tools.h"

using namespace ui;

class TestSelectionTools : public QObject
{
    Q_OBJECT

private slots:
    void cornersAndAnchors();
    void freeCentreAlwaysMoves();
    void scaleAreasAndRotateBands();
    void flipBandsOnTheEdges();
    void scaleFactorFollowsTheCornerDirection();
    void rotationAnglesAndSnap();
    void scaleCursorsFollowRotationAndFlip();
};

void TestSelectionTools::cornersAndAnchors()
{
    const QRectF bounds(0, 0, 100, 50);
    QCOMPARE(selection::corner(bounds, 0), QPointF(0, 0));
    QCOMPARE(selection::corner(bounds, 1), QPointF(100, 0));
    QCOMPARE(selection::corner(bounds, 2), QPointF(100, 50));
    QCOMPARE(selection::corner(bounds, 3), QPointF(0, 50));
    QCOMPARE(selection::scaleAnchor(bounds, 2), QPointF(0, 0));
    QCOMPARE(selection::scaleAnchor(bounds, 0), QPointF(100, 50));
}

void TestSelectionTools::freeCentreAlwaysMoves()
{
    const QRectF bounds(0, 0, 100, 100);
    // The centre moves even though the handles would overlap on a
    // very small item.
    QCOMPARE(selection::hitTest(bounds, 1.0, bounds.center()).part, selection::Part::None);
    const QRectF tiny(0, 0, 8, 8);
    QCOMPARE(selection::hitTest(tiny, 1.0, tiny.center()).part, selection::Part::None);
}

void TestSelectionTools::scaleAreasAndRotateBands()
{
    const QRectF bounds(0, 0, 200, 100);
    // Exactly on the bottom-right corner: scale.
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(200, 100)).part, selection::Part::Scale);
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(200, 100)).corner, 2);
    // Inside the scale box but off the exact corner: still scale.
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(208, 108)).part, selection::Part::Scale);
    // Just beyond the scale box, within the rotation band: rotate
    // (the band spans 10 to 20 scene units outside the corner).
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(215, 115)).part, selection::Part::Rotate);
    // Far outside: nothing.
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(260, 160)).part, selection::Part::None);
    // At 0.5 view scale the areas are twice as large in scene units,
    // so the same relative point sits inside the band.
    QCOMPARE(selection::hitTest(bounds, 0.5, QPointF(230, 130)).part, selection::Part::Rotate);
    QCOMPARE(selection::hitTest(bounds, 0.5, QPointF(214, 114)).part, selection::Part::Scale);
    QCOMPARE(selection::hitTest(bounds, 0.5, QPointF(260, 160)).part, selection::Part::None);
}

void TestSelectionTools::flipBandsOnTheEdges()
{
    const QRectF bounds(0, 0, 200, 100);
    // Middle of the top and bottom edges: vertical flip.
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(100, -4)).part,
             selection::Part::FlipVertical);
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(100, 104)).part,
             selection::Part::FlipVertical);
    // Middle of the left and right edges: horizontal flip.
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(-4, 50)).part,
             selection::Part::FlipHorizontal);
    QCOMPARE(selection::hitTest(bounds, 1.0, QPointF(204, 50)).part,
             selection::Part::FlipHorizontal);
}

void TestSelectionTools::scaleFactorFollowsTheCornerDirection()
{
    const QRectF bounds(0, 0, 200, 100);
    // Dragging the bottom-right corner further out grows the item; the
    // factor is the travel along the diagonal, relative to the diagonal
    // length.
    const QPointF press(200, 100);
    const double diagonal = std::hypot(200.0, 100.0);
    const QPointF move = press + QPointF(diagonal / 4.0, diagonal / 8.0) * 0.5;
    QVERIFY(selection::scaleFactor(bounds, press, move) > 1.0);
    QVERIFY(selection::scaleFactor(bounds, press, press) == 1.0);
    // Dragging back towards the centre shrinks.
    QVERIFY(selection::scaleFactor(bounds, press, QPointF(180, 90)) < 1.0);
}

void TestSelectionTools::rotationAnglesAndSnap()
{
    const QPointF anchor(0, 0);
    // Above the anchor is 0 degrees, to the right is 90 (the reference
    // uses -atan2(x, y)).
    QCOMPARE(selection::rotationAngle(anchor, QPointF(0, 10)), 0.0);
    QCOMPARE(selection::rotationAngle(anchor, QPointF(10, 0)), -90.0);
    QCOMPARE(selection::snapAngle(7.0), 0.0);
    QCOMPARE(selection::snapAngle(8.0), 15.0);
    QCOMPARE(selection::snapAngle(23.0), 30.0);
}

void TestSelectionTools::scaleCursorsFollowRotationAndFlip()
{
    // Unrotated, unflipped: opposite diagonals for opposite corners.
    QCOMPARE(selection::scaleCursor(2, 0, false), Qt::SizeFDiagCursor);
    QCOMPARE(selection::scaleCursor(0, 0, false), Qt::SizeFDiagCursor);
    QCOMPARE(selection::scaleCursor(1, 0, false), Qt::SizeBDiagCursor);
    QCOMPARE(selection::scaleCursor(3, 0, false), Qt::SizeBDiagCursor);
    // Rotated 90 degrees the diagonals swap.
    QCOMPARE(selection::scaleCursor(2, 90, false), Qt::SizeBDiagCursor);
    QCOMPARE(selection::scaleCursor(2, 45, false), Qt::SizeVerCursor);
    // Flipping swaps them back.
    QCOMPARE(selection::scaleCursor(2, 90, true), Qt::SizeFDiagCursor);
}

QTEST_GUILESS_MAIN(TestSelectionTools)

#include "test_selection_tools.moc"
