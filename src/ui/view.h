#pragma once

#include "level_loader.h"
#include "scene.h"

#include <QGraphicsView>
#include <QPoint>
#include <QPointF>
#include <QVector>

#include <functional>

class QMimeData;

namespace ui {

// Canvas view: pan, zoom, fit, rubber-band selection and
// threshold-based moving of the selected items. Requests display
// levels for the visible items through the loader. Drags are
// classified with a filter and forwarded as mimeDropped.
class View : public QGraphicsView
{
    Q_OBJECT

public:
    using MimeFilter = std::function<bool(const QMimeData &)>;

    explicit View(QWidget *parent = nullptr);

    void setLevelLoader(LevelLoader *loader);
    void setBoardScene(Scene *scene);
    void setMimeFilter(MimeFilter filter) { mimeFilter_ = std::move(filter); }

    void fitScene();
    void fitSelection();

    // delta is a wheel angleDelta step; anchor is in viewport pixels.
    void zoomAt(int delta, const QPoint &anchor);

    // Requests levels for visible items whose current level is too
    // coarse for the current zoom.
    void requestVisibleLevels();

signals:
    // A drop the filter accepted, with its position in scene
    // coordinates. The mime data stays owned by the event.
    void mimeDropped(const QMimeData *data, const QPointF &scenePos);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void panBy(const QPoint &delta);
    void scheduleLevelRequest();
    double zoomExtent(bool maximum) const;

    LevelLoader *loader_ = nullptr;
    Scene *boardScene_ = nullptr;
    MimeFilter mimeFilter_;
    quint64 nextRequestId_ = 1;
    bool levelRequestScheduled_ = false;

    bool panning_ = false;
    QPoint panStart_;
    bool moving_ = false;
    bool moveStarted_ = false;
    QPoint pressPos_;
    QPointF pressScenePos_;
    QVector<QPair<SceneItem *, QPointF>> moveStarts_;

    static constexpr double kMoveThreshold = 3.0; // viewport pixels
    static constexpr double kMaxZoomExtent = 10000000.0;
    static constexpr double kMinZoomExtent = 50.0;
};

} // namespace ui
