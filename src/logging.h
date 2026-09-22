#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QVariant>

namespace logging {

// Levels mirror the Go ports' slog levels; Trace and Debug render as
// "INFO-8" and "DEBUG-4" the way slog's text handler prints them.
enum class Level : int {
    Trace = -8,
    Debug = -4,
    Info = 0,
    Warn = 4,
    Error = 8,
};

using Attrs = QList<QPair<QString, QVariant>>;

// Opens the rotating log file (1 MB, one backup) and writes the session
// marker as its first record. Console output is limited to consoleLevel;
// the file always receives every level.
void setup(Level consoleLevel, const QString &logFile);

void log(Level level, const QString &message, const Attrs &attrs = {});
void trace(const QString &message, const Attrs &attrs = {});
void debug(const QString &message, const Attrs &attrs = {});
void info(const QString &message, const Attrs &attrs = {});
void warn(const QString &message, const Attrs &attrs = {});
void error(const QString &message, const Attrs &attrs = {});

// Current session's log lines: everything after the last session marker
// in the log file.
QString sessionText(const QString &logFile);

} // namespace logging
