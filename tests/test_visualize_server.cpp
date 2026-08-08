#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "map/map_command.hpp"
#include "map/map_writer.hpp"
#include "visualize/analysis_db.hpp"
#include "visualize/map_db.hpp"
#include "test_util.hpp"
#include "visualize/visualize_command.hpp"

namespace {

static void putBE32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(v >> 24); b.push_back(v >> 16); b.push_back(v >> 8); b.push_back(v);
}

// A minimal v4 manifest with one parentless database `data.bim` of `nBlk` 16-byte
// blocks (ids = sequential bytes) at block size `szBlk`.
static std::string buildSimpleManifest(const std::string& path, std::uint32_t szBlk,
                                       std::uint32_t nBlk) {
    const std::uint32_t nName = 16;
    std::vector<std::uint8_t> b;
    putBE32(b, 4); putBE32(b, szBlk); putBE32(b, 1); putBE32(b, 0);
    putBE32(b, nName); putBE32(b, 1);
    putBE32(b, 1); putBE32(b, 0); putBE32(b, 1); putBE32(b, 24 + 152);
    putBE32(b, nBlk); putBE32(b, 0);
    std::vector<std::uint8_t> nm(128, 0);
    const char* name = "data.bim";
    for (std::size_t i = 0; name[i]; ++i) nm[i] = static_cast<std::uint8_t>(name[i]);
    b.insert(b.end(), nm.begin(), nm.end());
    for (std::uint32_t k = 0; k < nBlk; ++k)
        b.insert(b.end(), 16, static_cast<std::uint8_t>(k + 1));
    writeBinaryFile(path, b);
    return path;
}

std::string buildMapFixture() {
    const std::string dbPath = tmpPath("viz_src.db");
    const std::string mapPath = tmpPath("viz_src.sqlite");
    std::remove(dbPath.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
                         "CREATE TABLE T(id INTEGER PRIMARY KEY, v TEXT);"
                         "INSERT INTO T(v) VALUES ('a'),('b'),('c');",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    MapOptions options{dbPath, mapPath};
    REQUIRE(runMap(options) == 0);
    return mapPath;
}

// A fixture with an indexed table (T + index T_n) and an index-less table (U),
// for the tree's per-table grouping.
std::string buildIndexedMapFixture() {
    const std::string dbPath = tmpPath("viz_idx_src.db");
    const std::string mapPath = tmpPath("viz_idx_src.sqlite");
    std::remove(dbPath.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
                         "CREATE TABLE T(id INTEGER PRIMARY KEY, n INTEGER, v TEXT);"
                         "CREATE INDEX T_n ON T(n);"
                         "CREATE TABLE U(id INTEGER PRIMARY KEY, x TEXT);"
                         "INSERT INTO T(n,v) VALUES (1,'a'),(2,'b'),(3,'c');"
                         "INSERT INTO U(x) VALUES ('p'),('q');",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    MapOptions options{dbPath, mapPath};
    REQUIRE(runMap(options) == 0);
    return mapPath;
}

// Overwrites meta.formatVersion in an existing map file (to simulate a map made
// by an older/newer build).
void setMapFormatVersion(const std::string& mapPath, int version) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(mapPath.c_str(), &db) == SQLITE_OK);
    const std::string sql = "UPDATE meta SET formatVersion=" + std::to_string(version);
    REQUIRE(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}

}  // namespace

TEST_CASE("map format version is reported so mismatches can be detected") {
    const std::string mapPath = buildMapFixture();

    SUBCASE("a freshly written map matches the expected version") {
        MapDb db(mapPath);
        auto j = nlohmann::json::parse(db.metaJson());
        CHECK(j["meta"]["formatVersion"] == MapWriter::kFormatVersion);
        CHECK(j["expectedFormatVersion"] == MapWriter::kFormatVersion);
    }
    SUBCASE("an older map reports a lower version than expected") {
        setMapFormatVersion(mapPath, MapWriter::kFormatVersion - 1);
        MapDb db(mapPath);
        auto j = nlohmann::json::parse(db.metaJson());
        CHECK(j["expectedFormatVersion"] == MapWriter::kFormatVersion);
        CHECK(j["meta"]["formatVersion"].get<int>() < j["expectedFormatVersion"].get<int>());
    }
    SUBCASE("a newer map reports a higher version than expected") {
        setMapFormatVersion(mapPath, MapWriter::kFormatVersion + 1);
        MapDb db(mapPath);
        auto j = nlohmann::json::parse(db.metaJson());
        CHECK(j["expectedFormatVersion"] == MapWriter::kFormatVersion);
        CHECK(j["meta"]["formatVersion"].get<int>() > j["expectedFormatVersion"].get<int>());
    }
}

