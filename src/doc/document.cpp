#include "document.h"

#include "logging.h"

#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>

#include <utility>

namespace doc {
namespace {

QJsonObject parseJsonObject(const QString &text)
{
    if (text.isEmpty())
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    return document.isObject() ? document.object() : QJsonObject();
}

// Extra save data: images always carry their
// filename, opacity, grayscale flag and crop, even at their defaults, so
// a freshly created item serializes like the Python and Go ports.
QJsonObject savedData(const Item &item)
{
    QJsonObject data = item.data;
    if (!item.isPixmap())
        return data;
    data.insert(QStringLiteral("filename"), item.filename);
    data.insert(QStringLiteral("opacity"), item.opacity());
    data.insert(QStringLiteral("grayscale"), item.grayscale());
    const QSize size = item.originalSize();
    if (size.isValid() && !size.isEmpty()) {
        const QRectF crop =
            item.hasCrop() ? item.crop() : QRectF(0, 0, size.width(), size.height());
        data.insert(QStringLiteral("crop"),
                    QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()});
    }
    return data;
}

QString jsonString(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QSize headerSize(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer);
    return reader.size();
}

} // namespace

Document::~Document()
{
    close();
}

Document::Document(Document &&other) noexcept
    : path_(std::move(other.path_))
    , tempDir_(std::move(other.tempDir_))
    , items_(std::move(other.items_))
    , modified_(other.modified_)
    , board_(std::move(other.board_))
{
    other.modified_ = false;
}

Document &Document::operator=(Document &&other) noexcept
{
    if (this == &other)
        return *this;
    close();
    path_ = std::move(other.path_);
    tempDir_ = std::move(other.tempDir_);
    items_ = std::move(other.items_);
    modified_ = other.modified_;
    board_ = std::move(other.board_);
    other.modified_ = false;
    return *this;
}

board::Result<Document> Document::open(const QString &path, const QString &tempDir,
                                const board::Progress &progress)
{
    auto opened = board::Board::open(path, tempDir);
    if (!opened)
        return opened.error();
    auto board = std::make_shared<board::Board>(opened.take());

    auto rows = board->items();
    if (!rows)
        return rows.error();
    const auto sizes = board->originalSizes(); // best-effort, like the Go port
    const auto floors = board->floorLevels();

    QVector<ItemPtr> items;
    items.reserve(rows.value().size());
    for (qsizetype i = 0; i < rows.value().size(); ++i) {
        if (progress)
            progress(static_cast<int>(i), static_cast<int>(rows.value().size()));
        const board::ItemRow &row = rows.value().at(i);

        auto item = std::make_shared<Item>(row.type);
        item->id = row.id;
        item->uuid = row.uuid;
        item->x = row.x;
        item->y = row.y;
        item->z = row.z;
        item->scale = row.scale;
        item->rotation = row.rotation;
        item->flip = row.flip == 1 ? 1.0 : -1.0;
        item->data = parseJsonObject(row.data);
        item->meta = parseJsonObject(row.meta);

        if (item->isPixmap()) {
            item->filename = item->data.value(QStringLiteral("filename")).toString();
            item->source = std::make_shared<BoardSource>(board, item->id);
            // The true encoded format comes from the sqlar name, not the
            // thumbnail: a JPEG original may have a PNG floor.
            if (auto format = board->blobFormat(item->id); format.isOk() && !format.value().isEmpty())
                item->format = format.value();
            if (sizes.isOk() && sizes.value().contains(item->id)) {
                item->setOriginalSize(sizes.value().value(item->id));
            } else if (auto blob = board->blob(item->id); blob.isOk()) {
                item->setOriginalSize(headerSize(blob.value()));
            }
            if (floors.isOk() && floors.value().contains(item->id)) {
                const board::FloorLevel &floor = floors.value().value(item->id);
                item->floorData = floor.data;
                item->floorFraction = floor.fraction;
                item->floorFormat = floor.format;
                if (item->format.isEmpty())
                    item->format = floor.format;
            }
        }
        items.append(std::move(item));
    }
    if (progress)
        progress(static_cast<int>(rows.value().size()), static_cast<int>(rows.value().size()));

    Document document;
    document.path_ = path;
    document.tempDir_ = tempDir;
    document.items_ = std::move(items);
    document.board_ = std::move(board);
    return document;
}

Document Document::create()
{
    return {};
}

void Document::addItem(const ItemPtr &item)
{
    items_.append(item);
}

void Document::insertItem(qsizetype index, const ItemPtr &item)
{
    items_.insert(qBound(qsizetype(0), index, items_.size()), item);
}

