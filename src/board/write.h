#pragma once

#include "error.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>

namespace board {

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
    QByteArray floorData;
    double floorFraction = 0;
    QString floorFormat;
    int origW = 0;
    int origH = 0;
};

// Reports work done out of total while saving.
using Progress = std::function<void(int done, int total)>;

// Writes records to path atomically: a temp file in the same directory
// is written and renamed over the target, so a failure leaves any
// existing file untouched. With storeThumbnails, a floor level is
// generated for every pixmap record that carries none.
// assignedIds, when given, receives the row id of every record in the
// same order, so the caller can write them back to its items.
Status save(const QString &path, const QVector<Record> &records,
            bool storeThumbnails = true, const Progress &progress = {},
            QVector<qint64> *assignedIds = nullptr);

} // namespace board