TEST_CASE("tree roots group each table's b-tree and indexes under one node") {
    MapDb db(buildIndexedMapFixture());
    auto roots = nlohmann::json::parse(db.treeRootsJson())["roots"];

    // Page 1 (sqlite_schema) is still its own root; tables are grouping nodes.
    bool sawSchema = false;
    nlohmann::json tNode, uNode;
    for (const auto& r : roots) {
        if (r.value("label", "") == "sqlite_schema") sawSchema = true;
        if (r.value("kind", "") == "table" && r.value("label", "") == "T") tNode = r;
        if (r.value("kind", "") == "table" && r.value("label", "") == "U") uNode = r;
    }
    CHECK(sawSchema);
    REQUIRE_FALSE(tNode.is_null());
    REQUIRE_FALSE(uNode.is_null());

    // T is a grouping node (no page of its own) whose b-tree child is "T (table)"
    // and whose one index child is "T_n (index)".
    CHECK(tNode["page"].is_null());
    CHECK(tNode["objectId"].is_number());
    CHECK(tNode["tableBtree"]["label"] == "T (table)");
    CHECK(tNode["tableBtree"]["page"].is_number());
    REQUIRE(tNode["indexes"].is_array());
    REQUIRE(tNode["indexes"].size() == 1);
    CHECK(tNode["indexes"][0]["label"] == "T_n (index)");
    CHECK(tNode["indexes"][0]["pageType"].get<std::string>().rfind("index", 0) == 0);

    // U has a b-tree but no indexes.
    CHECK(uNode["tableBtree"]["label"] == "U (table)");
    CHECK(uNode["indexes"].is_array());
    CHECK(uNode["indexes"].empty());
}

