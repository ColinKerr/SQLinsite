// Quantifies the per-page overhead of the sqlinsite wrapping VFS by running an
// identical read workload through the default VFS and through the shim, then
// dividing the wall-time difference by the number of logged page accesses.
//
// Usage: sqlinsite_bench [rows] [iterations]

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include <sqlite3.h>

#include "profile/csv_writer.hpp"
#include "profile/profiling_context.hpp"
#include "profile/vfs_shim.hpp"

namespace {

using Clock = std::chrono::steady_clock;

[[noreturn]] void die(const std::string& msg) {
    std::cerr << msg << '\n';
    std::exit(1);
}

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        die(std::string("exec failed: ") + (err ? err : "?"));
    }
}

void buildDb(const std::string& path, int rows) {
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) die("open failed");
    exec(db, "PRAGMA journal_mode=DELETE;");
    exec(db, "CREATE TABLE T(id INTEGER PRIMARY KEY, a TEXT, b TEXT);");
    exec(db, "BEGIN;");
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO T(a,b) VALUES(?,?)", -1, &st, nullptr);
    for (int i = 0; i < rows; ++i) {
        sqlite3_bind_text(st, 1, "some representative text value", -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, "another representative value", -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    exec(db, "COMMIT;");
    sqlite3_close(db);
}

// Opens `iters` fresh connections (cold cache each), full-scans the table, and
// returns elapsed nanoseconds. `vfs` is nullptr for the default VFS.
std::int64_t runWorkload(const std::string& path, const char* vfs, int iters) {
    const auto t0 = Clock::now();
    for (int i = 0; i < iters; ++i) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, vfs) !=
            SQLITE_OK) {
            die("open_v2 failed");
        }
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db, "SELECT count(a || b) FROM T", -1, &st, nullptr);
        while (sqlite3_step(st) == SQLITE_ROW) {
        }
        sqlite3_finalize(st);
        sqlite3_close(db);
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0)
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    const int rows = argc > 1 ? std::atoi(argv[1]) : 2000000;
    const int iters = argc > 2 ? std::atoi(argv[2]) : 50;

    const std::string dbPath = "/tmp/sqlinsite_bench.db";
    const std::string csvPath = "/tmp/sqlinsite_bench.csv";
    buildDb(dbPath, rows);

    if (registerSQLINSITEVfs() != SQLITE_OK) die("vfs register failed");

    // Warm the OS page cache so we compare CPU overhead, not disk variance.
    runWorkload(dbPath, nullptr, 2);

    const std::int64_t base = runWorkload(dbPath, nullptr, iters);

    CsvWriter writer(csvPath);
    writer.writeHeader();
    ProfilingContext& ctx = profilingContext();
    ctx.sessionName = "bench";
    ctx.statementIndex = 0;
    ctx.pageSize = 4096;
    ctx.out = &writer;
    const std::int64_t shim = runWorkload(dbPath, kSQLINSITEVfsName, iters);
    ctx.out = nullptr;

    const std::int64_t pages = writer.rowCount();
    const double perPageNs =
        pages > 0 ? static_cast<double>(shim - base) / static_cast<double>(pages)
                  : 0.0;

    std::cout << "rows=" << rows << " iterations=" << iters << '\n'
              << "logged page accesses: " << pages << '\n'
              << "default VFS:   " << base / 1e6 << " ms\n"
              << "sqlinsite VFS:" << shim / 1e6 << " ms\n"
              << "overhead:      " << (shim - base) / 1e6 << " ms ("
              << (base > 0 ? 100.0 * (shim - base) / base : 0.0) << "%)\n"
              << "per page:      " << perPageNs << " ns\n";
    return 0;
}
