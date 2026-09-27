#pragma once

#include "error.h"

#include <QString>

namespace board {
class Connection;
}

namespace board::schema {

// Current native format.
inline constexpr int kUserVersion = 5;
inline constexpr int kApplicationId = 0x52656658;

// Upstream BeeRef's legacy .bee format.
inline constexpr int kBeeUserVersion = 2;
inline constexpr int kBeeApplicationId = 2060242126;

// Creates the format's tables when missing (idempotent).
Status createTables(Connection &db);

// Creates the legacy .bee tables (no meta, uuid or lod).
Status createBeeTables(Connection &db);

// Writes application_id and user_version.
Status writeHeader(Connection &db, int userVersion = kUserVersion,
                   int applicationId = kApplicationId);

Result<int> readUserVersion(Connection &db);
Result<int> readApplicationId(Connection &db);

// True when the items table exists; used to reject non-board files.
Result<bool> hasItemsTable(Connection &db);
// Whether a table with this name exists; the salvage path uses it to see
// what is still readable in a damaged file.
Result<bool> hasTable(Connection &db, const QString &name);

// Migrates a read-write connection to the current version inside one
// transaction. Idempotent: columns are only added when absent, so a
// file stamped older that already carries later columns is fine.
// Versions newer than supported yield an error and leave the file
// untouched.
Status migrateToCurrent(Connection &db);

} // namespace board::schema
