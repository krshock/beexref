#pragma once

#include "doc/image_io.h"
#include "doc/item.h"
#include "doc/undo.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

class QMimeData;

namespace cache {
class SessionCache;
}

namespace ui {

class Downloader;
class Scene;

// The clipboard marker that tells a paste to restore items rather than
// the flattened image. Same format name as the Go ports.
inline constexpr char kItemsMime[] = "beexref/items";

// A payload route for drops and pastes, registered with
// addInsertHandler(): the built-in classification (files, URLs, raw
// images) is the fallback, so an extension claims its own mime formats
// without touching the port. Handlers are tried in
// registration order; the first with one of its formats present wins.
struct InsertHandler
{
    // The mime formats the handler claims. Checked with hasFormat()
    // only: drag-enter must not read payloads (an early read can poison
    // the drop-time read on some sources).
    QStringList formats;
    // Turns the payload into items, usually through insertItems().
    // Returning false reports `failure` (or the usual message) and
    // stops the dispatch, like a built-in extractor that found nothing.
    std::function<bool(const QMimeData &, const QPointF &, double viewScale)> insert;
    QString failure;
};

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

    // Registers a payload route tried before the built-in classification
    // (see InsertHandler).
    void addInsertHandler(InsertHandler handler);

    // Adds items at scenePos as one undo step and selects them; the
    // documented way for handlers and extensions to put items on the
    // board.
    void insertItems(QVector<doc::ItemPtr> items, const QPointF &scenePos,
                     const QString &text);

    // Creates a text item at scenePos with its own undo step and returns
    // it, so the caller can start editing it; empty text inserts nothing.
    doc::ItemPtr insertText(const QString &text, const QPointF &scenePos, double viewScale = 1.0);

    void copy();
    void cut();
    void paste(const QPointF &scenePos, double viewScale = 1.0);

    bool hasInternalClipboard() const { return !internalClipboard_.isEmpty(); }
    // Deletes the selection as one undo step.
    void removeSelection();
    // Forget the copied items, so a paste does not restore them; the
    // reference clears its internal clipboard when a colour is copied.
    void clearInternalClipboard() { internalClipboard_.clear(); }

    // Payloads of items that leave the board are moved here, so a
    // deleted image's bytes do not stay in RAM for undo.
    void setSessionCache(std::shared_ptr<cache::SessionCache> cache);

signals:
    void message(const QString &text);
    // One or more items were inserted (for the memory audit).
    void itemsInserted();

private:
    void insertLoaded(const doc::LoadedImage &loaded, const QPointF &scenePos);
    void insertUrls(const QList<QUrl> &urls, const QPointF &scenePos);
    void spillToCache(const doc::ItemPtr &item);
    void arrangeInserted(const QVector<doc::ItemPtr> &items, const QPointF &scenePos);
    // Whether a registered handler claims the payload (format names only).
    static bool handlerClaims(const InsertHandler &handler, const QMimeData &data);

    Scene *scene_;
    doc::UndoStack *undoStack_;
    Downloader *downloader_;
    std::shared_ptr<cache::SessionCache> sessionCache_;
    QVector<InsertHandler> insertHandlers_;
    QVector<doc::ItemPtr> internalClipboard_;
    QHash<quint64, QPointF> pendingDrops_;
    quint64 nextRequestId_ = 1;
};

} // namespace ui
