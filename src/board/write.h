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

// Checks that a board just written from records is complete: one item
// row per record, one blob per pixmap record, no orphaned floors, and
// the header the reader expects. save() runs this on the temp file
// before it replaces the target.
Status verifyWritten(Connection &db, const QVector<Record> &records, Format format);

} // namespace board
