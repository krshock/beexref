#pragma once

#include "item.h"

#include "board/board.h"
#include "board/write.h"

#include <QString>
#include <QSet>
#include <QVector>

#include <memory>

namespace doc {

// One thing a board could not fully account for when it was opened. The
// scene still loads; the entry lets the UI say what is missing, and
// keeps the original file protected on save.
struct Damage
{
    enum class Kind {
        MissingBlob,    // a pixmap row without its encoded image
        BadJson,        // data/meta that is not a JSON object
        BadGeometry,    // non-finite position, scale or rotation
        OrphanedFloor,  // a floor row whose item is gone (board-level)
        RecoveredImage, // an image salvaged without its item row
        NewerVersion,   // written by a newer app version (board-level)
    };

    Kind kind = Kind::MissingBlob;
    qint64 itemId = 0; // 0 for board-level entries
    QString detail;    // one short line for the report and the log
};

// data key marking an item whose image is gone: a recovered save writes
// the row as an explicit placeholder instead of refusing.
inline constexpr char kPlaceholderKey[] = "placeholder";

// What changed since the last save, for the incremental writer: items
// with no row in the file yet, existing items whose state changed, and
// the ids of rows to remove. Empty after an open or a save. It stays
// dark until the incremental writer consumes it.
struct Changes
{
    QSet<const Item *> added;
    QSet<const Item *> changed;
    QSet<qint64> removedIds;

    bool isEmpty() const { return added.isEmpty() && changed.isEmpty() && removedIds.isEmpty(); }
};

// User-facing label for a kind, shared by the log and the report.
QString damageLabel(Damage::Kind kind);

// One open board: its items, the read-only board connection, and the
// migrated temp copy that must be removed on close.
//
// Thread contract (inherited from the connection policy):
//   * Document and Item state are UI-thread-only.
//   * Item::source is immutable and may be read from any thread.
//   * Workers must be stopped before close(): close() closes the
//     connection and deletes the temp copy, and sources then return
//     empty bytes.
class Document
{
public:
    Document() = default;
    ~Document();
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;
    Document(Document &&other) noexcept;
    Document &operator=(Document &&other) noexcept;

    // Loads a board. Older files are migrated on a temp copy below
    // tempDir; the original is never written. Progress may be empty.
    static board::Result<Document> open(const QString &path, const QString &tempDir,
                                        const board::Progress &progress = {});

    // Empty document with no backing file.
    static Document create();

    const QString &path() const { return path_; }
    // Records the file the document was last written to (or opened from).
    void setPath(const QString &path) { path_ = path; }
    // Points saved images at the board as their LOD source and releases
    // the encoded buffers that now live in the file. Reopens the board
    // on the current path first, because
    // the save replaced the file and an old connection would read stale
    // bytes.
    void adoptFileSources();
    bool isModified() const { return modified_; }
    void setModified(bool modified) { modified_ = modified; }

    QVector<ItemPtr> &items() { return items_; }
    const QVector<ItemPtr> &items() const { return items_; }

    // Everything the board could not fully account for at load; empty
    // for a healthy board. The scene loads with these either way.
    const QVector<Damage> &damage() const { return damage_; }
    // Damage entries that still refer to an item in the document
    // (board-level entries always count), so deleting a broken item
    // lowers the count the status bar shows.
    int activeDamageCount() const;
    // Whether anything the board could not fully account for is still in
    // the scene. Deleting the broken items fixes their entries, so once
    // none are left an in-place save is allowed again; board-level
    // entries (orphaned floors) stay until a recovered copy is written.
    bool damaged() const { return activeDamageCount() > 0; }
    // Items whose image is known to be gone, so a recovered save can say
    // how many will be written as placeholders.
    int placeholderCount() const;
    // A successful save writes the placeholders explicitly, so the
    // in-memory document matches the file again.
    void clearDamage() { damage_.clear(); }

    // Items whose state changed since the UI last synced; commands call
    // noteItemChanged(), so a canvas refresh only reapplies the items
    // that actually moved (a board with thousands of items would
    // otherwise reapply every transform on every undo).
    void noteItemChanged(const ItemPtr &item);
    const QSet<const Item *> &dirtyItems() const { return dirty_; }
    void clearDirtyItems() { dirty_.clear(); }

    // The save-side change set: new rows, updated rows and removed ids
    // since the file was last written. A successful save empties it.
    const Changes &changes() const { return changes_; }
    void clearChanges();

    void addItem(const ItemPtr &item);
    void insertItem(qsizetype index, const ItemPtr &item);
    void removeItem(const ItemPtr &item);
    qsizetype indexOf(const Item &item) const;
    qsizetype indexOf(const ItemPtr &item) const;
    ItemPtr itemById(qint64 id) const;

    // Whether the item's encoded bytes can be produced.
    bool hasBlob(const Item &item) const;
    board::Result<QByteArray> blob(const Item &item) const;

    // Writes the document in the native format. Blobs are read one at a
    // time through the item sources, so a save never holds the whole
    // board's payloads in memory.
    // Writing assigns every item its row id in the saved file. On a
    // "create new" save (Save As) the ids are cleared first, so a new
    // file starts with fresh ids.
    //
    // After a successful save, adoptFileSources() points the images at
    // the file and frees their in-RAM encoded buffers. A damaged board
    // refuses an in-place save (createNew false); Save As writes a
    // recovered copy whose imageless items are explicit placeholders.
    board::Status save(const QString &path, bool storeThumbnails = true,
                       const board::Progress &progress = {}, bool createNew = false);

    // Writes the legacy upstream .bee format (interchange only): no
    // thumbnails, no meta/uuid, the scene's ids and path untouched.
    board::Status exportBee(const QString &path, const board::Progress &progress = {}) const;

    std::shared_ptr<board::Board> board() const { return board_; }
    void close();

private:
    // One writer record per item, in item order.
    QVector<board::Record> buildRecords() const;
    // Records an item that entered the document (add, insert or undo of
    // a removal): a row the file already holds is an update, anything
    // else is an insert, and a pending removal of that id is cancelled.
    void noteItemAdded(const ItemPtr &item);

    QString path_;
    QString tempDir_;
    QVector<ItemPtr> items_;
    QVector<Damage> damage_;
    Changes changes_;
    QSet<qint64> savedIds_;
    QSet<const Item *> dirty_;
    bool modified_ = false;
    std::shared_ptr<board::Board> board_;
};

} // namespace doc
