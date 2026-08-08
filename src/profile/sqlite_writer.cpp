#include "profile/sqlite_writer.hpp"

#include <cstdio>
#include <ctime>
#include <stdexcept>

#include <sqlite3.h>

namespace {

[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    const std::string msg = db ? sqlite3_errmsg(db) : "?";
    throw std::runtime_error("profile: " + what + ": " + msg);
}

// UTC ISO-8601 timestamp (e.g. 2026-08-03T14:12:07Z) for the meta row.
std::string isoNowUtc() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace

SqliteWriter::SqliteWriter(const std::string& path) {
    std::remove(path.c_str());
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        fail(db_, "cannot create output file " + path);
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;", nullptr,
                 nullptr, nullptr);
    const char* schema =
        "CREATE TABLE meta("
        "  formatVersion INTEGER, testFile TEXT, pageSize INTEGER,"
        "  timing TEXT, name TEXT, createdAt TEXT);"
        // One row per page access (raw log; visualize aggregates on import).
        "CREATE TABLE accesses("
        "  sessionName TEXT, statementIndex INTEGER,"
        "  timeStart INTEGER, timeEnd INTEGER,"
        "  pageNumber INTEGER, access TEXT);";  // access: 'Read' | 'Write'
    if (sqlite3_exec(db_, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, "create schema");
    }
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    if (sqlite3_prepare_v2(db_, "INSERT INTO accesses VALUES (?,?,?,?,?,?)", -1,
                           &insert_, nullptr) != SQLITE_OK) {
        fail(db_, "prepare insert");
    }
}

SqliteWriter::~SqliteWriter() {
    if (insert_) sqlite3_finalize(insert_);
    if (db_ && !finished_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    if (db_) sqlite3_close(db_);
}

void SqliteWriter::record(const std::string& sessionName, int statementIndex,
                          std::int64_t timeStart, std::int64_t timeEnd,
                          std::int64_t pageNumber, AccessType access) {
    sqlite3_bind_text(insert_, 1, sessionName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insert_, 2, statementIndex);
    sqlite3_bind_int64(insert_, 3, timeStart);
    sqlite3_bind_int64(insert_, 4, timeEnd);
    sqlite3_bind_int64(insert_, 5, pageNumber);
    sqlite3_bind_text(insert_, 6, access == AccessType::Read ? "Read" : "Write",
                      -1, SQLITE_STATIC);
    if (sqlite3_step(insert_) != SQLITE_DONE) fail(db_, "insert access");
    sqlite3_reset(insert_);
    if (access == AccessType::Read) ++reads_; else ++writes_;
}

void SqliteWriter::finish(const ProfileMeta& meta) {
    if (finished_) return;
    sqlite3_stmt* m = nullptr;
    if (sqlite3_prepare_v2(db_, "INSERT INTO meta VALUES (?,?,?,?,?,?)", -1, &m,
                           nullptr) != SQLITE_OK) {
        fail(db_, "prepare meta");
    }
    const std::string createdAt = isoNowUtc();
    sqlite3_bind_int(m, 1, kFormatVersion);
    sqlite3_bind_text(m, 2, meta.testFile.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(m, 3, meta.pageSize);
    sqlite3_bind_text(m, 4, meta.timing.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m, 5, meta.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m, 6, createdAt.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(m) != SQLITE_DONE) fail(db_, "insert meta");
    sqlite3_finalize(m);
    if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, "commit");
    }
    finished_ = true;
}
