#include "visualize/profile_db.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <sqlite3.h>

namespace {

[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    const std::string msg = db ? sqlite3_errmsg(db) : "?";
    throw std::runtime_error("visualize: profile db: " + what + ": " + msg);
}

void exec(sqlite3* db, const std::string& sql) {
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db, "exec [" + sql.substr(0, 40) + "…]");
    }
}

// A unique temp-file path for this process's profile db.
std::string makeTempPath() {
    static std::atomic<unsigned> counter{0};
    const auto dir = std::filesystem::temp_directory_path();
    const std::string name = "sqlinsite-profile-" +
                             std::to_string(
#if defined(_WIN32)
                                 static_cast<long>(_getpid())
#else
                                 static_cast<long>(::getpid())
#endif
                                 ) +
                             "-" + std::to_string(counter++) + ".sqlite";
    return (dir / name).string();
}

}  // namespace

ProfileDb::ProfileDb() : path_(makeTempPath()) {
    std::remove(path_.c_str());
    if (sqlite3_open_v2(path_.c_str(), &writer_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        fail(writer_, "cannot create " + path_);
    }
    exec(writer_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=OFF;");
    exec(writer_,
         "CREATE TABLE sources(sourceId INTEGER PRIMARY KEY, kind TEXT, "
         "sessionName TEXT, sessionId INTEGER);"
         "CREATE TABLE page_access(sourceId INTEGER, pageNumber INTEGER, "
         "reads INTEGER, writes INTEGER);"
         "CREATE INDEX page_access_page ON page_access(pageNumber);"
         "CREATE INDEX page_access_src ON page_access(sourceId);");
}

ProfileDb::~ProfileDb() {
    if (writer_) sqlite3_close(writer_);
    // Remove the temp file and any WAL sidecars.
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
}

void ProfileDb::importInputProfile(const std::string& profileDbPath) {
    char* attach = sqlite3_mprintf("ATTACH DATABASE %Q AS input", profileDbPath.c_str());
    const int rc = sqlite3_exec(writer_, attach, nullptr, nullptr, nullptr);
    sqlite3_free(attach);
    if (rc != SQLITE_OK) fail(writer_, "cannot attach profile db " + profileDbPath);

    // Require the raw `accesses` table (produced by `sqlinsite profile`).
    if (sqlite3_exec(writer_, "SELECT 1 FROM input.accesses LIMIT 0", nullptr,
                     nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(writer_, "DETACH DATABASE input", nullptr, nullptr, nullptr);
        throw std::runtime_error(
            "visualize: --profile-file is not a sqlinsite profile db (no 'accesses' table): " +
            profileDbPath);
    }

    exec(writer_, "BEGIN");
    // One 'input' source per (sessionName, statementIndex), in first-seen order so
    // sourceIds (the sel keys) match the CSV/statements order the tree displays.
    exec(writer_,
         "INSERT INTO sources(kind, sessionName, sessionId) "
         "SELECT 'input', sessionName, statementIndex FROM input.accesses "
         "GROUP BY sessionName, statementIndex ORDER BY MIN(rowid)");
    // Aggregate the raw accesses into per-(source,page) read/write counts.
    exec(writer_,
         "INSERT INTO page_access(sourceId, pageNumber, reads, writes) "
         "SELECT s.sourceId, a.pageNumber, "
         "  SUM(a.access='Read'), SUM(a.access='Write') "
         "FROM input.accesses a "
         "JOIN sources s ON s.kind='input' AND s.sessionName=a.sessionName "
         "  AND s.sessionId=a.statementIndex "
         "GROUP BY s.sourceId, a.pageNumber");
    exec(writer_, "COMMIT");
    exec(writer_, "DETACH DATABASE input");
}

std::int64_t ProfileDb::addQuerySource(const std::string& sql, int queryId,
                                       const std::vector<PageCount>& pages) {
    exec(writer_, "BEGIN");
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(writer_,
                       "INSERT INTO sources(kind, sessionName, sessionId) VALUES ('query',?,?)",
                       -1, &s, nullptr);
    sqlite3_bind_text(s, 1, sql.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 2, queryId);
    if (sqlite3_step(s) != SQLITE_DONE) { sqlite3_finalize(s); fail(writer_, "insert query source"); }
    sqlite3_finalize(s);
    const std::int64_t sourceId = sqlite3_last_insert_rowid(writer_);

    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(writer_,
                       "INSERT INTO page_access VALUES (?,?,?,?)", -1, &ins, nullptr);
    for (const PageCount& p : pages) {
        sqlite3_bind_int64(ins, 1, sourceId);
        sqlite3_bind_int64(ins, 2, p.pageNumber);
        sqlite3_bind_int64(ins, 3, p.reads);
        sqlite3_bind_int64(ins, 4, p.writes);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    exec(writer_, "COMMIT");
    return sourceId;
}
