#include "layout_ops.h"

#include "scene.h"
#include "scene_item.h"
#include "selection_ops.h"

#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ui::layout {
namespace {

QString filenameOf(const doc::Item &item)
{
    if (!item.filename.isEmpty())
        return item.filename;
    return item.data.value(QStringLiteral("filename")).toString();
}

// The item's scene bounding box, which is what the reference arranges
// by (rotation and crops included).
QRectF boundsOf(const SceneItem *view)
{
    return view->sceneBoundingRect();
}

// The reference's item.center: the centre of the visible rectangle, in
// scene coordinates. Used as the anchor for normalizing scales.
QPointF centerOf(const SceneItem *view)
{
    return view->mapToScene(view->boundingRect().center());
}

double normalizeValue(const QRectF &rect, Normalize mode)
{
    switch (mode) {
    case Normalize::Width:
        return rect.width();
    case Normalize::Height:
        return rect.height();
    case Normalize::Size:
        return rect.width() * rect.height();
    }
    return 0.0;
}

void pushChange(doc::UndoStack &stack, SceneItem *view,
                const doc::ChangeItemCommand::State &before, const QString &text)
{
    const doc::ChangeItemCommand::State after =
        doc::ChangeItemCommand::State::capture(*view->item());
    if (after == before)
        return;
    stack.push(std::make_unique<doc::ChangeItemCommand>(view->item(), before, after, text));
}

} // namespace

QVector<SceneItem *> orderedSelection(const Scene &scene)
{
    QVector<SceneItem *> byFilename;
    QVector<SceneItem *> bySaveId;
    QVector<SceneItem *> remaining;
    for (SceneItem *view : selection::selectionItems(scene)) {
        const QString name = filenameOf(*view->item());
        if (!name.isEmpty())
            byFilename.append(view);
        else if (view->item()->id != 0)
            bySaveId.append(view);
        else
            remaining.append(view);
    }
    std::stable_sort(byFilename.begin(), byFilename.end(),
                     [](const SceneItem *a, const SceneItem *b) {
                         return filenameOf(*a->item()) < filenameOf(*b->item());
                     });
    std::stable_sort(bySaveId.begin(), bySaveId.end(), [](const SceneItem *a, const SceneItem *b) {
        return a->item()->id < b->item()->id;
    });

    QVector<SceneItem *> ordered = byFilename;
    ordered += bySaveId;
    ordered += remaining;
    return ordered;
}

Arrange arrangeModeFromSetting(const QString &value)
{
    const QString mode = value.trimmed().toLower();
    if (mode == QLatin1String("horizontal"))
        return Arrange::Horizontal;
    if (mode == QLatin1String("vertical"))
        return Arrange::Vertical;
    // "square", "optimal" and anything unrecognised.
    return Arrange::Square;
}

void normalize(const Scene &scene, doc::UndoStack &stack, Normalize mode)
{
    const QVector<SceneItem *> items = selection::selectionItems(scene);
    if (items.size() < 2)
        return;

    QVector<double> values;
    values.reserve(items.size());
    for (SceneItem *view : items)
        values.append(normalizeValue(boundsOf(view), mode));

    // A degenerate item (a zero-size crop, say) has no meaningful
    // factor; the reference divides by zero here, we simply do nothing.
    if (std::any_of(values.cbegin(), values.cend(), [](double value) { return value <= 0.0; }))
        return;

    const double average = std::accumulate(values.cbegin(), values.cend(), 0.0)
        / static_cast<double>(values.size());

    stack.beginMacro(QStringLiteral("Normalize items"));
    for (qsizetype i = 0; i < items.size(); ++i) {
        SceneItem *view = items.at(i);
        const double factor =
            mode == Normalize::Size ? std::sqrt(average / values.at(i)) : average / values.at(i);
        const doc::ChangeItemCommand::State state =
            doc::ChangeItemCommand::State::capture(*view->item());
        const QPointF anchor = centerOf(view);
        selection::transformAroundAnchor(view, anchor, [view, factor]() {
            view->item()->scale = view->item()->scale * factor;
        });
        pushChange(stack, view, state, QStringLiteral("Normalize items"));
    }
    stack.endMacro();
}