void Document::removeItem(const ItemPtr &item)
{
    const qsizetype index = indexOf(item);
    if (index >= 0)
        items_.remove(index);
}

qsizetype Document::indexOf(const Item &item) const
{
    for (qsizetype i = 0; i < items_.size(); ++i) {
        if (items_.at(i).get() == &item)
            return i;
    }
    return -1;
}

qsizetype Document::indexOf(const ItemPtr &item) const
{
    return indexOf(*item);
}

ItemPtr Document::itemById(qint64 id) const
{
    for (const ItemPtr &item : items_) {
        if (item->id == id)
            return item;
    }
    return {};
}

bool Document::hasBlob(const Item &item) const
{
    return item.hasSource();
}

board::Result<QByteArray> Document::blob(const Item &item) const
{
    if (!item.hasSource()) {
        return board::Error{0,
                     QStringLiteral("No readable encoded data for item %1").arg(item.id),
                     path_};
    }
    QByteArray bytes = item.source->bytes();
    if (bytes.isEmpty()) {
        return board::Error{0,
                     QStringLiteral("No readable encoded data for item %1").arg(item.id),
                     path_};
    }
    return bytes;
}

board::Status Document::save(const QString &path, bool storeThumbnails,
                      const board::Progress &progress, bool createNew) const
{
    if (createNew) {
        // A new file gets fresh row ids.
        for (const ItemPtr &item : items_)
            item->id = 0;
    }

    const QVector<board::Record> records = buildRecords();
    QVector<qint64> ids;
    const board::Status status = board::save(path, records, storeThumbnails, progress, &ids);
    if (!status)
        return status;
    // The writer assigned a row id to every record, in order.
    if (ids.size() == items_.size()) {
        for (qsizetype i = 0; i < items_.size(); ++i)
            items_.at(i)->id = ids.at(i);
    }
    return status;
}

board::Status Document::exportBee(const QString &path, const board::Progress &progress) const
{
    // The legacy format is import/export only: the scene's own ids and
    // sources are untouched and the document keeps its .beex path.
    return board::save(path, buildRecords(), false, progress, nullptr, board::Format::Bee);
}

QVector<board::Record> Document::buildRecords() const
{
    QVector<board::Record> records;
    records.reserve(items_.size());
    for (const ItemPtr &item : items_) {
        board::Record record;
        record.saveId = item->id;
        record.type = item->type;
        record.x = item->x;
        record.y = item->y;
        record.z = item->z;
        record.scale = item->scale;
        record.rotation = item->rotation;
        record.flip = item->flip;
        record.dataJson = jsonString(savedData(*item));
        record.metaJson = jsonString(item->meta);
        record.uuid = item->uuid;

        if (item->isPixmap()) {
            // Capture the immutable source, not the item: the writer may
            // run on a worker while the document keeps changing.
            const SourcePtr source = item->source;
            if (source && source->isValid())
                record.pixmapSource = [source]() { return source->bytes(); };
            record.format = item->format;
            record.filename = item->filename.isEmpty()
                ? item->data.value(QStringLiteral("filename")).toString()
                : item->filename;
            record.floorData = item->floorData;
            record.floorFraction = item->floorFraction;
            record.floorFormat = item->floorFormat;
            const QSize size = item->originalSize();
            record.origW = size.width();
            record.origH = size.height();
        }
        records.append(record);
    }
    return records;
}

void Document::adoptFileSources()
{
    if (path_.isEmpty())
        return;
    // The save replaced the file; an old connection would keep reading
    // the previous bytes, so reopen the board on the current path.
    auto opened = board::Board::open(path_, tempDir_);
    if (!opened) {
        // The save itself succeeded; without the reopen, LOD levels read
        // from RAM until the next open. Say so instead of swallowing it.
        logging::warn(QStringLiteral("Saved board could not be reopened for LOD sources"),
                      {{QStringLiteral("file"), path_},
                       {QStringLiteral("error"), opened.error().toString()}});
        return;
    }
    board_ = std::make_shared<board::Board>(opened.take());

    for (const ItemPtr &item : items_) {
        if (!item->isPixmap() || item->id == 0)
            continue;
        // The encoded bytes now live in the file; reading them through
        // the board frees the in-RAM copy (levels stay in RAM).
        item->source = std::make_shared<BoardSource>(board_, item->id);
    }
}

void Document::close()
{
    if (board_) {
        board_->close();
        board_.reset();
    }
}

} // namespace doc
