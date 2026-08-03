#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "profile/profile_command.hpp"
#include "profile/sqlite_writer.hpp"
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

// Opens the profile db and runs a scalar-int query (first column of first row).
std::int64_t profileScalar(const std::string& path, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
            SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
    std::int64_t v = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) v = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return v;
}

std::string profileText(const std::string& path, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) ==
            SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
    std::string v;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (t) v = t;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return v;
}

}  // namespace

TEST_CASE("profile produces read and write rows for the main db") {
    const std::string dbPath = tmpPath("fixture.db");
    const std::string statementsPath = tmpPath("integration_statements.json");
    const std::string outPath = tmpPath("integration_out.sqlite");
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

    // The output is a SQLite db with a raw `accesses` table and a `meta` row.
    CHECK(profileScalar(outPath,
                        "SELECT COUNT(*) FROM accesses WHERE sessionName='ReadSession' "
                        "AND statementIndex=0 AND access='Read'") > 0);
    CHECK(profileScalar(outPath,
                        "SELECT COUNT(*) FROM accesses WHERE sessionName='WriteSession' "
                        "AND access='Write'") > 0);
    // Meta carries the run's provenance/compat fields.
    CHECK(profileScalar(outPath, "SELECT formatVersion FROM meta") ==
          SqliteWriter::kFormatVersion);
    CHECK(profileText(outPath, "SELECT name FROM meta") == "Integration");
    CHECK(profileText(outPath, "SELECT timing FROM meta") == "raw");
    CHECK(profileScalar(outPath, "SELECT pageSize FROM meta") == actualPageSize(dbPath));

    // The in-place INSERT must be visible afterward.
    CHECK(profileScalar(dbPath, "SELECT COUNT(*) FROM Fruit") == 4);
}

TEST_CASE("page indices are correct with a 65536-byte page size") {
    const std::string dbPath = tmpPath("fixture_64k.db");
    const std::string statementsPath = tmpPath("statements_64k.json");
    const std::string outPath = tmpPath("out_64k.sqlite");
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

    CHECK(profileScalar(outPath, "SELECT COUNT(*) FROM accesses WHERE sessionName='S'") > 0);
    // No negative/bogus page indices.
    CHECK(profileScalar(outPath, "SELECT COUNT(*) FROM accesses WHERE pageNumber<0") == 0);
    CHECK(profileScalar(outPath, "SELECT pageSize FROM meta") == 65536);
}

TEST_CASE("a failing statement does not stop later statements but fails the run") {
    const std::string dbPath = tmpPath("fixture_err.db");
    const std::string statementsPath = tmpPath("statements_err.json");
    const std::string outPath = tmpPath("out_err.sqlite");
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

    // ...but statement 1 still ran and logged.
    CHECK(profileScalar(outPath,
                        "SELECT COUNT(*) FROM accesses WHERE sessionName='S' "
                        "AND statementIndex=1") > 0);
}

TEST_CASE("a missing test file is rejected") {
    const std::string statementsPath = tmpPath("statements_missing.json");
    const std::string outPath = tmpPath("out_missing.sqlite");
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
    const std::string outPath = tmpPath("out_rel.sqlite");
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

    // With rebasing the earliest timeStart must be small (well under 1s of ns),
    // unlike raw steady_clock ticks; and meta records the timing mode.
    CHECK(profileScalar(outPath, "SELECT MIN(timeStart) FROM accesses") >= 0);
    CHECK(profileScalar(outPath, "SELECT MIN(timeStart) FROM accesses") < 1'000'000'000);
    CHECK(profileText(outPath, "SELECT timing FROM meta") == "relative");
}
