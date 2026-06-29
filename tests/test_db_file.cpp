#include <doctest/doctest.h>

#include <cstdio>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "map/db_file.hpp"
#include "test_util.hpp"

namespace {

void buildDb(const std::string& path, const char* extra = "") {
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const std::string sql =
        std::string("CREATE TABLE Fruit(id INTEGER PRIMARY KEY, name TEXT);"
                    "INSERT INTO Fruit(name) VALUES ('apple'),('banana');") +
        extra;
    REQUIRE(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) ==
            SQLITE_OK);
    sqlite3_close(db);
}

}  // namespace

TEST_CASE("reads header and pages") {
    const std::string path = tmpPath("dbfile.db");
    buildDb(path);

    DbFile f = DbFile::open(path);
    CHECK(f.pageSize() > 0);
    CHECK(f.pageCount() >= 2);
    CHECK(f.usableSize() == f.pageSize() - f.header().reservedBytesPerPage);

    const auto& page1 = f.page(1);
    REQUIRE(page1.size() >= 16);
    const std::string magic(reinterpret_cast<const char*>(page1.data()), 15);
    CHECK(magic == "SQLite format 3");

    CHECK(f.header().textEncoding == 1);  // utf-8 default
    CHECK(f.header().sqliteVersionNumber > 3000000);
}

TEST_CASE("detects auto_vacuum") {
    const std::string path = tmpPath("dbfile_av.db");
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
                         "PRAGMA auto_vacuum=FULL;"
                         "CREATE TABLE T(a);"
                         "INSERT INTO T VALUES (1),(2),(3);",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);

    DbFile f = DbFile::open(path);
    CHECK(f.autoVacuum() == true);
}

TEST_CASE("out-of-range page throws") {
    const std::string path = tmpPath("dbfile2.db");
    buildDb(path);
    DbFile f = DbFile::open(path);
    CHECK_THROWS_AS(f.page(0), std::out_of_range);
    CHECK_THROWS_AS(f.page(f.pageCount() + 1), std::out_of_range);
}

TEST_CASE("non-database file is rejected") {
    const std::string path = tmpPath("not_a_db.txt");
    writeTextFile(path, "this is definitely not a sqlite database file");
    CHECK_THROWS_AS(DbFile::open(path), std::runtime_error);
}
