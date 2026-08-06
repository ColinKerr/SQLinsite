#pragma once

#include <cstdint>
#include <string>

#include "profile/access_sink.hpp"

struct sqlite3;
struct sqlite3_stmt;

// Run-level metadata for the profile db's single-row `meta` table.
struct ProfileMeta {
    std::string testFile;   // the db that was profiled (--db-file)
    int pageSize = 0;       // page size discovered from its header
    std::string timing;     // "raw" | "relative"
    std::string name;       // TestRun name from the statements file
};

// Writes the SQLite profile database emitted by `sqlinsite profile` (replaces the
// old CSV output). Streams one row per page access into a raw `accesses` table and
// writes a single `meta` row on finish(). Schema is documented in
// plan/commands/PROFILE.md; `visualize --profile-file` aggregates it on import.
class SqliteWriter : public AccessSink {
public:
    // Bump when the on-disk schema changes so visualize can refuse a mismatch.
    static constexpr int kFormatVersion = 1;

    // Creates (truncating any existing file) and opens the profile db. Throws
    // std::runtime_error if it cannot be created.
    explicit SqliteWriter(const std::string& path);
    ~SqliteWriter() override;

    SqliteWriter(const SqliteWriter&) = delete;
    SqliteWriter& operator=(const SqliteWriter&) = delete;

    // AccessSink: appends one row to `accesses` and tallies read/write counts.
    void record(const std::string& sessionName, int statementIndex,
                std::int64_t timeStart, std::int64_t timeEnd,
                std::int64_t pageNumber, AccessType access) override;

    // Writes the `meta` row and commits. Call exactly once, after all accesses.
    void finish(const ProfileMeta& meta);

    std::int64_t readCount() const { return reads_; }
    std::int64_t writeCount() const { return writes_; }
    std::int64_t rowCount() const { return reads_ + writes_; }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_ = nullptr;
    bool finished_ = false;
    std::int64_t reads_ = 0;
    std::int64_t writes_ = 0;
};
