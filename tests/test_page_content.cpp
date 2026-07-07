#include <doctest/doctest.h>

#include <cstdio>
#include <string>

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
                        CHECK(col["overflowPages"].is_array());
                        CHECK(col["overflowPages"].size() >= 1);
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
