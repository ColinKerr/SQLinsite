#include <doctest/doctest.h>

#include <stdexcept>

#include "profile/statements_file.hpp"
#include "test_util.hpp"

TEST_CASE("parses a valid statements file") {
    const std::string path = tmpPath("valid_statements.json");
    writeTextFile(path, R"({
        "TestRun": {
            "Name": "Test Name",
            "Sessions": [
                {
                    "SessionName": "Session One",
                    "Statements": ["SELECT * FROM Banana", "SELECT * FROM Apple"]
                },
                {
                    "SessionName": "Session Two",
                    "Statements": ["SELECT * FROM Juice"]
                }
            ]
        }
    })");

    TestRun run = parseStatementsFile(path);

    CHECK(run.name == "Test Name");
    REQUIRE(run.sessions.size() == 2);
    CHECK(run.sessions[0].name == "Session One");
    REQUIRE(run.sessions[0].statements.size() == 2);
    CHECK(run.sessions[0].statements[1] == "SELECT * FROM Apple");
    CHECK(run.sessions[1].statements[0] == "SELECT * FROM Juice");
}

TEST_CASE("rejects malformed JSON") {
    const std::string path = tmpPath("malformed.json");
    writeTextFile(path, "{ not json");
    CHECK_THROWS_AS(parseStatementsFile(path), std::runtime_error);
}

TEST_CASE("rejects missing required fields") {
    const std::string path = tmpPath("missing_fields.json");
    writeTextFile(path, R"({"TestRun": {"Name": "x"}})");
    CHECK_THROWS_AS(parseStatementsFile(path), std::runtime_error);
}
