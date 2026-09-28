#pragma once

#include "error.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>

namespace board {

class Connection;

// One item to write. Positive save ids are preserved; new records get
// fresh ids after the highest existing one. PixmapSource supplies the
// encoded payload lazily, so a save streams one image at a time
// instead of holding every record's bytes in memory. FloorData, when
// present and valid, is an existing floor thumbnail that is written
// verbatim instead of decoding the source again.
struct Record
{
    qint64 saveId = 0;
    QString type;
    double x = 0;
    double y = 0;
    double z = 0;
    double scale = 1;
    double rotation = 0;
    double flip = 1;
    QString dataJson;
    QString metaJson;
    QString uuid;
    QByteArray pixmap;
    QString format;
    QString filename;
    std::function<QByteArray()> pixmapSource;
    // True for an item whose image is known to be gone (a recovered
    // board's placeholder): the row is written and marked in its data
    // instead of failing the save. Native format only.
    bool placeholder = false;
    QByteArray floorData;
    double floorFraction = 0;
    QString floorFormat;
    int origW = 0;
    int origH = 0;
};

// Reports work done out of total while saving.
using Progress = std::function<void(int done, int total)>;

// The on-disk shape: the native .beex, or the legacy upstream .bee
// (items without meta/uuid, sqlar only, upstream header), which is an
// interchange format: it never stores thumbnails and is never saved in
// place.
enum class Format {
    Beex,
    Bee,
};

// Writes records to path atomically: a temp file in the same directory
// is written and renamed over the target, so a failure leaves any
// existing file untouched. With storeThumbnails, a floor level is
// generated for every pixmap record that carries none (native format
// only). A pixmap whose bytes cannot be produced fails the whole save:
// an image row is never written without its image.
// assignedIds, when given, receives the row id of every record in the
// same order, so the caller can write them back to its items.
Status save(const QString &path, const QVector<Record> &records,
            bool storeThumbnails = true, const Progress &progress = {},
            QVector<qint64> *assignedIds = nullptr, Format format = Format::Beex);

// The two halves of save(), for a caller that has its own reader open on
// the target: writeTemp writes and verifies the complete new file next to
// path and returns its path; replaceTemp moves it over the target. A
// caller whose reader is the target itself must release that reader
// between the two calls -- Windows cannot replace an open file, not even
// the same process's own -- and rebuild it afterwards. On a failed
// replace the temp file is kept: it is the only copy of the save.
Result<QString> writeTemp(const QString &path, const QVector<Record> &records,
                          bool storeThumbnails = true, const Progress &progress = {},
                          QVector<qint64> *assignedIds = nullptr, Format format = Format::Beex);
Status replaceTemp(const QString &tempPath, const QString &path);

// Checks that a board just written from records is complete: one item
// row per record, one blob per pixmap record, no orphaned floors, and
// the header the reader expects. save() runs this on the temp file
// before it replaces the target.
Status verifyWritten(Connection &db, const QVector<Record> &records, Format format);

// Applies a change set to an existing native board in place, in one
// transaction: removed ids are deleted (blobs and floors follow through
// the foreign keys), changed records update their row (the encoded blob
// is immutable and stays), and added records are inserted with their
// blobs and floors. The target must already be at the current version;
// the caller falls back to save() otherwise. SQLite's journal makes the
// transaction atomic, but unlike save() there is no temp-file rename
// behind it.
//
// firstId is where new row ids start; pass the caller's highest-ever id
// plus one. Zero derives one from the file, which can reuse an id that
// was deleted in an earlier save -- two items sharing an id would
// collide, so callers that keep an undo history should pass a value.
// assignedIds, when given, receives the row id of every added record in
// order.
Status update(const QString &path, const QVector<Record> &changed, const QVector<Record> &added,
              const QVector<qint64> &removedIds, qint64 firstId = 0, bool storeThumbnails = true,
              const Progress &progress = {}, QVector<qint64> *assignedIds = nullptr);

} // namespace board
