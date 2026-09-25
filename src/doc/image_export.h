#pragma once

#include "item.h"

#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace doc {

// Export every pixmap item to a directory, ported from the reference's
// ImagesToDirectoryExporter (fileio/export.py). The bytes come straight
// from each item's source, so unedited images pass through byte for
// byte; no re-encoding ever happens here.

// What to do when the target file already exists.
enum class ExportConflict {
    Skip,
    SkipAll,
    Overwrite,
    OverwriteAll,
};

struct ImageExportSummary
{
    int written = 0;
    int skipped = 0;
    bool cancelled = false;
    // One human-readable "path: message" per failure.
    QVector<QString> errors;

    bool ok() const { return errors.isEmpty(); }
};

// The reference's export_filename(): "{save_id:04}-{basename}.{format}",
// or "{save_id:04}.{format}" when the item has no source filename.
// For http(s) filenames (boards from the Python reference store the full
// URL) the decoded last path segment is used instead, with characters
// unsafe in file names replaced and the stem capped to one path
// component; local paths keep the reference's exact rule.
QString exportFilename(const QString &filename, const QString &format, qint64 saveId);

// Writes one file per pixmap item into dir. Items without a save id get
// fresh ids after the highest one, matching the reference. `resolve` is
// called with the target path when the file already exists; returning
// nullopt aborts the whole export. `progress` (optional) reports
// done/total. `cancelled` (optional) is polled before each file, like
// the reference's worker.canceled check; the summary's `cancelled` flag
// is set when either aborts the run.
ImageExportSummary exportImages(
    const QVector<ItemPtr> &items, const QString &dir,
    const std::function<std::optional<ExportConflict>(const QString &)> &resolve,
    const std::function<void(int done, int total)> &progress = {},
    const std::function<bool()> &cancelled = {});

} // namespace doc
