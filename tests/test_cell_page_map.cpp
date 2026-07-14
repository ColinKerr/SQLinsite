#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <unordered_map>

#include <sqlite3.h>

#include "map/map_command.hpp"
#include "test_util.hpp"
#include "visualize/cell_page_map.hpp"
#include "visualize/map_db.hpp"

namespace {

// Builds a DB whose last column holds a value large enough to overflow onto
// overflow pages, plus a map alongside it. Returns db path, map path.
struct Fixture {
    std::string dbPath;
    std::string mapPath;
};

Fixture buildFixture() {
    Fixture fx;
    fx.dbPath = tmpPath("cpm_src.db");
    fx.mapPath = tmpPath("cpm_src.sqlite");
    std::remove(fx.dbPath.c_str());
    std::remove(fx.mapPath.c_str());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(fx.dbPath.c_str(), &db) == SQLITE_OK);
    // id (INTEGER PRIMARY KEY), small INTEGER, big TEXT.
    REQUIRE(sqlite3_exec(db, "CREATE TABLE T(id INTEGER PRIMARY KEY, small INTEGER, big TEXT);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    // A ~6000-byte value overflows the local payload on any usual page size.
    const std::string big(6000, 'x');
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO T(small, big) VALUES (42, ?)", -1, &st,
                               nullptr) == SQLITE_OK);
    sqlite3_bind_text(st, 1, big.c_str(), static_cast<int>(big.size()), SQLITE_TRANSIENT);
    REQUIRE(sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    sqlite3_close(db);

    REQUIRE(runMap(MapOptions{fx.dbPath, fx.mapPath}) == 0);
    return fx;
}

}  // namespace

TEST_CASE("cell→page mapping is per column, spanning overflow pages") {
    const Fixture fx = buildFixture();
    MapDb map(fx.mapPath);
    CellPageMap cpm = CellPageMap::open(fx.dbPath);

    const std::int64_t leaf = map.leafPageForRowid("T", 1);
    REQUIRE(leaf > 0);

    // Small columns live entirely within the leaf's local payload.
    const auto pk = cpm.pagesForCell(leaf, /*rowid=*/1, /*cid=*/0);      // id
    const auto small = cpm.pagesForCell(leaf, 1, /*cid=*/1);            // small
    CHECK(pk == std::vector<std::int64_t>{leaf});
    CHECK(small == std::vector<std::int64_t>{leaf});

    // The large TEXT column spills onto overflow pages: it touches the leaf plus
    // at least one distinct overflow page.
    const auto big = cpm.pagesForCell(leaf, 1, /*cid=*/2);
    REQUIRE(big.size() >= 2);
    CHECK(big.front() == leaf);
    for (std::size_t i = 1; i < big.size(); ++i) {
        CHECK(big[i] != leaf);
        CHECK(big[i] > 0);
    }

    // Out-of-range / missing rows resolve to nothing.
    CHECK(cpm.pagesForCell(leaf, 1, /*cid=*/99).empty());
    CHECK(cpm.pagesForCell(leaf, /*rowid=*/999, /*cid=*/0).empty());
}

TEST_CASE("rowid→leaf mapping never returns table-interior pages") {
    // A table large enough to have a multi-level b-tree. The rowid→leaf mapping
    // must resolve only to the table-leaf pages where rows actually live.
    const std::string dbPath = tmpPath("cpm_big.db");
    const std::string mapPath = tmpPath("cpm_big.sqlite");
    std::remove(dbPath.c_str());
    std::remove(mapPath.c_str());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "CREATE TABLE Big(id INTEGER PRIMARY KEY, v TEXT);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_stmt* ins = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO Big(v) VALUES (?)", -1, &ins, nullptr) == SQLITE_OK);
    for (int i = 0; i < 4000; ++i) {  // enough rows to force ≥1 interior page
        sqlite3_bind_text(ins, 1, "row-value-padding", -1, SQLITE_TRANSIENT);
        REQUIRE(sqlite3_step(ins) == SQLITE_DONE);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    REQUIRE(sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    REQUIRE(runMap(MapOptions{dbPath, mapPath}) == 0);
    MapDb map(mapPath);

    // Every rowid resolves to a table-leaf page (never an interior page), and a
    // missing rowid resolves to 0.
    sqlite3* mdb = nullptr;
    REQUIRE(sqlite3_open_v2(mapPath.c_str(), &mdb, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* q = nullptr;
    REQUIRE(sqlite3_prepare_v2(mdb, "SELECT pageType FROM pages WHERE pageNumber=?", -1,
                               &q, nullptr) == SQLITE_OK);
    bool sawInterior = false;
    for (int rowid = 1; rowid <= 4000; ++rowid) {
        const std::int64_t page = map.leafPageForRowid("Big", rowid);
        REQUIRE(page > 0);
        sqlite3_reset(q);
        sqlite3_bind_int64(q, 1, page);
        REQUIRE(sqlite3_step(q) == SQLITE_ROW);
        const std::string type = reinterpret_cast<const char*>(sqlite3_column_text(q, 0));
        if (type != "table-leaf") sawInterior = true;
    }
    sqlite3_finalize(q);
    sqlite3_close(mdb);

    CHECK_FALSE(sawInterior);
    CHECK(map.leafPageForRowid("Big", 999999) == 0);  // missing rowid → 0

    // rowid is a table-leaf-only concept: this multi-level b-tree HAS interior
    // cells, but none of them carries a rowid in the map (their divider keys are
    // boundaries, not real rowids, so they are left NULL).
    sqlite3* mdb2 = nullptr;
    REQUIRE(sqlite3_open_v2(mapPath.c_str(), &mdb2, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    auto countCells = [&](const char* where) {
        sqlite3_stmt* s = nullptr;
        const std::string sql =
            std::string("SELECT count(*) FROM cells c JOIN pages p ON p.pageNumber=c.pageNumber WHERE ") + where;
        REQUIRE(sqlite3_prepare_v2(mdb2, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(s) == SQLITE_ROW);
        const int n = sqlite3_column_int(s, 0);
        sqlite3_finalize(s);
        return n;
    };
    CHECK(countCells("p.pageType='table-interior'") >= 1);                        // interior cells exist
    CHECK(countCells("c.rowid IS NOT NULL AND p.pageType='table-interior'") == 0); // but none has a rowid
    sqlite3_close(mdb2);
}
