#include "input_controller.h"

#include "constants.h"

#include "cache/session_cache.h"
#include "downloader.h"
#include "drop.h"
#include "logging.h"
#include "scene.h"
#include "settings.h"
#include "util/format.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#include <memory>

namespace ui {
namespace {

// Inserted items go above everything.
constexpr double kZStep = constants::kZStep;

// Shown when a paste finds nothing it can insert.
QString noPasteMessage()
{
    return QStringLiteral("No image data or text in clipboard or image too big");
}

bool isRemote(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

// Decodes the full display image from an item's authoritative bytes.
QImage decodeDisplayImage(const doc::ItemPtr &item)
{
    if (!item->source || !item->source->isValid())
        return {};
    const QByteArray bytes = item->source->bytes();
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    return reader.read();
}

} // namespace

InputController::InputController(Scene *scene, doc::UndoStack *undoStack, QObject *parent)
    : QObject(parent)
    , scene_(scene)
    , undoStack_(undoStack)
    , downloader_(new Downloader(this))
{
    // Detached payloads are encoded and written here, off the UI thread;
    // one worker is plenty, the spills are rare and order does not
    // matter.
    spillPool_ = new QThreadPool(this);
    spillPool_->setMaxThreadCount(1);
    connect(downloader_, &Downloader::finished, this,
            [this](quint64 requestId, const QByteArray &bytes, const QString &source) {
                const QPointF position = pendingDrops_.take(requestId);
                const doc::LoadedImage loaded = doc::loadImageData(bytes, source);
                if (!loaded.isValid()) {
                    emit message(QStringLiteral("Could not decode downloaded image: %1").arg(source));
                    return;
                }
                insertLoaded(loaded, position);
            });
    connect(downloader_, &Downloader::failed, this,
            [this](quint64 requestId, const QString &source, const QString &error) {
                pendingDrops_.remove(requestId);
                emit message(QStringLiteral("Download failed: %1 (%2)").arg(source, error));
            });
}

InputController::~InputController()
{
    // The runnables keep their payloads alive through shared pointers,
    // but nothing of theirs may outlive this object: wait them out.
    if (spillPool_)
        spillPool_->waitForDone();
}

bool InputController::acceptsMimeData(const QMimeData &data) const
{
    if (dropAccepts(data))
        return true;
    for (const InsertHandler &handler : insertHandlers_) {
        if (handlerClaims(handler, data))
            return true;
    }
    return false;
}

void InputController::addInsertHandler(InsertHandler handler)
{
    insertHandlers_.append(std::move(handler));
}

bool InputController::handlerClaims(const InsertHandler &handler, const QMimeData &data)
{
    for (const QString &format : handler.formats) {
        if (data.hasFormat(format))
            return true;
    }
    return false;
}

void InputController::insertMimeData(const QMimeData &data, const QPointF &scenePos,
                                     double viewScale)
{
    // Registered routes first; the built-in classification below is the
    // fallback, so a drop the app understands behaves exactly as
    // before.
    for (const InsertHandler &handler : std::as_const(insertHandlers_)) {
        if (!handlerClaims(handler, data))
            continue;
        const bool inserted = handler.insert ? handler.insert(data, scenePos, viewScale) : false;
        if (!inserted) {
            emit message(handler.failure.isEmpty() ? QString::fromLatin1(kNoDropMessage)
                                                   : handler.failure);
        }
        return;
    }

    const DropResult result = inspectDrop(data);
    switch (result.kind) {
    case DropKind::Urls:
        insertUrls(result.urls, scenePos);
        return;
    case DropKind::Image:
        insertLoaded(doc::imageToLoaded(result.image), scenePos);
        return;
    case DropKind::None:
        break;
    }
    // The app rejects plain-text drops (a paste of text becomes a
    // note, a drop of text does not).
    emit message(result.message.isEmpty() ? QString::fromLatin1(kNoDropMessage) : result.message);
}

void InputController::insertUrls(const QList<QUrl> &urls, const QPointF &scenePos)
{
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            const doc::LoadedImage loaded = doc::loadImageFile(url.toLocalFile());
            if (!loaded.isValid()) {
                emit message(QStringLiteral("Could not open %1").arg(url.toLocalFile()));
                continue;
            }
            insertLoaded(loaded, scenePos);
        } else if (isRemote(url)) {
            const quint64 requestId = nextRequestId_++;
            pendingDrops_.insert(requestId, scenePos);
            downloader_->fetch(requestId, url);
        } else {
            emit message(QStringLiteral("Unsupported URL: %1").arg(url.toString()));
        }
    }
}

doc::ItemPtr InputController::insertText(const QString &text, const QPointF &scenePos,
                                         double viewScale)
{
    if (text.isEmpty())
        return {};
    auto item = doc::createItem(doc::kTypeText);
    item->setText(text);
    if (viewScale > 0)
        item->scale = 1.0 / viewScale;
    insertItems({item}, scenePos, QStringLiteral("Insert text"));
    return item;
}

