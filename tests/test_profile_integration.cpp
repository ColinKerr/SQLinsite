#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include <sqlite3.h>

#include "profile/profile_command.hpp"
#include "test_util.hpp"

namespace {

void buildFixtureDb(const std::string& path) {
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const char* setup =
        "CREATE TABLE Fruit(id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO Fruit(name) VALUES ('apple'),('banana'),('cherry');";
    char* err = nullptr;
    REQUIRE(sqlite3_exec(db, setup, nullptr, nullptr, &err) == SQLITE_OK);
    sqlite3_close(db);
}

void buildFixtureDbWithPageSize(const std::string& path, int pageSize) {
    std::remove(path.c_str());
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const std::string setup =
        "PRAGMA page_size=" + std::to_string(pageSize) + ";"
        "CREATE TABLE Fruit(id INTEGER PRIMARY KEY, name TEXT);"
        "INSERT INTO Fruit(name) VALUES ('apple'),('banana'),('cherry');";
    char* err = nullptr;
    REQUIRE(sqlite3_exec(db, setup.c_str(), nullptr, nullptr, &err) == SQLITE_OK);
    sqlite3_close(db);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int actualPageSize(const std::string& path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA page_size", -1, &stmt, nullptr) ==
            SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const int pageSize = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return pageSize;
}

}  // namespace

TEST_CASE("profile produces read and write rows for the main db") {
    const std::string dbPath = tmpPath("fixture.db");
    const std::string statementsPath = tmpPath("integration_statements.json");
    const std::string outPath = tmpPath("integration_out.csv");
    buildFixtureDb(dbPath);

    writeTextFile(statementsPath, R"json({
        "TestRun": {
            "Name": "Integration",
            "Sessions": [
                {
                    "SessionName": "ReadSession",
                    "Statements": ["SELECT * FROM Fruit"]
                },
                {
                    "SessionName": "WriteSession",
                    "Statements": ["INSERT INTO Fruit(name) VALUES ('date')"]
                }
            ]
        }
    })json");

    ProfileOptions options{dbPath, statementsPath, outPath, false, true};
    CHECK(runProfile(options) == 0);

    const std::string csv = readTextFile(outPath);
    CHECK(contains(csv, "Session Name,Statement Index"));
    CHECK(contains(csv, "ReadSession,0,"));
    CHECK(contains(csv, ",Read\n"));
    CHECK(contains(csv, "WriteSession,0,"));
    CHECK(contains(csv, ",Write\n"));

    // The in-place INSERT must be visible afterward.
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(dbPath.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Fruit", -1, &stmt,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(sqlite3_column_int(stmt, 0) == 4);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

TEST_CASE("page indices are correct with a 65536-byte page size") {
    const std::string dbPath = tmpPath("fixture_64k.db");
    const std::string statementsPath = tmpPath("statements_64k.json");
    const std::string outPath = tmpPath("out_64k.csv");
    buildFixtureDbWithPageSize(dbPath, 65536);
    REQUIRE(actualPageSize(dbPath) == 65536);

    writeTextFile(statementsPath, R"json({
        "TestRun": {
            "Name": "BigPage",
            "Sessions": [
                { "SessionName": "S", "Statements": ["SELECT * FROM Fruit"] }
            ]
        }
    })json");

    ProfileOptions options{dbPath, statementsPath, outPath, false, true};
    CHECK(runProfile(options) == 0);

    const std::string csv = readTextFile(outPath);
    // A small DB at 64k page size keeps everything in page 0; no negative or
    // bogus indices should appear.
    CHECK(contains(csv, "S,0,"));
    CHECK(!contains(csv, ",-1,"));
}

TEST_CASE("a failing statement does not stop later statements but fails the run") {
    const std::string dbPath = tmpPath("fixture_err.db");
    const std::string statementsPath = tmpPath("statements_err.json");
    const std::string outPath = tmpPath("out_err.csv");
    buildFixtureDb(dbPath);

    writeTextFile(statementsPath, R"json({
        "TestRun": {
            "Name": "ErrorPolicy",
            "Sessions": [
                {
                    "SessionName": "S",
                    "Statements": [
                        "SELECT * FROM NoSuchTable",
                        "SELECT * FROM Fruit"
                    ]
                }
            ]
        }
    })json");

    ProfileOptions options{dbPath, statementsPath, outPath, false, true};
    CHECK(runProfile(options) != 0);  // overall failure...

    const std::string csv = readTextFile(outPath);
    CHECK(contains(csv, "S,1,"));  // ...but statement 1 still ran and logged
}

TEST_CASE("a missing test file is rejected") {
    const std::string statementsPath = tmpPath("statements_missing.json");
    const std::string outPath = tmpPath("out_missing.csv");
    writeTextFile(statementsPath, R"json({
        "TestRun": {
            "Name": "Missing",
            "Sessions": [
                { "SessionName": "S", "Statements": ["SELECT 1"] }
            ]
        }
    })json");

    ProfileOptions options{tmpPath("does_not_exist.db"), statementsPath, outPath,
                           false, true};
    CHECK(runProfile(options) != 0);
}

TEST_CASE("relative timing rebases timestamps near zero") {
    const std::string dbPath = tmpPath("fixture_rel.db");
    const std::string statementsPath = tmpPath("statements_rel.json");
    const std::string outPath = tmpPath("out_rel.csv");
    buildFixtureDb(dbPath);
    writeTextFile(statementsPath, R"json({
        "TestRun": {
            "Name": "Relative",
            "Sessions": [
                { "SessionName": "S", "Statements": ["SELECT * FROM Fruit"] }
            ]
        }
    })json");

    ProfileOptions options{dbPath, statementsPath, outPath,
                           /*relativeTiming=*/true, /*quiet=*/true};
    CHECK(runProfile(options) == 0);

    // Parse the first data row's Time Start: with rebasing it must be small
    // (well under one second of nanoseconds), unlike raw steady_clock ticks.
    const std::string csv = readTextFile(outPath);
    const std::size_t nl = csv.find('\n');
    REQUIRE(nl != std::string::npos);
    const std::string firstRow = csv.substr(nl + 1, csv.find('\n', nl + 1) - nl - 1);
    // Columns: name,index,start,end,page,rw  -> take field 2 (start).
    const std::size_t c1 = firstRow.find(',');
    const std::size_t c2 = firstRow.find(',', c1 + 1);
    const std::size_t c3 = firstRow.find(',', c2 + 1);
    const std::int64_t start =
        std::stoll(firstRow.substr(c2 + 1, c3 - c2 - 1));
    CHECK(start >= 0);
    CHECK(start < 1'000'000'000);  // < 1s of ns since the run baseline
}
