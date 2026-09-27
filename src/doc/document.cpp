#include "document.h"

#include "logging.h"

#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>

#include <utility>

namespace doc {
namespace {

// An empty column means "no data"; anything else must be a JSON object,
// so a load can report what it could not read. The caller starts *ok at
// true and this only ever clears it, so both columns can share one flag.
QJsonObject parseJsonObject(const QString &text, bool *ok = nullptr)
{
    if (text.isEmpty())
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject()) {
        if (ok)
            *ok = false;
        return {};
    }
    return document.object();
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
    , damage_(std::move(other.damage_))
    , changes_(std::move(other.changes_))
    , savedIds_(std::move(other.savedIds_))
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
    damage_ = std::move(other.damage_);
    changes_ = std::move(other.changes_);
    savedIds_ = std::move(other.savedIds_);
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
    bool salvagedRows = false;
    if (!rows) {
        // The item rows cannot be read at all: recover the images from
        // the blob store instead of giving up on the scene.
        auto salvaged = board->salvageItems();
        if (!salvaged || salvaged.value().isEmpty())
            return rows.error(); // nothing to recover: the real reason it failed
        rows = std::move(salvaged);
        salvagedRows = true;
    }
    const auto sizes = board->originalSizes(); // best-effort, like the Go port
    const auto floors = board->floorLevels();
    const auto blobIds = board->blobIds(); // best-effort: skip the check if unreadable

    // What the board could not fully account for. The scene still loads;
    // the entries drive the report, the status badge and the log.
    QVector<Damage> damage;
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
        if (salvagedRows) {
            damage.append({Damage::Kind::RecoveredImage, row.id,
                           QStringLiteral("item %1: image recovered without its position")
                               .arg(row.id)});
        }
        bool jsonOk = true;
        item->data = parseJsonObject(row.data, &jsonOk);
        item->meta = parseJsonObject(row.meta, &jsonOk);
        if (!jsonOk) {
            damage.append({Damage::Kind::BadJson, row.id,
                           QStringLiteral("item %1: metadata is not a JSON object").arg(row.id)});
        }
        if (!qIsFinite(row.x) || !qIsFinite(row.y) || !qIsFinite(row.scale)
            || !qIsFinite(row.rotation)) {
            damage.append({Damage::Kind::BadGeometry, row.id,
                           QStringLiteral("item %1: non-finite position or scale").arg(row.id)});
        }

