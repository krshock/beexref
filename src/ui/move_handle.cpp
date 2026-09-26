#include "move_handle.h"

#include "theme.h"

#include <QMouseEvent>
#include <QPainter>

namespace ui {
namespace {

// A four-way arrow, drawn rather than loaded: the app has no move icon
// asset, and this keeps it theme-coloured and crisp at any scale.
void paintArrows(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const QPointF centre = rect.center();
    // Proportions of the original 30x30 icon, so the arrow scales with
    // the button.
    const double size = qMin(rect.width(), rect.height());
    const double reach = size * (10.0 / 30.0); // tip distance from the centre
    const double head = size * (3.2 / 30.0);   // arrow head half width
    const double neck = size * (5.5 / 30.0);   // where the head starts
    const double shaft = size * (1.2 / 30.0);  // shaft half thickness

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    // The shafts.
    painter.drawRect(QRectF(centre.x() - shaft, centre.y() - neck, shaft * 2, neck * 2));
    painter.drawRect(QRectF(centre.x() - neck, centre.y() - shaft, neck * 2, shaft * 2));

    // The heads, one per direction.
    const QPointF heads[4][3] = {
        {{centre.x(), centre.y() - reach},
         {centre.x() - head, centre.y() - neck},
         {centre.x() + head, centre.y() - neck}},
        {{centre.x(), centre.y() + reach},
         {centre.x() + head, centre.y() + neck},
         {centre.x() - head, centre.y() + neck}},
        {{centre.x() - reach, centre.y()},
         {centre.x() - neck, centre.y() - head},
         {centre.x() - neck, centre.y() + head}},
        {{centre.x() + reach, centre.y()},
         {centre.x() + neck, centre.y() + head},
         {centre.x() + neck, centre.y() - head}},
    };
    for (const auto &triangle : heads) {
        QPolygonF polygon;
        polygon << triangle[0] << triangle[1] << triangle[2];
        painter.drawPolygon(polygon);
    }
}

} // namespace

MoveHandle::MoveHandle(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(kSize, kSize);
    setCursor(Qt::SizeAllCursor);
    setToolTip(QStringLiteral("Move the window"));
    setMouseTracking(true);
    // Match the HUD panels: a translucent glass body on the canvas.
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

void MoveHandle::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(theme::hudBorder, 1));
    painter.setBrush(theme::hudSurface);
    const double radius = qMin(rect().width(), rect().height()) * (8.0 / 30.0);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    paintArrows(painter, rect(), theme::hudForeground);
}

void MoveHandle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit moveRequested();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

} // namespace ui