TEST_CASE("tree roots show 'All other pages' only for unowned non-structural pages") {
    const std::string mapPath = buildIndexedMapFixture();

    auto hasOther = [](const std::string& path) {
        MapDb db(path);
        auto roots = nlohmann::json::parse(db.treeRootsJson())["roots"];
        for (const auto& r : roots)
            if (r.value("kind", "") == "other" && r.value("label", "") == "All other pages")
                return true;
        return false;
    };

    // A well-formed map assigns every page to an object → no "other" node.
    CHECK_FALSE(hasOther(mapPath));

    // Mark one page unowned + non-structural (objectId NULL, a b-tree type). The
    // "other" node must appear — driven by objectId IS NULL, served by pages_object.
    sqlite3* m = nullptr;
    REQUIRE(sqlite3_open(mapPath.c_str(), &m) == SQLITE_OK);
    REQUIRE(sqlite3_exec(m,
                         "UPDATE pages SET objectId=NULL, pageType='table-leaf' "
                         "WHERE pageNumber=(SELECT MAX(pageNumber) FROM pages)",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(m);
    CHECK(hasOther(mapPath));
}

TEST_CASE("tree object overview reports a table's rows and indexes") {
    MapDb db(buildIndexedMapFixture());
    auto roots = nlohmann::json::parse(db.treeRootsJson())["roots"];
    std::int64_t tId = 0;
    for (const auto& r : roots)
        if (r.value("kind", "") == "table" && r.value("label", "") == "T")
            tId = r["objectId"].get<std::int64_t>();
    REQUIRE(tId > 0);

    auto ov = nlohmann::json::parse(db.treeObjectOverviewJson(tId))["overview"];
    CHECK(ov["name"] == "T");
    CHECK(ov["type"] == "table");
    CHECK(ov["rowCount"] == 3);
    CHECK(ov["sql"].get<std::string>().find("CREATE TABLE T") != std::string::npos);
    REQUIRE(ov["indexes"].size() == 1);
    CHECK(ov["indexes"][0]["name"] == "T_n");
    CHECK(ov["indexes"][0]["pageCount"].is_number());

    // A missing object yields an empty string (404 at the HTTP layer).
    CHECK(db.treeObjectOverviewJson(999999).empty());
}

TEST_CASE("minimap returns a bounded object-colored overview covering the whole file") {
    MapDb db(buildIndexedMapFixture());

    // Capped bucket count, contiguous, and covering exactly [1, pageCount].
    auto mm = nlohmann::json::parse(db.minimapJson(4));
    const std::int64_t pageCount = mm["pageCount"].get<std::int64_t>();
    REQUIRE(pageCount > 0);
    auto buckets = mm["buckets"];
    REQUIRE(buckets.is_array());
    REQUIRE_FALSE(buckets.empty());
    CHECK(buckets.size() <= 4);
    CHECK(buckets.front()["startPage"] == 1);
    CHECK(buckets.back()["endPage"] == pageCount);
    std::int64_t covered = 0;
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        if (i > 0)
            CHECK(buckets[i]["startPage"].get<std::int64_t>() ==
                  buckets[i - 1]["endPage"].get<std::int64_t>() + 1);
        covered += buckets[i]["endPage"].get<std::int64_t>() -
                   buckets[i]["startPage"].get<std::int64_t>() + 1;
        // objectId is null (unowned) or a real object id (0 is the schema/page-1).
        if (!buckets[i]["objectId"].is_null()) CHECK(buckets[i]["objectId"].get<std::int64_t>() >= 0);
    }
    CHECK(covered == pageCount);

    // At full resolution the real objects appear: table T owns pages, so some
    // bucket must carry its objectId.
    auto roots = nlohmann::json::parse(db.treeRootsJson())["roots"];
    std::int64_t tId = 0;
    for (const auto& r : roots)
        if (r.value("kind", "") == "table" && r.value("label", "") == "T")
            tId = r["objectId"].get<std::int64_t>();
    REQUIRE(tId > 0);
    auto full = nlohmann::json::parse(db.minimapJson(100000))["buckets"];
    bool sawT = false;
    for (const auto& b : full)
        if (!b["objectId"].is_null() && b["objectId"].get<std::int64_t>() == tId) sawT = true;
    CHECK(sawT);
}

TEST_CASE("tree search finds pages by page-number prefix") {
    // A table large enough for page numbers to reach three digits.
    const std::string dbPath = tmpPath("viz_search_src.db");
    const std::string mapPath = tmpPath("viz_search_src.sqlite");
    std::remove(dbPath.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(dbPath.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "CREATE TABLE T(id INTEGER PRIMARY KEY, s TEXT);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "INSERT INTO T(id,s) VALUES(?, ?)", -1, &st, nullptr) == SQLITE_OK);
    const std::string pad(80, 'x');
    for (int i = 1; i <= 6000; ++i) {
        sqlite3_bind_int(st, 1, i);
        sqlite3_bind_text(st, 2, pad.c_str(), -1, SQLITE_STATIC);
        REQUIRE(sqlite3_step(st) == SQLITE_DONE);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    REQUIRE(runMap(MapOptions{dbPath, mapPath}) == 0);

    MapDb map(mapPath);
    const std::int64_t pageCount =
        nlohmann::json::parse(map.metaJson())["meta"]["pageCount"].get<std::int64_t>();
    REQUIRE(pageCount >= 123);

    auto m = nlohmann::json::parse(map.treeSearchJson("123", 20))["matches"];
    REQUIRE(m.is_array());
    REQUIRE_FALSE(m.empty());
    // Exact value first; each match's page number starts with "123".
    CHECK(m[0]["page"] == 123);
    CHECK(m[0]["label"] == "Page 123");
    CHECK(m[0].contains("pageType"));
    CHECK(m[0].contains("subtreePageCount"));
    for (const auto& n : m)
        CHECK(std::to_string(n["page"].get<std::int64_t>()).rfind("123", 0) == 0);

    // Fewer than the cap: honored.
    CHECK(nlohmann::json::parse(map.treeSearchJson("123", 1))["matches"].size() == 1);
    // No page starts with these digits (out of range) → empty.
    CHECK(nlohmann::json::parse(map.treeSearchJson("999999", 20))["matches"].empty());
    // Non-digit / leading-zero queries → empty.
    CHECK(nlohmann::json::parse(map.treeSearchJson("12a", 20))["matches"].empty());
    CHECK(nlohmann::json::parse(map.treeSearchJson("012", 20))["matches"].empty());
}

TEST_CASE("server answers the map query API") {
    MapDb db(buildMapFixture());

    httplib::Server server;
    configureVisualizeRoutes(server, db);
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread th([&] { server.listen_after_bind(); });
    for (int i = 0; i < 200 && !server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(server.is_running());

    httplib::Client cli("127.0.0.1", port);

    SUBCASE("meta includes objects and type counts") {
        auto r = cli.Get("/api/meta");
        REQUIRE(r);
        CHECK(r->status == 200);
        auto j = nlohmann::json::parse(r->body);
        CHECK(j["meta"]["pageCount"].get<int>() >= 2);
        CHECK(j["objects"].is_array());
        CHECK(j["typeCounts"].is_array());
        REQUIRE(j["objects"].size() >= 1);
        CHECK(j["objects"][0].contains("tableName"));
        CHECK(j["objects"][0].contains("startPage"));
        CHECK(j["objects"][0].contains("startLeafPage"));
        CHECK(j["hasProfile"] == false);  // no profile loaded
        // The profile source manifest is served separately and starts empty.
        auto srcs = nlohmann::json::parse(cli.Get("/api/profile/sources")->body);
        CHECK(srcs["sources"].is_array());
        CHECK(srcs["sources"].empty());
    }
    SUBCASE("pages range returns rows") {
        auto r = cli.Get("/api/pages?from=1&to=2");
        REQUIRE(r);
        CHECK(r->status == 200);
        auto j = nlohmann::json::parse(r->body);
        CHECK(j["pages"].size() == 2);
        CHECK(j["pages"][0]["pageNumber"] == 1);
    }
    SUBCASE("oversize pages range is rejected with 413") {
        auto r = cli.Get("/api/pages?from=1&to=999999999");
        REQUIRE(r);
        CHECK(r->status == 413);
    }
    SUBCASE("runs overlap the range") {
        auto r = cli.Get("/api/runs?from=1&to=100");
        REQUIRE(r);
        CHECK(r->status == 200);
        auto j = nlohmann::json::parse(r->body);
        CHECK(j["runs"].size() >= 1);
        CHECK(j["runs"][0]["startPage"] == 1);
    }
    SUBCASE("single page detail includes pointers and cells") {
        auto r = cli.Get("/api/page/1");
        REQUIRE(r);
        CHECK(r->status == 200);
        auto j = nlohmann::json::parse(r->body);
        CHECK(j["pageNumber"] == 1);
        CHECK(j.contains("pointers"));
        CHECK(j.contains("cells"));
    }
    SUBCASE("missing page is 404") {
        auto r = cli.Get("/api/page/999999");
        REQUIRE(r);
        CHECK(r->status == 404);
    }
    SUBCASE("static assets and index are served") {
        auto idx = cli.Get("/");
        REQUIRE(idx);
        CHECK(idx->status == 200);
        auto js = cli.Get("/static/sqlinsite.js");
        REQUIRE(js);
        CHECK(js->status == 200);
        CHECK(js->get_header_value("Content-Type").find("javascript") != std::string::npos);
    }

    server.stop();
    th.join();
}

// Writes a `sqlinsite profile` output db (raw `accesses` table) at `path`.
static void writeProfileDb(const std::string& path, const char* accessRows) {
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const std::string sql =
        "CREATE TABLE accesses(sessionName TEXT, statementIndex INTEGER, "
        "timeStart INTEGER, timeEnd INTEGER, pageNumber INTEGER, access TEXT);" +
        std::string(accessRows);
    REQUIRE(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}

TEST_CASE("analysis connection queries map read-only over prefixes") {
    const std::string mapPath = buildMapFixture();
    AnalysisDb a(/*dbFile=*/"", mapPath, /*profile=*/"", /*manifest=*/"");

    auto j = nlohmann::json::parse(a.queryJson("SELECT count(*) AS n FROM map.pages"));
    CHECK(j["columns"][0]["name"] == "n");
    CHECK(j["rows"][0][0].get<std::int64_t>() >= 1);
    CHECK(j["truncated"] == false);

    // Bad SQL surfaces an error, not a crash.
    auto bad = nlohmann::json::parse(a.queryJson("SELECT * FROM nope"));
    CHECK(bad.contains("error"));

    // The connection is read-only (query_only), including attached dbs.
    auto ro = nlohmann::json::parse(a.queryJson("CREATE TABLE map.hax(x)"));
    CHECK(ro.contains("error"));
}

TEST_CASE("block endpoints map pages to manifest blocks") {
    const std::string mapPath = buildMapFixture();
    // Read pageSize/pageCount directly (sizing the synthetic manifest) before opening
    // MapDb, so the manifest is loaded before the pool connection is first used.
    int pageSize = 0;
    std::int64_t pageCount = 0;
    {
        sqlite3* m = nullptr;
        REQUIRE(sqlite3_open_v2(mapPath.c_str(), &m, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(m, "SELECT pageSize, pageCount FROM meta", -1, &s, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(s) == SQLITE_ROW);
        pageSize = sqlite3_column_int(s, 0);
        pageCount = sqlite3_column_int64(s, 1);
        sqlite3_finalize(s);
        sqlite3_close(m);
    }
    // Small block so the tiny fixture spans >1 block: 2 pages/block.
    const std::int64_t ppb = 2;
    const std::int64_t nBlk = (pageCount + ppb - 1) / ppb;
    const std::string man = buildSimpleManifest(tmpPath("viz.bcv"),
                                                static_cast<std::uint32_t>(ppb * pageSize),
                                                static_cast<std::uint32_t>(nBlk));
    MapDb db(mapPath);
    db.loadManifest(man, "");

    // meta now advertises the matching manifest.
    auto meta2 = nlohmann::json::parse(db.metaJson());
    CHECK(meta2["hasManifest"] == true);
    CHECK(meta2["manifestMatch"] == true);
    CHECK(meta2["pagesPerBlock"] == ppb);
    CHECK(meta2["blockCount"] == nBlk);

    auto blocks = nlohmann::json::parse(db.blocksJson(0, nBlk - 1))["blocks"];
    REQUIRE(static_cast<std::int64_t>(blocks.size()) == nBlk);
    // Blocks tile the pages: contiguous, real pages sum to pageCount, last block ends
    // at pageCount.
    std::int64_t realSum = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        CHECK(blocks[i]["startPage"].get<std::int64_t>() ==
              static_cast<std::int64_t>(i) * ppb + 1);
        realSum += blocks[i]["realPages"].get<std::int64_t>();
    }
    CHECK(realSum == pageCount);
    CHECK(blocks.back()["endPage"].get<std::int64_t>() == pageCount);

    // A page's detail names its block; block 0 holds pages 1..ppb.
    auto page1 = nlohmann::json::parse(db.pageJson(1, {}));
    CHECK(page1["block"]["blockIndex"] == 0);
    CHECK(page1["block"]["inBlockOffset"] == 0);
    auto blk0 = nlohmann::json::parse(db.blockJson(0, {}));
    CHECK(blk0["realPages"].get<std::int64_t>() >= 1);
    CHECK(blk0["objectName"].get<std::string>().rfind(".bcv") != std::string::npos);
}

TEST_CASE("profile overlay endpoints reflect a loaded profile") {
    MapDb db(buildMapFixture());
    const std::string profileDb = tmpPath("viz.profile.sqlite");
    // Two leaves: (S,0) reads page 1 twice; (S,1) writes page 2 once.
    writeProfileDb(profileDb,
                   "INSERT INTO accesses VALUES"
                   " ('S',0,1,2,1,'Read'),"
                   " ('S',0,3,4,1,'Read'),"
                   " ('S',1,5,6,2,'Write');");
    db.loadProfile(profileDb);

    httplib::Server server;
    configureVisualizeRoutes(server, db);
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread th([&] { server.listen_after_bind(); });
    for (int i = 0; i < 200 && !server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(server.is_running());

    httplib::Client cli("127.0.0.1", port);

    SUBCASE("meta reports a profile is present") {
        auto r = cli.Get("/api/meta");
        REQUIRE(r);
        CHECK(nlohmann::json::parse(r->body)["hasProfile"] == true);
    }

    SUBCASE("profile/sources lists the loaded input sources") {
        auto r = cli.Get("/api/profile/sources");
        REQUIRE(r);
        auto j = nlohmann::json::parse(r->body);
        REQUIRE(j["sources"].size() == 2);  // (S,0) and (S,1)
        CHECK(j["sources"][0]["kind"] == "input");
        CHECK(j["sources"][0]["sessionName"] == "S");
        CHECK(j["sources"][0]["sessionId"] == 0);
        CHECK(j["sources"][1]["sessionId"] == 1);
    }

    SUBCASE("profile pages aggregate across all leaves") {
        auto pages = cli.Get("/api/profile/pages?from=1&to=10");
        REQUIRE(pages);
        auto pj = nlohmann::json::parse(pages->body);
        REQUIRE(pj["pages"].size() == 2);
        CHECK(pj["pages"][0]["pageNumber"] == 1);
        CHECK(pj["pages"][0]["reads"] == 2);
    }

    SUBCASE("sel filters profile pages to selected sources") {
        // sourceId 1 = leaf (S,0), which reads page 1 twice.
        auto pages = cli.Get("/api/profile/pages?from=1&to=10&sel=1");
        REQUIRE(pages);
        auto pj = nlohmann::json::parse(pages->body);
        REQUIRE(pj["pages"].size() == 1);
        CHECK(pj["pages"][0]["pageNumber"] == 1);
        CHECK(pj["pages"][0]["reads"] == 2);
    }

    SUBCASE("page detail respects the sel filter") {
        auto all = nlohmann::json::parse(cli.Get("/api/page/1")->body);
        CHECK(all["profile"]["reads"] == 2);
        // sourceId 2 = leaf (S,1), which touches page 2, not page 1.
        auto other = nlohmann::json::parse(cli.Get("/api/page/1?sel=2")->body);
        CHECK(other["profile"]["reads"] == 0);
    }

    server.stop();
    th.join();
}