void arrange(const Scene &scene, doc::UndoStack &stack, Arrange mode, int gap)
{
    // Only items with filenames/ids keep a stable identity; the grid
    // uses the filename order, the line order comes from the positions.
    QVector<SceneItem *> items = orderedSelection(scene);
    if (items.size() < 2)
        return;

    const QPointF center = scene.selectionBounds().center();
    QVector<SceneItem *> ordered;
    QVector<QPointF> targets;
    ordered.reserve(items.size());
    targets.reserve(items.size());

    if (mode == Arrange::Square) {
        double maxWidth = 0.0;
        double maxHeight = 0.0;
        for (SceneItem *view : items) {
            const QRectF rect = boundsOf(view);
            maxWidth = std::max(maxWidth, rect.width() + gap);
            maxHeight = std::max(maxHeight, rect.height() + gap);
        }
        const int rows = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(items.size()))));
        const QPointF diff =
            center - static_cast<double>(rows) / 2.0 * QPointF(maxWidth, maxHeight);

        int index = 0;
        for (int j = 0; j < rows && index < items.size(); ++j) {
            for (int i = 0; i < rows && index < items.size(); ++i) {
                SceneItem *view = items.at(index++);
                const QRectF rect = boundsOf(view);
                targets.append(QPointF(i * maxWidth + (maxWidth - rect.width()) / 2.0,
                                       j * maxHeight + (maxHeight - rect.height()) / 2.0)
                               + diff);
                ordered.append(view);
            }
        }
    } else {
        const bool vertical = mode == Arrange::Vertical;
        QVector<SceneItem *> line = items;
        std::stable_sort(line.begin(), line.end(),
                         [vertical](const SceneItem *a, const SceneItem *b) {
                             const QRectF ra = boundsOf(a);
                             const QRectF rb = boundsOf(b);
                             return vertical ? ra.top() < rb.top() : ra.left() < rb.left();
                         });

        double total = 0.0;
        for (SceneItem *view : line) {
            const QRectF rect = boundsOf(view);
            total += vertical ? rect.height() : rect.width();
        }

        if (vertical) {
            double y = std::round(center.y() - total / 2.0);
            for (SceneItem *view : line) {
                const QRectF rect = boundsOf(view);
                targets.append(QPointF(std::round(center.x() - rect.width() / 2.0), y));
                y += rect.height() + gap;
            }
        } else {
            double x = std::round(center.x() - total / 2.0);
            for (SceneItem *view : line) {
                const QRectF rect = boundsOf(view);
                targets.append(QPointF(x, std::round(center.y() - rect.height() / 2.0)));
                x += rect.width() + gap;
            }
        }
        ordered = line;
    }

    // Move the bounding boxes to their targets: the model position is
    // the local origin, so it follows the box's top-left delta.
    QVector<doc::ChangeItemCommand::State> before;
    before.reserve(ordered.size());
    for (SceneItem *view : ordered)
        before.append(doc::ChangeItemCommand::State::capture(*view->item()));

    for (qsizetype i = 0; i < ordered.size(); ++i) {
        SceneItem *view = ordered.at(i);
        const QPointF delta = targets.at(i) - boundsOf(view).topLeft();
        view->item()->x += delta.x();
        view->item()->y += delta.y();
        view->applyModelState();
    }

    stack.beginMacro(QStringLiteral("Arrange items"));
    for (qsizetype i = 0; i < ordered.size(); ++i)
        pushChange(stack, ordered.at(i), before.at(i), QStringLiteral("Arrange items"));
    stack.endMacro();
}

} // namespace ui::layout
