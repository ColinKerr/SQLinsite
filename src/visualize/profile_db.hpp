#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

// A single on-disk temp SQLite database holding every profile *source* the
// visualize server knows about — both the profile loaded from --profile-file
// ('input' sources) and the interactive Query-view runs ('query' sources) — plus
// their per-page read/write counts. One unified store means any view can overlay
// any source. Created empty at startup; the file is deleted on destruction.
//
// Schema:
//   sources(sourceId INTEGER PRIMARY KEY,   -- the sel/filter key
//           kind TEXT,                      -- 'input' | 'query'
//           sessionName TEXT,               -- input: session name; query: the SQL
//           sessionId INTEGER)              -- input: statementIndex; query: queryId
//   page_access(sourceId INTEGER, pageNumber INTEGER, reads INTEGER, writes INTEGER)
//
// WAL mode lets the read-pool connections ATTACH it and see committed writes while
// a query run appends a new source (writes go through the single writer_).
class ProfileDb {
public:
    // Creates a fresh temp file with the schema. Throws std::runtime_error on
    // failure.
    ProfileDb();
    ~ProfileDb();

    ProfileDb(const ProfileDb&) = delete;
    ProfileDb& operator=(const ProfileDb&) = delete;

    // Path a read connection should `ATTACH DATABASE … AS prof`.
    const std::string& path() const { return path_; }

    // Imports a `sqlinsite profile` output db (Phase 1 format): one 'input' source
    // per (sessionName, statementIndex), with page_access aggregated from its raw
    // `accesses` rows. Throws if the file isn't a readable profile db.
    void importInputProfile(const std::string& profileDbPath);

    // Appends one 'query' source (sessionName = the SQL, sessionId = queryId) and
    // its per-page counts, returning the new sourceId. Serialized by the caller
    // (the QueryEngine runs one query at a time).
    struct PageCount { std::int64_t pageNumber, reads, writes; };
    std::int64_t addQuerySource(const std::string& sql, int queryId,
                                const std::vector<PageCount>& pages);

private:
    std::string path_;
    sqlite3* writer_ = nullptr;
};