void InputController::insertLoaded(const doc::LoadedImage &given, const QPointF &scenePos)
{
    if (!given.isValid())
        return;

    // The Image Storage setting decides how the bytes are kept; the
    // result never grows (see doc::applyStorageMode).
    doc::LoadedImage loaded = given;
    {
        settings::File file(settings::iniPath());
        file.load();
        const QString setting =
            settings::valueOrDefault(file, QStringLiteral("Items/image_storage_format"))
                .toString();
        doc::applyStorageMode(loaded, doc::storageModeForSetting(setting));
    }

    auto item = doc::createItem(doc::kTypePixmap);
    item->source = std::make_shared<doc::BytesSource>(loaded.encoded);
    item->format = loaded.format;
    item->setOriginalSize(loaded.image.size());

    if (!loaded.source.isEmpty()) {
        const QUrl url(loaded.source);
        if (url.isValid() && isRemote(url)) {
            // The same rule as the images exporter, so a browser drop
            // keeps its name through save and export: decoded last path
            // segment, query gone, sanitized, capped. An empty result
            // leaves the filename unset; the export falls back to the
            // item's id.
            item->filename = util::filenameFromUrl(url);
            item->meta.insert(QStringLiteral("origin_url"), loaded.source);
        } else {
            item->filename = QFileInfo(loaded.source).fileName();
        }
    }
    if (!item->filename.isEmpty())
        item->data.insert(QStringLiteral("filename"), item->filename);

    insertItems({item}, scenePos, QStringLiteral("Insert image"));
}

void InputController::insertItems(QVector<doc::ItemPtr> items, const QPointF &scenePos,
                                  const QString &text)
{
    if (items.isEmpty())
        return;
    logging::info(QStringLiteral("Inserted items"),
                  {{QStringLiteral("count"), items.size()},
                   {QStringLiteral("reason"), text}});

    // Bring the new items to the front (stored on the items, so undo
    // and redo keep the stacking).
    double maxZ = 0;
    for (const SceneItem *view : scene_->itemViews())
        maxZ = qMax(maxZ, view->item()->z);
    for (const doc::ItemPtr &item : items) {
        maxZ += kZStep;
        item->z = maxZ;
    }

    undoStack_->beginMacro(text);
    undoStack_->push(std::make_unique<doc::AddItemsCommand>(items, text));
    undoStack_->endMacro();
    scene_->syncDocument();
    // The views exist now, so the group can be placed at the drop point.
    arrangeInserted(items, scenePos);
    if (const auto &document = scene_->document())
        document->setModified(true);

    scene_->clearSelection();
    for (const doc::ItemPtr &item : items) {
        if (SceneItem *view = scene_->itemViewFor(item))
            view->setSelected(true);
    }
    emit itemsInserted();
}

void InputController::arrangeInserted(const QVector<doc::ItemPtr> &items, const QPointF &scenePos)
{
    // Insert placement: the items keep their relative
    // positions (a pasted group stays arranged as it was copied) and
    // the whole group is shifted so its bounding-rect centre lands on
    // the insertion point. Single items are simply centred on it.
    QRectF bounds;
    for (const doc::ItemPtr &item : items) {
        if (const SceneItem *view = scene_->itemViewFor(item)) {
            const QRectF rect = view->sceneBoundingRect();
            bounds = bounds.isNull() ? rect : bounds.united(rect);
        }
    }
    if (bounds.isNull())
        return;
    const QPointF offset = scenePos - bounds.center();
    if (offset.isNull())
        return;
    for (const doc::ItemPtr &item : items) {
        item->x += offset.x();
        item->y += offset.y();
        if (SceneItem *view = scene_->itemViewFor(item))
            view->applyModelState();
    }
}

void InputController::copy()
{
    const QVector<SceneItem *> selected = scene_->selectedItemViews();
    if (selected.isEmpty())
        return;

    internalClipboard_.clear();
    for (SceneItem *view : selected)
        internalClipboard_.append(view->item());

    if (!internalClipboard_.isEmpty()) {
        const doc::ItemPtr &first = internalClipboard_.first();
        auto *mime = new QMimeData;
        mime->setData(QString::fromLatin1(kItemsMime),
                      QByteArray::number(internalClipboard_.size()));
        if (first->isPixmap()) {
            const QImage image = decodeDisplayImage(first);
            if (!image.isNull())
                mime->setImageData(image);
        } else if (first->isText()) {
            mime->setText(first->text());
        }
        QApplication::clipboard()->setMimeData(mime);
    }
}

void InputController::setSessionCache(std::shared_ptr<cache::SessionCache> cache)
{
    sessionCache_ = std::move(cache);
}

