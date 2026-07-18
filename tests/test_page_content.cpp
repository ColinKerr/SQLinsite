#include <doctest/doctest.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "map/map_command.hpp"
#include "test_util.hpp"
#include "visualize/map_db.hpp"
#include "visualize/page_content.hpp"

using nlohmann::json;

namespace {
struct Fixture { std::string dbPath, mapPath; };

Fixture buildFixture() {
    Fixture fx;
    fx.dbPath = tmpPath("pc_src.db");
    fx.mapPath = tmpPath("pc_src.sqlite");
    std::remove(fx.dbPath.c_str());
    std::remove(fx.mapPath.c_str());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(fx.dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
                         "CREATE TABLE T(id INTEGER PRIMARY KEY, n INTEGER, s TEXT);"
                         "CREATE INDEX T_n ON T(n);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO T(n,s) VALUES (?,?)", -1, &st, nullptr) == SQLITE_OK);
    for (int i = 0; i < 200; ++i) {
        sqlite3_bind_int(st, 1, i);
        const std::string s = (i == 0) ? std::string(6000, 'x') : "row" + std::to_string(i);
        sqlite3_bind_text(st, 2, s.c_str(), -1, SQLITE_TRANSIENT);
        REQUIRE(sqlite3_step(st) == SQLITE_DONE);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    REQUIRE(runMap(MapOptions{fx.dbPath, fx.mapPath}) == 0);
    return fx;
}

// A table big enough to force a multi-level b-tree (an interior root page).
Fixture buildInteriorFixture() {
    Fixture fx;
    fx.dbPath = tmpPath("pc_int.db");
    fx.mapPath = tmpPath("pc_int.sqlite");
    std::remove(fx.dbPath.c_str());
    std::remove(fx.mapPath.c_str());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(fx.dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "CREATE TABLE T(id INTEGER PRIMARY KEY, s TEXT)",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO T(id,s) VALUES (?,?)", -1, &st, nullptr) == SQLITE_OK);
    const std::string pad(60, 'x');
    for (int i = 1; i <= 5000; ++i) {
        sqlite3_bind_int(st, 1, i);
        sqlite3_bind_text(st, 2, pad.c_str(), -1, SQLITE_STATIC);
        REQUIRE(sqlite3_step(st) == SQLITE_DONE);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    REQUIRE(runMap(MapOptions{fx.dbPath, fx.mapPath}) == 0);
    return fx;
}

// Like buildInteriorFixture but with scattered deletions so rowids are NOT
// contiguous (every 7th id, plus the block 100..139 removed).
Fixture buildGappyInteriorFixture() {
    Fixture fx;
    fx.dbPath = tmpPath("pc_gap.db");
    fx.mapPath = tmpPath("pc_gap.sqlite");
    std::remove(fx.dbPath.c_str());
    std::remove(fx.mapPath.c_str());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(fx.dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "CREATE TABLE T(id INTEGER PRIMARY KEY, s TEXT)",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO T(id,s) VALUES (?,?)", -1, &st, nullptr) == SQLITE_OK);
    const std::string pad(60, 'x');
    for (int i = 1; i <= 5000; ++i) {
        sqlite3_bind_int(st, 1, i);
        sqlite3_bind_text(st, 2, pad.c_str(), -1, SQLITE_STATIC);
        REQUIRE(sqlite3_step(st) == SQLITE_DONE);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    REQUIRE(sqlite3_exec(db,
                         "DELETE FROM T WHERE id % 7 = 1 OR id BETWEEN 100 AND 139",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    REQUIRE(runMap(MapOptions{fx.dbPath, fx.mapPath}) == 0);
    return fx;
}

// Wide rows force few rows per leaf → a THREE-level table b-tree, so interior
// pages that parent other interior pages exist. Scattered deletions add gaps.
Fixture buildThreeLevelFixture() {
    Fixture fx;
    fx.dbPath = tmpPath("pc_deep.db");
    fx.mapPath = tmpPath("pc_deep.sqlite");
    std::remove(fx.dbPath.c_str());
    std::remove(fx.mapPath.c_str());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(fx.dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "CREATE TABLE T(id INTEGER PRIMARY KEY, s TEXT)",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO T(id,s) VALUES (?,?)", -1, &st, nullptr) == SQLITE_OK);
    const std::string pad(400, 'x');
    for (int i = 1; i <= 12000; ++i) {
        sqlite3_bind_int(st, 1, i);
        sqlite3_bind_text(st, 2, pad.c_str(), -1, SQLITE_STATIC);
        REQUIRE(sqlite3_step(st) == SQLITE_DONE);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    REQUIRE(sqlite3_exec(db, "DELETE FROM T WHERE id % 500 = 0", nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    REQUIRE(runMap(MapOptions{fx.dbPath, fx.mapPath}) == 0);
    return fx;
}

// Opens a map file read-only for raw SQL assertions.
struct MapQuery {
    sqlite3* db = nullptr;
    explicit MapQuery(const std::string& path) {
        REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    }
    ~MapQuery() { sqlite3_close(db); }
    std::int64_t scalar(const std::string& sql) {
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
        std::int64_t v = -1;
        if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
        return v;
    }
};
}  // namespace

TEST_CASE("page content decodes every page type without throwing") {
    const Fixture fx = buildFixture();
    MapDb map(fx.mapPath);
    PageContent pc = PageContent::open(fx.dbPath);

    const auto meta = json::parse(map.metaJson());
    const std::int64_t pageCount = meta["meta"].value("pageCount", 0);
    REQUIRE(pageCount > 3);

    bool sawTableLeaf = false, sawTypeName = false, sawSerialName = false, sawOverflowValue = false;
    for (std::int64_t n = 1; n <= pageCount; ++n) {
        const std::string type = map.pageType(n);
        if (type.empty()) continue;
        CAPTURE(n);
        CAPTURE(type);
        const std::string body = pc.pageJson(n, type, [&](std::int64_t p) { return map.pageType(p); });
        REQUIRE_FALSE(body.empty());
        const auto j = json::parse(body);
        CHECK(j["pageNumber"] == n);
        CHECK(j["regions"].is_array());
        CHECK(j["regions"].size() >= 1);
        if (type == "table-leaf" || type == "table-interior")
            if (j["header"].value("typeName", "") != "") sawTypeName = true;
        if (type == "table-leaf") {
            sawTableLeaf = true;
            for (const auto& c : j["cells"]) {
                for (const auto& col : c.value("columns", json::array())) {
                    if (col.contains("serialName")) sawSerialName = true;
                    // The 6000-char row overflows: its text column is assembled in
                    // full from the overflow pages and flagged fromOverflow.
                    if (col.value("type", "") == "text" && col.value("fromOverflow", false)) {
                        sawOverflowValue = true;
                        // Per-page byte/text segments; at least one is an overflow page.
                        REQUIRE(col["segments"].is_array());
                        CHECK(col["segments"].size() >= 1);
                        bool sawOvSeg = false;
                        std::size_t segBytes = 0;
                        for (const auto& seg : col["segments"]) {
                            segBytes += seg["bytes"].get<std::size_t>();
                            if (seg["page"].get<std::int64_t>() != n) sawOvSeg = true;
                        }
                        CHECK(sawOvSeg);
                        CHECK(segBytes == col["bytes"].get<std::size_t>()); // segments cover the value
                        // "in full": the assembled value length matches its byte size.
                        CHECK(col["value"].get<std::string>().size() == col["bytes"].get<std::size_t>());
                    }
                }
            }
        }
        // Pointers carry their target's page type for symbology.
        for (const auto& p : j["pointers"]) CHECK(p.contains("pageType"));
    }
    CHECK(sawTableLeaf);
    CHECK(sawTypeName);
    CHECK(sawSerialName);
    CHECK(sawOverflowValue);
}

TEST_CASE("overflow page is owned by the leaf whose value chains through it") {
    const Fixture fx = buildFixture();
    MapDb map(fx.mapPath);
    PageContent pc = PageContent::open(fx.dbPath);
    auto typeOf = [&](std::int64_t p) { return map.pageType(p); };

    const auto meta = json::parse(map.metaJson());
    const std::int64_t pageCount = meta["meta"].value("pageCount", 0);

    std::int64_t overflowPage = 0;
    for (std::int64_t n = 1; n <= pageCount; ++n) {
        if (map.pageType(n) == "overflow") { overflowPage = n; break; }
    }
    REQUIRE(overflowPage > 0);

    // The owner is a non-overflow page (the table leaf holding the 6000-char row).
    const std::int64_t owner = map.overflowOwner(overflowPage);
    REQUIRE(owner > 0);
    CHECK(map.pageType(owner) != "overflow");

    // The owner has a cell whose value has a segment on this overflow page.
    const auto own = json::parse(pc.pageJson(owner, map.pageType(owner), typeOf));
    bool owningCellReaches = false;
    for (const auto& cell : own.value("cells", json::array()))
        for (const auto& col : cell.value("columns", json::array()))
            for (const auto& seg : col.value("segments", json::array()))
                if (seg.value("page", std::int64_t{0}) == overflowPage) owningCellReaches = true;
    CHECK(owningCellReaches);
}

namespace {
std::int64_t firstInteriorPage(const MapDb& map) {
    const auto meta = json::parse(map.metaJson());
    const std::int64_t pageCount = meta["meta"].value("pageCount", 0);
    for (std::int64_t n = 1; n <= pageCount; ++n)
        if (map.pageType(n) == "table-interior") return n;
    return 0;
}
}  // namespace

TEST_CASE("page b-tree info reports the owning object and table row count") {
    // A multi-level table b-tree: interior + leaf pages both belong to table T and
    // carry a row count (subtree total for the interior root, own rows for a leaf).
    {
        const Fixture fx = buildInteriorFixture();
        MapDb map(fx.mapPath);
        const std::int64_t pageCount = json::parse(map.metaJson())["meta"].value("pageCount", 0);
        const std::int64_t interior = firstInteriorPage(map);  // the table's root
        REQUIRE(interior > 0);
        std::int64_t leaf = 0;
        for (std::int64_t n = 2; n <= pageCount && leaf == 0; ++n)  // skip sqlite_schema (page 1)
            if (map.pageType(n) == "table-leaf") leaf = n;
        REQUIRE(leaf > 0);

        const auto in = json::parse(map.pageBtreeInfoJson(interior));
        CHECK(in["object"]["name"] == "T");
        CHECK(in["object"]["type"] == "table");
        REQUIRE(in.contains("rowCount"));
        CHECK(in["rowCount"].get<std::int64_t>() == 5000);  // whole table

        const auto lf = json::parse(map.pageBtreeInfoJson(leaf));
        CHECK(lf["object"]["name"] == "T");
        REQUIRE(lf.contains("rowCount"));
        CHECK(lf["rowCount"].get<std::int64_t>() > 0);
        CHECK(lf["rowCount"].get<std::int64_t>() < 5000);   // one leaf < whole table
    }
    // An index page belongs to its index object but has no row count.
    {
        const Fixture fx = buildFixture();  // table T + index T_n
        MapDb map(fx.mapPath);
        const std::int64_t pageCount = json::parse(map.metaJson())["meta"].value("pageCount", 0);
        std::int64_t idx = 0;
        for (std::int64_t n = 1; n <= pageCount && idx == 0; ++n) {
            const std::string t = map.pageType(n);
            if (t == "index-leaf" || t == "index-interior") idx = n;
        }
        REQUIRE(idx > 0);
        const auto j = json::parse(map.pageBtreeInfoJson(idx));
        CHECK(j["object"]["type"] == "index");
        CHECK_FALSE(j.contains("rowCount"));  // index pages carry no row count
    }
}

TEST_CASE("table-interior rowid runs/counts cover a contiguous table end to end") {
    const Fixture fx = buildInteriorFixture();
    MapDb map(fx.mapPath);
    const std::int64_t interior = firstInteriorPage(map);
    REQUIRE(interior > 0);

    const auto r_string = map.tableInteriorRowRunsJson(interior);
    const auto r = json::parse(r_string);
    const auto& cells = r["cells"];
    REQUIRE(cells.size() >= 1);

    // No deletions: each child is one contiguous run, runs are adjacent & ascending.
    std::int64_t sum = 0, prevHigh = 0;
    const std::int64_t firstLow = cells[0]["rowRuns"][0]["startRowId"].get<std::int64_t>();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        REQUIRE(cells[i]["rowRuns"].size() == 1);
        const std::int64_t lo = cells[i]["rowRuns"][0]["startRowId"].get<std::int64_t>();
        const std::int64_t hi = cells[i]["rowRuns"][0]["endRowId"].get<std::int64_t>();
        CHECK(cells[i]["rowRuns"][0]["rowCount"].get<std::int64_t>() == hi - lo + 1);
        CHECK(cells[i]["rowCount"].get<std::int64_t>() == hi - lo + 1);
        if (i > 0) CHECK(lo == prevHigh + 1);
        prevHigh = hi;
        sum += cells[i]["rowCount"].get<std::int64_t>();
    }
    const auto& rm = r["rightmost"];
    REQUIRE(rm["rowRuns"].size() == 1);
    CHECK(rm["rowRuns"][0]["startRowId"].get<std::int64_t>() == prevHigh + 1);
    sum += rm["rowCount"].get<std::int64_t>();

    CHECK(firstLow == 1);
    CHECK(rm["rowRuns"][0]["endRowId"].get<std::int64_t>() == 5000);
    CHECK(sum == 5000);  // every rowid is accounted for exactly once
}

TEST_CASE("table-interior counts individual rowids and splits runs at gaps") {
    const Fixture fx = buildGappyInteriorFixture();  // deletes id%7==1 and 100..139
    MapDb map(fx.mapPath);
    const std::int64_t interior = firstInteriorPage(map);
    REQUIRE(interior > 0);

    const auto r = json::parse(map.tableInteriorRowRunsJson(interior));
    const auto& cells = r["cells"];

    // Collect every run across cells + rightmost.
    std::vector<std::pair<std::int64_t, std::int64_t>> runs;
    std::int64_t sum = 0;
    bool anyMultiRun = false;
    for (const auto& c : cells) {
        sum += c["rowCount"].get<std::int64_t>();
        if (c["rowRuns"].size() > 1) anyMultiRun = true;
        for (const auto& run : c["rowRuns"])
            runs.emplace_back(run["startRowId"].get<std::int64_t>(), run["endRowId"].get<std::int64_t>());
    }
    sum += r["rightmost"]["rowCount"].get<std::int64_t>();
    for (const auto& run : r["rightmost"]["rowRuns"])
        runs.emplace_back(run["startRowId"].get<std::int64_t>(), run["endRowId"].get<std::int64_t>());

    auto covered = [&](std::int64_t id) {
        for (const auto& [lo, hi] : runs) if (id >= lo && id <= hi) return true;
        return false;
    };

    // Count excludes the 750 deleted rows; gaps split runs, so some cell has >1 run.
    CHECK(sum == 4250);
    CHECK(anyMultiRun);
    // Deleted ids are absent; surviving neighbours are present.
    CHECK_FALSE(covered(1));    // id%7==1
    CHECK_FALSE(covered(8));
    CHECK_FALSE(covered(120));  // inside the deleted 100..139 block
    CHECK(covered(2));
    CHECK(covered(98));   // survives (98 % 7 == 0), just below the deleted block
    CHECK(covered(140));  // survives, just above the deleted block
}

TEST_CASE("pageRowidRunsJson serves a page's exact rows, keyset-paginated") {
    const Fixture fx = buildGappyInteriorFixture();  // deletes id%7==1 and 100..139
    MapDb map(fx.mapPath);
    const std::int64_t pageCount = json::parse(map.metaJson())["meta"].value("pageCount", 0);
    const std::int64_t interior = firstInteriorPage(map);
    REQUIRE(interior > 0);
    std::int64_t leaf = 0, overflow = 0;
    for (std::int64_t n = 2; n <= pageCount; ++n) {
        const std::string t = map.pageType(n);
        if (t == "table-leaf" && leaf == 0) leaf = n;
        if (t == "overflow" && overflow == 0) overflow = n;
    }
    REQUIRE(leaf > 0);

    // Gaps are excluded: no deleted rowid appears in any returned run, and runs are
    // ascending & maximal (the fixture deletes id%7==1, so runs split at those gaps).
    auto deleted = [](std::int64_t id) { return id % 7 == 1 || (id >= 100 && id <= 139); };
    auto checkRuns = [&](std::int64_t page) {
        const auto j = json::parse(map.pageRowidRunsJson(page, 0, 5000));  // all runs, one batch
        CHECK(j["table"] == "T");
        CHECK(j["nextAfter"].is_null());
        std::int64_t sum = 0, prevHi = 0;
        for (const auto& run : j["runs"]) {
            const std::int64_t lo = run[0].get<std::int64_t>(), hi = run[1].get<std::int64_t>();
            CHECK(lo <= hi);
            if (prevHi) CHECK(lo > prevHi + 1);   // a real gap between maximal runs
            for (std::int64_t id = lo; id <= hi; ++id) CHECK_FALSE(deleted(id));
            prevHi = hi;
            sum += hi - lo + 1;
        }
        CHECK(sum == j["totalRowCount"].get<std::int64_t>());
        CHECK(sum > 0);
    };
    checkRuns(leaf);       // a leaf page → its own rows
    checkRuns(interior);   // an interior page → its whole subtree's rows

    // An overflow page resolves to its owning leaf's rows (same table).
    if (overflow > 0) {
        const auto ov = json::parse(map.pageRowidRunsJson(overflow, 0, 5000));
        CHECK(ov["table"] == "T");
        CHECK(ov["totalRowCount"].get<std::int64_t>() > 0);
    }

    // Keyset pagination: a small limit truncates and hands back a cursor; paging by
    // `nextAfter` yields the rest with no gaps or repeats vs. the one-shot batch.
    const auto full = json::parse(map.pageRowidRunsJson(interior, 0, 5000));
    json paged = json::array();
    std::int64_t after = 0;
    for (int guard = 0; guard < 1000; ++guard) {
        const auto b = json::parse(map.pageRowidRunsJson(interior, after, 3));
        CHECK(b["runs"].size() <= 3);
        for (const auto& run : b["runs"]) paged.push_back(run);
        if (b["nextAfter"].is_null()) break;
        after = b["nextAfter"].get<std::int64_t>();
    }
    CHECK(paged == full["runs"]);

    // An index page (no rowid runs) yields the empty shape.
    std::int64_t idx = 0;
    for (std::int64_t n = 1; n <= pageCount && idx == 0; ++n) {
        const std::string t = map.pageType(n);
        if (t == "index-leaf" || t == "index-interior") idx = n;
    }
    if (idx > 0) {
        const auto j = json::parse(map.pageRowidRunsJson(idx, 0, 5000));
        CHECK(j["table"].is_null());
        CHECK(j["runs"].empty());
    }
}

TEST_CASE("page_row_runs precomputes contiguous rowid runs for every interior page") {
    const Fixture fx = buildThreeLevelFixture();
    MapQuery m(fx.mapPath);

    // The tree is ≥3 levels: some interior page's child is also table-interior.
    CHECK(m.scalar(
        "SELECT COUNT(*) FROM pointers ptr "
        "JOIN pages pp ON pp.pageNumber=ptr.fromPage AND pp.pageType='table-interior' "
        "JOIN pages cp ON cp.pageNumber=ptr.toPage AND cp.pageType='table-interior' "
        "WHERE ptr.kind='child'") >= 1);

    // Every table-interior page has at least one run.
    CHECK(m.scalar(
        "SELECT COUNT(*) FROM pages p WHERE p.pageType='table-interior' "
        "AND NOT EXISTS(SELECT 1 FROM page_row_runs WHERE parentPageNumber=p.pageNumber)") == 0);

    // Runs within a page are ascending and MAXIMAL — a real gap between them
    // (endRowId+1 < next startRowId), never overlapping or adjacent.
    CHECK(m.scalar(
        "WITH ord AS (SELECT parentPageNumber, startRowId, endRowId, "
        " LAG(endRowId) OVER (PARTITION BY parentPageNumber ORDER BY startRowId) AS prevEnd "
        " FROM page_row_runs) "
        "SELECT COUNT(*) FROM ord "
        "WHERE startRowId>endRowId OR (prevEnd IS NOT NULL AND startRowId<=prevEnd+1)") == 0);

    // rowCount is exactly the run's span everywhere.
    CHECK(m.scalar("SELECT COUNT(*) FROM page_row_runs WHERE rowCount <> endRowId-startRowId+1") == 0);

    // The root interior page covers exactly the surviving rows (12000 − 24 that
    // are multiples of 500), from rowid 1 to 11999.
    const std::string root = std::to_string(m.scalar(
        "SELECT pageNumber FROM pages p WHERE p.pageType='table-interior' "
        "AND NOT EXISTS(SELECT 1 FROM pointers WHERE toPage=p.pageNumber AND kind='child')"));
    CHECK(m.scalar("SELECT SUM(rowCount) FROM page_row_runs WHERE parentPageNumber=" + root) == 11976);
    CHECK(m.scalar("SELECT MIN(startRowId) FROM page_row_runs WHERE parentPageNumber=" + root) == 1);
    CHECK(m.scalar("SELECT MAX(endRowId) FROM page_row_runs WHERE parentPageNumber=" + root) == 11999);

    // No deleted rowid (a multiple of 500) falls inside any root run.
    CHECK(m.scalar(
        "WITH RECURSIVE del(v) AS (SELECT 500 UNION ALL SELECT v+500 FROM del WHERE v<12000) "
        "SELECT COUNT(*) FROM page_row_runs prr JOIN del ON del.v BETWEEN prr.startRowId AND prr.endRowId "
        "WHERE prr.parentPageNumber=" + root) == 0);
}
