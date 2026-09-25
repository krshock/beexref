#pragma once

#include "item.h"

#include "board/board.h"
#include "board/write.h"

#include <QString>
#include <QVector>

#include <memory>

namespace doc {

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
    // the encoded buffers that now live in the file (the reference's
    // adopt_lod). Reopens the board on the current path first, because
    // the save replaced the file and an old connection would read stale
    // bytes.
    void adoptFileSources();
    bool isModified() const { return modified_; }
    void setModified(bool modified) { modified_ = modified; }

    QVector<ItemPtr> &items() { return items_; }
    const QVector<ItemPtr> &items() const { return items_; }

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
    // file starts with fresh ids, like the reference.
    //
    // After a successful save, adoptFileSources() points the images at
    // the file and frees their in-RAM encoded buffers.
    board::Status save(const QString &path, bool storeThumbnails = true,
                       const board::Progress &progress = {}, bool createNew = false) const;

    // Writes the legacy upstream .bee format (interchange only): no
    // thumbnails, no meta/uuid, the scene's ids and path untouched.
    board::Status exportBee(const QString &path, const board::Progress &progress = {}) const;

    std::shared_ptr<board::Board> board() const { return board_; }
    void close();

private:
    // One writer record per item, in item order.
    QVector<board::Record> buildRecords() const;

    QString path_;
    QString tempDir_;
    QVector<ItemPtr> items_;
    bool modified_ = false;
    std::shared_ptr<board::Board> board_;
};

} // namespace doc