        if (item->isPixmap()) {
            item->filename = item->data.value(QStringLiteral("filename")).toString();
            item->source = std::make_shared<BoardSource>(board, item->id);
            if (blobIds.isOk()) {
                if (!blobIds.value().contains(row.id)) {
                    // A row already marked in the file is an explicit
                    // placeholder from a recovered copy, not damage; one
                    // that is not gets marked now, so the recovered save
                    // can write it instead of refusing.
                    if (!item->data.value(QLatin1String(kPlaceholderKey)).toBool()) {
                        damage.append(
                            {Damage::Kind::MissingBlob, row.id,
                             QStringLiteral("item %1: no image data in the file").arg(row.id)});
                    }
                    item->data.insert(QLatin1String(kPlaceholderKey), true);
                } else {
                    // The image is back; a stale mark would be misleading.
                    item->data.remove(QLatin1String(kPlaceholderKey));
                }
            }
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

    if (const auto orphans = board->orphanedFloorCount(); orphans.isOk() && orphans.value() > 0) {
        damage.append({Damage::Kind::OrphanedFloor, 0,
                       QStringLiteral("%1 thumbnail rows have no item").arg(orphans.value())});
    }
    if (board->isNewerVersion()) {
        damage.append({Damage::Kind::NewerVersion, 0,
                       QStringLiteral("written by a newer version of the app; Save As writes a "
                                      "recovered copy")});
    }

    for (const Damage &entry : damage) {
        logging::warn(QStringLiteral("Board opened with a problem"),
                      {{QStringLiteral("file"), path},
                       {QStringLiteral("kind"), damageLabel(entry.kind)},
                       {QStringLiteral("item"), entry.itemId},
                       {QStringLiteral("detail"), entry.detail}});
    }

    Document document;
    document.path_ = path;
    document.tempDir_ = tempDir;
    document.items_ = std::move(items);
    document.board_ = std::move(board);
    document.damage_ = std::move(damage);
    // The file holds every current item, so there is nothing to save yet.
    document.clearChanges();
    return document;
}

Document Document::create()
{
    return {};
}

QString damageLabel(Damage::Kind kind)
{
    switch (kind) {
    case Damage::Kind::MissingBlob:
        return QStringLiteral("missing image data");
    case Damage::Kind::BadJson:
        return QStringLiteral("invalid metadata");
    case Damage::Kind::BadGeometry:
        return QStringLiteral("invalid geometry");
    case Damage::Kind::OrphanedFloor:
        return QStringLiteral("orphaned thumbnail");
    case Damage::Kind::RecoveredImage:
        return QStringLiteral("recovered image");
    case Damage::Kind::NewerVersion:
        return QStringLiteral("newer board version");
    }
    return QStringLiteral("problem");
}

int Document::activeDamageCount() const
{
    int count = 0;
    for (const Damage &entry : damage_) {
        if (entry.itemId == 0 || itemById(entry.itemId))
            ++count;
    }
    return count;
}

int Document::placeholderCount() const
{
    int count = 0;
    for (const ItemPtr &item : items_) {
        if (item->isPixmap() && item->data.value(QLatin1String(kPlaceholderKey)).toBool())
            ++count;
    }
    return count;
}

bool Document::hasChangedOnDisk()
{
    return board_ && board_->hasChangedOnDisk();
}

void Document::addItem(const ItemPtr &item)
{
    items_.append(item);
    noteItemAdded(item);
}

void Document::insertItem(qsizetype index, const ItemPtr &item)
{
    items_.insert(qBound(qsizetype(0), index, items_.size()), item);
    noteItemAdded(item);
}

void Document::removeItem(const ItemPtr &item)
{
    const qsizetype index = indexOf(item);
    if (index < 0)
        return;
    items_.remove(index);
    // An item that never reached the file is not a file change at all;
    // one the file holds becomes a pending delete.
    changes_.added.remove(item.get());
    changes_.changed.remove(item.get());
    if (item->id > 0 && savedIds_.contains(item->id))
        changes_.removedIds.insert(item->id);
}

void Document::noteItemChanged(const ItemPtr &item)
{
    if (!item)
        return;
    dirty_.insert(item.get());
    if (item->id > 0 && savedIds_.contains(item->id))
        changes_.changed.insert(item.get());
    else
        changes_.added.insert(item.get());
}

void Document::noteItemAdded(const ItemPtr &item)
{
    if (!item)
        return;
    // Restoring a removed item (undo) takes its id back out of the
    // removal set; a row the file already holds is an update, anything
    // else an insert.
    changes_.removedIds.remove(item->id);
    if (item->id > 0 && savedIds_.contains(item->id))
        changes_.changed.insert(item.get());
    else
        changes_.added.insert(item.get());
}

void Document::clearChanges()
{
    changes_ = {};
    savedIds_.clear();
    for (const ItemPtr &item : items_) {
        if (item->id > 0)
            savedIds_.insert(item->id);
    }
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
                      const board::Progress &progress, bool createNew)
{
    // A board with problems is never written back over its source: the
    // recovered copy is what makes the placeholders explicit.
    if (damaged() && !createNew) {
        return board::Error{0,
                            QStringLiteral("This board was opened with problems; use Save As "
                                           "to write a recovered copy."),
                            path_};
    }

    // The file changed since it was opened (another instance, a sync
    // client, a replaced file): an in-place write would clobber that, so
    // the caller writes a copy instead.
    if (!createNew && hasChangedOnDisk()) {
        return board::Error{0,
                            QStringLiteral("The file changed on disk since it was opened; use "
                                           "Save As to write a copy."),
                            path_};
    }

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
    // The file now matches what the document knows: the placeholders are
    // explicit rows and everything else is complete, so the damage list
    // and the change set are empty again.
    damage_.clear();
    clearChanges();
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
            record.placeholder = item->data.value(QLatin1String(kPlaceholderKey)).toBool();
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
