#include "input_controller.h"

#include "cache/session_cache.h"
#include "downloader.h"
#include "drop.h"
#include "logging.h"
#include "scene.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QUrl>

#include <memory>

namespace ui {
namespace {

constexpr double kInsertGap = 0.0;

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

bool InputController::acceptsMimeData(const QMimeData &data) const
{
    return dropAccepts(data);
}

void InputController::insertMimeData(const QMimeData &data, const QPointF &scenePos,
                                     double viewScale)
{
    Q_UNUSED(viewScale);
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
    // The reference rejects plain-text drops (a paste of text becomes a
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

void InputController::insertText(const QString &text, const QPointF &scenePos, double viewScale)
{
    if (text.isEmpty())
        return;
    auto item = std::make_shared<doc::Item>(doc::kTypeText);
    item->setText(text);
    if (viewScale > 0)
        item->scale = 1.0 / viewScale;
    insertItems({item}, scenePos, QStringLiteral("Insert text"));
}

void InputController::insertLoaded(const doc::LoadedImage &loaded, const QPointF &scenePos)
{
    if (!loaded.isValid())
        return;

    auto item = std::make_shared<doc::Item>(doc::kTypePixmap);
    item->source = std::make_shared<doc::BytesSource>(loaded.encoded);
    item->format = loaded.format;
    item->setOriginalSize(loaded.image.size());

    if (!loaded.source.isEmpty()) {
        const QUrl url(loaded.source);
        if (url.isValid() && isRemote(url)) {
            item->filename = url.fileName();
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
    arrangeInserted(items, scenePos);
    logging::info(QStringLiteral("Inserted items"),
                  {{QStringLiteral("count"), items.size()},
                   {QStringLiteral("reason"), text}});

    undoStack_->beginMacro(text);
    undoStack_->push(std::make_unique<doc::AddItemsCommand>(items, text));
    undoStack_->endMacro();
    scene_->syncDocument();
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
    // Simple left-to-right row centred on the drop point; the reference
    // arranges the default (optimal packing) style here, which belongs
    // to the arrange work.
    QRectF bounds;
    double x = 0;
    for (const doc::ItemPtr &item : items) {
        const QSize size = item->originalSize().isValid() ? item->originalSize() : QSize(100, 100);
        const double width = size.width() * item->scale;
        const double height = size.height() * item->scale;
        item->x = x;
        item->y = 0;
        const QRectF rect(x, 0, width, height);
        bounds = bounds.isNull() ? rect : bounds.united(rect);
        x += width + kInsertGap;
    }
    if (bounds.isNull())
        return;
    const QPointF offset = scenePos - bounds.center();
    for (const doc::ItemPtr &item : items) {
        item->x += offset.x();
        item->y += offset.y();
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

    const QByteArray bytes = item->source->bytes();
    if (bytes.isEmpty())
        return;
    const QString key = item->ensureUuid();
    if (key.isEmpty())
        return;
    const QString format = item->format.isEmpty() ? QStringLiteral("png") : item->format;
    if (!sessionCache_->put(QStringLiteral("undo"), key, format, bytes))
        return;

    const auto cache = sessionCache_;
    item->source = std::make_shared<doc::ProviderSource>(
        [cache, key]() { return cache->get(QStringLiteral("undo"), key).value_or(QByteArray()); });
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
        QStringLiteral("Delete"));
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
    emit message(QStringLiteral("No image data or text in clipboard or image too big"));
}

} // namespace ui
