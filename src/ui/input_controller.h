#pragma once

#include "doc/image_io.h"
#include "doc/item.h"
#include "doc/undo.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QVector>

class QMimeData;

namespace ui {

class Downloader;
class Scene;

// The clipboard marker that tells a paste to restore items rather than
// the flattened image. Same format name as the Go ports.
inline constexpr char kItemsMime[] = "beexref/items";

// Turns clipboard and drop payloads into board items: local files and
// raw images insert immediately, remote URLs download and insert when
// they arrive. Every insertion is a single undo step.
class InputController : public QObject
{
    Q_OBJECT

public:
    InputController(Scene *scene, doc::UndoStack *undoStack, QObject *parent = nullptr);

    bool acceptsMimeData(const QMimeData &data) const;
    void insertMimeData(const QMimeData &data, const QPointF &scenePos, double viewScale = 1.0);

    void copy();
    void cut();
    void paste(const QPointF &scenePos, double viewScale = 1.0);

    bool hasInternalClipboard() const { return !internalClipboard_.isEmpty(); }

signals:
    void message(const QString &text);

private:
    void insertLoaded(const doc::LoadedImage &loaded, const QPointF &scenePos);
    void insertItems(QVector<doc::ItemPtr> items, const QPointF &scenePos, const QString &text);
    void insertUrls(const QList<QUrl> &urls, const QPointF &scenePos);
    void insertText(const QString &text, const QPointF &scenePos, double viewScale);
    void removeSelection();
    void arrangeInserted(const QVector<doc::ItemPtr> &items, const QPointF &scenePos);

    Scene *scene_;
    doc::UndoStack *undoStack_;
    Downloader *downloader_;
    QVector<doc::ItemPtr> internalClipboard_;
    QHash<quint64, QPointF> pendingDrops_;
    quint64 nextRequestId_ = 1;
};

} // namespace ui
