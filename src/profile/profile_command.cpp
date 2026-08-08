#include "profile/profile_command.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

#include <sqlite3.h>

#include "profile/sqlite_writer.hpp"
#include "profile/profiling_context.hpp"
#include "profile/statements_file.hpp"
#include "profile/vfs_shim.hpp"

namespace {

int queryPageSize(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA page_size;", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return 0;
    }
    int pageSize = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        pageSize = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return pageSize;
}

// Prepares, steps to completion, and finalizes a single statement.
// Returns true on success; reports SQLite errors to stderr.
bool executeStatement(sqlite3* db, const std::string& sql,
                      const std::string& sessionName, int index) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "prepare failed [" << sessionName << ":" << index
                  << "]: " << sqlite3_errmsg(db) << '\n';
        return false;
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // Stepping touches the pages we want to profile; rows are discarded.
    }
    const bool ok = (rc == SQLITE_DONE);
    if (!ok) {
        std::cerr << "step failed [" << sessionName << ":" << index
                  << "]: " << sqlite3_errmsg(db) << '\n';
    }
    sqlite3_finalize(stmt);
    return ok;
}

// The test file must already be a database; the profiler never creates one.
bool testFileIsUsable(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        std::cerr << "test file does not exist: " << path << '\n';
        return false;
    }
    if (std::filesystem::file_size(path, ec) == 0 || ec) {
        std::cerr << "test file is empty: " << path << '\n';
        return false;
    }
    return true;
}

struct SessionStat {
    std::string name;
    std::int64_t reads;
    std::int64_t writes;
};

void printSummary(const std::vector<SessionStat>& stats, const SqliteWriter& writer) {
    std::cerr << "SQLinsite summary:\n";
    for (const SessionStat& s : stats) {
        std::cerr << "  session \"" << s.name << "\": " << (s.reads + s.writes)
                  << " rows (" << s.reads << " read, " << s.writes << " write)\n";
    }
    std::cerr << "  total: " << writer.rowCount() << " rows ("
              << writer.readCount() << " read, " << writer.writeCount()
              << " write) across " << stats.size() << " session(s)\n";
}

bool runSession(const Session& session, const std::string& testFile,
                SqliteWriter& writer) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(testFile.c_str(), &db, SQLITE_OPEN_READWRITE,
                             kSQLINSITEVfsName);
    if (rc != SQLITE_OK) {
        std::cerr << "cannot open " << testFile << ": " << sqlite3_errmsg(db)
                  << '\n';
        sqlite3_close(db);
        return false;
    }

    ProfilingContext& ctx = profilingContext();
    ctx.sessionName = session.name;
    ctx.pageSize = queryPageSize(db);

    bool allOk = true;
    ctx.out = &writer;  // begin measuring
    for (std::size_t i = 0; i < session.statements.size(); ++i) {
        ctx.statementIndex = static_cast<int>(i);
        if (!executeStatement(db, session.statements[i], session.name,
                              static_cast<int>(i))) {
            allOk = false;
        }
    }
    ctx.out = nullptr;  // stop measuring

    sqlite3_close(db);
    return allOk;
}

}  // namespace

int runProfile(const ProfileOptions& options) {
    TestRun testRun;
    try {
        testRun = parseStatementsFile(options.statementsFile);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    if (!testFileIsUsable(options.testFile)) {
        return 1;
    }

    if (registerSQLINSITEVfs() != SQLITE_OK) {
        std::cerr << "failed to register sqlinsite VFS\n";
        return 1;
    }

    SqliteWriter writer(options.outFile);

    ProfilingContext& ctx = profilingContext();
    ctx.relativeTiming = options.relativeTiming;
    ctx.timeBaseline =
        options.relativeTiming
            ? std::chrono::steady_clock::now().time_since_epoch().count()
            : 0;

    bool allOk = true;
    std::vector<SessionStat> stats;
    for (const Session& session : testRun.sessions) {
        const std::int64_t reads0 = writer.readCount();
        const std::int64_t writes0 = writer.writeCount();
        if (!runSession(session, options.testFile, writer)) {
            allOk = false;
        }
        stats.push_back({session.name, writer.readCount() - reads0,
                         writer.writeCount() - writes0});
    }

    // `ctx.pageSize` was set to the test db's page size while profiling; it's the
    // same file every session, so the last value is authoritative.
    writer.finish({options.testFile, ctx.pageSize,
                   options.relativeTiming ? "relative" : "raw", testRun.name});

    if (!options.quiet) {
        printSummary(stats, writer);
    }

    return allOk ? 0 : 1;
}
