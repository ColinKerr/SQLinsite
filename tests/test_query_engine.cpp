#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "map/map_command.hpp"
#include "test_util.hpp"
#include "visualize/map_db.hpp"
#include "visualize/query_engine.hpp"
#include "visualize/visualize_command.hpp"

using nlohmann::json;

namespace {

struct Fixture {
    std::string dbPath;
    std::string mapPath;
    int pageSize = 0;
};

Fixture buildFixture() {
    Fixture fx;
    fx.dbPath = tmpPath("qe_src.db");
    fx.mapPath = tmpPath("qe_src.sqlite");
    std::remove(fx.dbPath.c_str());
    std::remove(fx.mapPath.c_str());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(fx.dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
                         "CREATE TABLE T(id INTEGER PRIMARY KEY, v TEXT);"
                         "INSERT INTO T(v) VALUES ('a'),('b'),('c');"
                         "CREATE INDEX T_v ON T(v);"
                         "CREATE VIEW VW AS SELECT id, v FROM T;",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "PRAGMA page_size;", -1, &st, nullptr);
    if (sqlite3_step(st) == SQLITE_ROW) fx.pageSize = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(db);

    REQUIRE(runMap(MapOptions{fx.dbPath, fx.mapPath}) == 0);
    return fx;
}

}  // namespace

TEST_CASE("query engine runs, profiles, paginates, explains") {
    const Fixture fx = buildFixture();
    MapDb map(fx.mapPath);
    QueryEngine engine(fx.dbPath, fx.pageSize, map);

    SUBCASE("run captures columns, rows and a cold-cache profile") {
        auto j = json::parse(engine.runJson("SELECT id, v FROM T ORDER BY id"));
        CHECK(j["queryId"] == 1);
        CHECK(j["columns"][0]["name"] == "id");
        CHECK(j["columns"][1]["name"] == "v");
        CHECK(j["rowCount"] == 3);
        CHECK(j["truncated"] == false);
        CHECK(j["pageCount"].get<int>() >= 1);
        CHECK(j["accesses"].get<int>() >= 1);
        CHECK(j["profile"]["pages"].is_array());
        CHECK(j["profile"]["pages"].size() >= 1);

        auto rows = json::parse(engine.rowsJson(1, 0, 2));
        REQUIRE(rows["rows"].size() == 2);
        CHECK(rows["rows"][0][0] == 1);
        CHECK(rows["rows"][0][1] == "a");
        CHECK(rows["rowCount"] == 3);
    }

    SUBCASE("row slice is clamped to the stored rows") {
        engine.runJson("SELECT id FROM T");
        auto rows = json::parse(engine.rowsJson(1, 1, 999));
        CHECK(rows["rows"].size() == 2); // rows 1..2 of 3
    }

    SUBCASE("invalid SQL returns an error, no history entry") {
        auto j = json::parse(engine.runJson("SELECT * FROM nope"));
        CHECK(j.contains("error"));
        auto hist = json::parse(engine.historyJson());
        CHECK(hist["history"].empty());
    }

    SUBCASE("explain returns both plans") {
        auto j = json::parse(engine.explainJson("SELECT * FROM T"));
        CHECK(j["queryPlan"]["columns"].is_array());
        CHECK(j["explain"]["rows"].is_array());
        CHECK(j["explain"]["rows"].size() >= 1);
    }

    SUBCASE("history lists runs newest-first") {
        engine.runJson("SELECT 1");
        engine.runJson("SELECT 2");
        auto hist = json::parse(engine.historyJson());
        REQUIRE(hist["history"].size() == 2);
        CHECK(hist["history"][0]["sql"] == "SELECT 2");
        CHECK(hist["history"][1]["sql"] == "SELECT 1");
    }
}

