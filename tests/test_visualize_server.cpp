#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "map/map_command.hpp"
#include "visualize/map_db.hpp"
#include "test_util.hpp"
#include "visualize/visualize_command.hpp"

namespace {

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

}  // namespace

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
        auto js = cli.Get("/static/app.js");
        REQUIRE(js);
        CHECK(js->status == 200);
        CHECK(js->get_header_value("Content-Type").find("javascript") != std::string::npos);
    }

    server.stop();
    th.join();
}

TEST_CASE("profile overlay endpoints reflect a loaded profile") {
    MapDb db(buildMapFixture());
    const std::string csv = tmpPath("viz.csv");
    writeTextFile(csv,
                  "Session Name,Statement Index,Time Start,Time End,Page Number,Read or Write\n"
                  "S,0,1,2,1,Read\n"
                  "S,0,3,4,1,Read\n"
                  "S,0,5,6,2,Write\n");
    db.loadProfile(csv);

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

    auto pages = cli.Get("/api/profile/pages?from=1&to=10");
    REQUIRE(pages);
    auto pj = nlohmann::json::parse(pages->body);
    REQUIRE(pj["pages"].size() == 2);
    CHECK(pj["pages"][0]["pageNumber"] == 1);
    CHECK(pj["pages"][0]["reads"] == 2);

    auto hist = cli.Get("/api/profile/histogram?from=1&to=10&bins=2");
    REQUIRE(hist);
    auto hj = nlohmann::json::parse(hist->body);
    CHECK(hj["bins"].size() == 2);

    auto page = cli.Get("/api/page/1");
    auto pgj = nlohmann::json::parse(page->body);
    CHECK(pgj["profile"]["reads"] == 2);

    server.stop();
    th.join();
}
