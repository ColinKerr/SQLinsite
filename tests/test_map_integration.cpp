#include <doctest/doctest.h>

#include <cstdio>
#include <string>

#include <sqlite3.h>

#include "map/map_command.hpp"
#include "test_util.hpp"

namespace {

void exec(sqlite3* db, const char* sql) {
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
}

void buildDb(const std::string& path, const char* sql) {
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    exec(db, sql);
    sqlite3_close(db);
}

// Opens a generated map and returns a single integer query result.
std::int64_t queryInt(const std::string& mapPath, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(mapPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
            SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
    std::int64_t value = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
}

std::string queryText(const std::string& mapPath, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(mapPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
            SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* t = sqlite3_column_text(stmt, 0);
        if (t) value = reinterpret_cast<const char*>(t);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
}

std::string mapOf(const std::string& dbPath, const std::string& name) {
    const std::string out = tmpPath(name);
    MapOptions options{dbPath, out};
    REQUIRE(runMap(options) == 0);
    return out;
}

}  // namespace

TEST_CASE("maps a table, index, and overflow row to SQLite") {
    const std::string path = tmpPath("map_basic.db");
    buildDb(path,
            "CREATE TABLE Fruit(id INTEGER PRIMARY KEY, name TEXT, notes TEXT);"
            "CREATE INDEX idx_fruit_name ON Fruit(name);"
            "INSERT INTO Fruit(name,notes) VALUES ('apple','a'),('banana','b');"
            "INSERT INTO Fruit(name,notes) VALUES ('big', zeroblob(5000));");
    const std::string map = mapOf(path, "map_basic.sqlite");

    // pages count matches meta.pageCount.
    const std::int64_t pageCount = queryInt(map, "SELECT pageCount FROM meta");
    CHECK(pageCount >= 4);
    CHECK(queryInt(map, "SELECT COUNT(*) FROM pages") == pageCount);

    // Object assignment + the overflow page belongs to Fruit.
    CHECK(queryText(map,
                    "SELECT pageType FROM pages WHERE pageNumber=1") == "table-leaf");
    CHECK(queryInt(map,
                   "SELECT COUNT(*) FROM pages p JOIN objects o ON o.id=p.objectId "
                   "WHERE o.name='Fruit' AND p.pageType='overflow'") == 1);

    // Index page has decoded keys persisted as JSON.
    CHECK(queryInt(map,
                   "SELECT COUNT(*) FROM cells c JOIN pages p ON p.pageNumber=c.pageNumber "
                   "WHERE p.pageType='index-leaf' AND c.keyJson IS NOT NULL") >= 1);

    // Every pointer references a valid page.
    CHECK(queryInt(map,
                   "SELECT COUNT(*) FROM pointers WHERE toPage < 1 OR toPage > "
                   "(SELECT pageCount FROM meta)") == 0);

    // Runs tile every page with no gaps or overlaps.
    CHECK(queryInt(map, "SELECT MIN(startPage) FROM runs") == 1);
    CHECK(queryInt(map, "SELECT MAX(endPage) FROM runs") == pageCount);
    CHECK(queryInt(map, "SELECT SUM(endPage-startPage+1) FROM runs") == pageCount);

    // type_counts agree with the pages table.
    CHECK(queryInt(map, "SELECT count FROM type_counts WHERE pageType='overflow'") ==
          queryInt(map, "SELECT COUNT(*) FROM pages WHERE pageType='overflow'"));

    // objects.pageCount agrees with the pages table.
    CHECK(queryInt(map,
                   "SELECT pageCount FROM objects WHERE name='Fruit'") ==
          queryInt(map,
                   "SELECT COUNT(*) FROM pages p JOIN objects o ON o.id=p.objectId "
                   "WHERE o.name='Fruit'"));
}

TEST_CASE("maps an auto_vacuum database with a pointer-map page") {
    const std::string path = tmpPath("map_av.db");
    buildDb(path,
            "PRAGMA auto_vacuum=FULL;"
            "CREATE TABLE T(a);"
            "INSERT INTO T VALUES (1),(2),(3),(4),(5);");
    const std::string map = mapOf(path, "map_av.sqlite");

    CHECK(queryText(map, "SELECT autoVacuum FROM meta") == "full");
    CHECK(queryText(map, "SELECT pageType FROM pages WHERE pageNumber=2") ==
          "pointer-map");
    CHECK(queryInt(map, "SELECT COUNT(*) FROM ptrmap") >= 1);
}

TEST_CASE("maps freelist pages after deletes") {
    const std::string path = tmpPath("map_free.db");
    std::string sql = "CREATE TABLE T(a INTEGER, b TEXT); BEGIN;";
    for (int i = 0; i < 2000; ++i)
        sql += "INSERT INTO T VALUES(" + std::to_string(i) + ",'padding padding');";
    sql += "COMMIT; DELETE FROM T WHERE a > 5;";
    buildDb(path, sql.c_str());
    const std::string map = mapOf(path, "map_free.sqlite");

    CHECK(queryInt(map, "SELECT freelistPageCount FROM meta") > 0);
    CHECK(queryInt(map,
                   "SELECT COUNT(*) FROM pages WHERE pageType IN "
                   "('freelist-trunk','freelist-leaf')") > 0);
}

TEST_CASE("map rejects a non-database file") {
    const std::string path = tmpPath("map_notdb.bin");
    writeTextFile(path, "not a database");
    MapOptions options{path, tmpPath("unused.sqlite")};
    CHECK(runMap(options) == 1);
}
