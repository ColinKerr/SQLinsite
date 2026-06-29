#pragma once

// Name of the wrapping VFS. Pass this to sqlite3_open_v2 to route a connection
// through the profiler.
extern const char* const kSQLINSITEVfsName;

// Registers the "sqlinsite" wrapping VFS (non-default). Idempotent.
// Returns an SQLite result code (SQLITE_OK on success).
int registerSQLINSITEVfs();