void InputController::spillToCache(const doc::ItemPtr &item)
{
    if (!sessionCache_ || !sessionCache_->isAvailable() || !item->isPixmap() || !item->source)
        return;
    // Board-backed sources hold nothing in RAM and stay readable from
    // the open file; only in-memory payloads are worth moving out.
    if (item->source->residentBytes() == 0)
        return;
    const QString key = item->ensureUuid();
    if (key.isEmpty())
        return;

    // The encode and the disk write never run on the UI thread: deleting
    // a large image used to stall the window while its payload was
    // encoded and written. The captures keep the payload alive, and the
    // swap lands back on this thread, so the item's source is only ever
    // written here (and only while it still holds the spilled source).
    const doc::SourcePtr source = item->source;
    const QString format = item->format.isEmpty() ? QStringLiteral("png") : item->format;
    const auto cache = sessionCache_;
    const QPointer<InputController> self(this);
    ++pendingSpillSwaps_;
    spillPool_->start([self, item, source, cache, key, format]() {
        const QByteArray bytes = source->bytes();
        const bool stored =
            !bytes.isEmpty() && cache->put(QStringLiteral("undo"), key, format, bytes);
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self,
            [self, item, source, cache, key, stored]() {
                if (stored && item->source == source) {
                    item->source = std::make_shared<doc::ProviderSource>([cache, key]() {
                        return cache->get(QStringLiteral("undo"), key).value_or(QByteArray());
                    });
                }
                self->finishSpillSwap();
            },
            Qt::QueuedConnection);
    });
}

void InputController::finishSpillSwap()
{
    if (pendingSpillSwaps_ > 0)
        --pendingSpillSwaps_;
}

void InputController::waitForSpills()
{
    spillPool_->waitForDone();
    // The swaps are queued to this thread: run them, so the items really
    // hold their cache-backed sources when this returns.
    while (pendingSpillSwaps_ > 0)
        QCoreApplication::processEvents();
}

void InputController::removeSelection()
{
    const QVector<SceneItem *> selected = scene_->selectedItemViews();
    if (selected.isEmpty())
        return;
    QVector<doc::ItemPtr> items;
    items.reserve(selected.size());
    for (SceneItem *view : selected)
        items.append(view->item());

    auto command = std::make_unique<doc::RemoveItemsCommand>(
        items, [this](const doc::ItemPtr &item) { spillToCache(item); },
        QStringLiteral("Delete"),
        // Delete deselects before removing and
        // selects the restored items again on undo; the scene is brought
        // in line here because the views for restored items do not exist
        // until it syncs.
        [this](const QVector<doc::ItemPtr> &restored, bool removed) {
            scene_->syncDocument();
            if (removed) {
                scene_->clearSelection();
                return;
            }
            for (const doc::ItemPtr &item : restored) {
                if (SceneItem *view = scene_->itemViewFor(item))
                    view->setSelected(true);
            }
        });
    undoStack_->push(std::move(command));
    scene_->syncDocument();
    if (const auto &document = scene_->document())
        document->setModified(true);
}

void InputController::cut()
{
    copy();
    removeSelection();
}

void InputController::paste(const QPointF &scenePos, double viewScale)
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    const bool ownCopy = mime && !mime->data(QString::fromLatin1(kItemsMime)).isEmpty();

    if (ownCopy && !internalClipboard_.isEmpty()) {
        QVector<doc::ItemPtr> copies;
        copies.reserve(internalClipboard_.size());
        for (const doc::ItemPtr &item : internalClipboard_)
            copies.append(item->createCopy());
        insertItems(std::move(copies), scenePos, QStringLiteral("Paste"));
        return;
    }

    if (mime) {
        // Registered routes come after the internal clipboard (a copy of
        // board items still pastes as items) and before the built-in
        // image/URL/text classification.
        for (const InsertHandler &handler : std::as_const(insertHandlers_)) {
            if (!handlerClaims(handler, *mime))
                continue;
            const bool inserted =
                handler.insert ? handler.insert(*mime, scenePos, viewScale) : false;
            if (!inserted)
                emit message(handler.failure.isEmpty() ? noPasteMessage() : handler.failure);
            return;
        }

        const QImage image = QApplication::clipboard()->image();
        if (!image.isNull()) {
            insertLoaded(doc::imageToLoaded(image), scenePos);
            return;
        }
        const DropResult result = inspectDrop(*mime);
        if (result.kind == DropKind::Urls) {
            insertUrls(result.urls, scenePos);
            return;
        }
        if (result.kind == DropKind::Image) {
            insertLoaded(doc::imageToLoaded(result.image), scenePos);
            return;
        }
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty()) {
            insertText(text, scenePos, viewScale);
            return;
        }
    }
    emit message(noPasteMessage());
}

} // namespace ui
