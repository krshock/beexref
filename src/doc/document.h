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
    // board's payloads in memory. Does not mutate the document.
    board::Status save(const QString &path, bool storeThumbnails = true,
                       const board::Progress &progress = {}) const;

    std::shared_ptr<board::Board> board() const { return board_; }
    void close();

private:
    QString path_;
    QVector<ItemPtr> items_;
    bool modified_ = false;
    std::shared_ptr<board::Board> board_;
};

} // namespace doc