TEST_CASE("schema tree joins db structure to map page counts") {
    const Fixture fx = buildFixture();
    MapDb map(fx.mapPath);
    QueryEngine engine(fx.dbPath, fx.pageSize, map);

    auto j = json::parse(engine.schemaJson(map.objectStats()));
    REQUIRE(j["tables"].is_array());

    const auto& tables = j["tables"];
    auto tit = std::find_if(tables.begin(), tables.end(),
                            [](const json& t) { return t["name"] == "T"; });
    REQUIRE(tit != tables.end());
    const json& t = *tit;
    CHECK(t["pageCount"].get<int>() >= 1);
    // columns id, v
    CHECK(t["columns"].size() == 2);
    CHECK(t["columns"][0]["name"] == "id");
    // index T_v nested under T
    REQUIRE(t["indexes"].size() >= 1);
    CHECK(t["indexes"][0]["name"] == "T_v");

    REQUIRE(j["views"].size() >= 1);
    CHECK(j["views"][0]["name"] == "VW");
    CHECK(j["views"][0]["columns"].size() == 2);
}

TEST_CASE("row→page mapping resolves single-table cells, leaves others null") {
    const Fixture fx = buildFixture();
    MapDb map(fx.mapPath);
    QueryEngine engine(fx.dbPath, fx.pageSize, map);

    SUBCASE("plain select maps id/v cells to the row's leaf page") {
        engine.runJson("SELECT id, v FROM T ORDER BY id");
        auto rows = json::parse(engine.rowsJson(1, 0, 3));
        REQUIRE(rows["rowPages"].size() == 3);
        // Each cell is an array of the pages holding its bytes. For this tiny
        // table both columns live on the same single (leaf) page.
        const auto page = rows["rowPages"][0][0];
        REQUIRE(page.is_array());
        REQUIRE(page.size() == 1);
        CHECK(page[0].is_number());
        CHECK(rows["rowPages"][0][1] == page);   // both columns from table T
        CHECK(rows["columns"][0]["sourceTable"] == "T");
        CHECK(rows["columns"][0]["sourceColumn"] == "id");
    }

    SUBCASE("expression columns are unresolved (empty)") {
        engine.runJson("SELECT id, id*2 AS doubled FROM T ORDER BY id");
        auto rows = json::parse(engine.rowsJson(1, 0, 1));
        CHECK(rows["rowPages"][0][0].is_array());
        CHECK_FALSE(rows["rowPages"][0][0].empty());  // id → resolved
        CHECK(rows["rowPages"][0][1].is_array());
        CHECK(rows["rowPages"][0][1].empty());        // id*2 → unresolved
        CHECK(rows["columns"][1]["sourceTable"].is_null());
    }

    SUBCASE("aggregates leave all cells unresolved") {
        engine.runJson("SELECT count(*) AS n FROM T");
        auto rows = json::parse(engine.rowsJson(1, 0, 1));
        CHECK(rows["rowPages"][0][0].is_array());
        CHECK(rows["rowPages"][0][0].empty());
    }
}

TEST_CASE("live-query routes are served when a db is attached") {
    const Fixture fx = buildFixture();
    MapDb map(fx.mapPath);
    map.setHasDb(true);
    QueryEngine engine(fx.dbPath, fx.pageSize, map);

    httplib::Server server;
    configureVisualizeRoutes(server, map, &engine);
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread th([&] { server.listen_after_bind(); });
    for (int i = 0; i < 200 && !server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(server.is_running());
    httplib::Client cli("127.0.0.1", port);

    SUBCASE("meta reports hasDb") {
        auto r = cli.Get("/api/meta");
        REQUIRE(r);
        CHECK(json::parse(r->body)["hasDb"] == true);
    }
    SUBCASE("POST /api/query/run returns results") {
        auto r = cli.Post("/api/query/run", "SELECT id FROM T ORDER BY id", "text/plain");
        REQUIRE(r);
        CHECK(r->status == 200);
        auto j = json::parse(r->body);
        CHECK(j["columns"][0]["name"] == "id");
        CHECK(j["rowCount"] == 3);
    }
    SUBCASE("bad SQL yields 400") {
        auto r = cli.Post("/api/query/run", "SELECT * FROM nope", "text/plain");
        REQUIRE(r);
        CHECK(r->status == 400);
        CHECK(json::parse(r->body).contains("error"));
    }
    SUBCASE("GET /api/schema returns the tree") {
        auto r = cli.Get("/api/schema");
        REQUIRE(r);
        CHECK(r->status == 200);
        CHECK(json::parse(r->body)["tables"].is_array());
    }

    server.stop();
    th.join();
}
