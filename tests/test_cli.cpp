#include <doctest/doctest.h>

#include "common/cli.hpp"

namespace {
ParsedCli parse(std::vector<std::string> args) { return parseCli(args); }
}  // namespace

TEST_CASE("parses a full profile invocation") {
    ParsedCli p = parse({"profile", "--test-file", "a.db", "--statements",
                         "s.json", "--out-file", "o.sqlite"});
    REQUIRE(p.kind == ParsedCli::Kind::Profile);
    CHECK(p.profile.testFile == "a.db");
    CHECK(p.profile.statementsFile == "s.json");
    CHECK(p.profile.outFile == "o.sqlite");
}

TEST_CASE("supports --flag=value form") {
    ParsedCli p = parse({"profile", "--test-file=a.db", "--statements=s.json",
                         "--out-file=o.sqlite"});
    REQUIRE(p.kind == ParsedCli::Kind::Profile);
    CHECK(p.profile.testFile == "a.db");
    CHECK(p.profile.outFile == "o.sqlite");
}

TEST_CASE("help is requested in several forms") {
    for (const char* arg : {"--help", "-h", "help"}) {
        ParsedCli p = parse({arg});
        CHECK(p.kind == ParsedCli::Kind::Help);
        CHECK(p.exitCode == 0);
    }
}

TEST_CASE("help within profile is honored") {
    ParsedCli p = parse({"profile", "--help"});
    CHECK(p.kind == ParsedCli::Kind::Help);
}

TEST_CASE("parses a map invocation") {
    ParsedCli p = parse({"map", "--test-file", "a.db", "--out-file", "a.map.json"});
    REQUIRE(p.kind == ParsedCli::Kind::Map);
    CHECK(p.map.testFile == "a.db");
    CHECK(p.map.outFile == "a.map.json");
}

TEST_CASE("map requires both options") {
    ParsedCli p = parse({"map", "--test-file", "a.db"});
    REQUIRE(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("--out-file") != std::string::npos);
}

TEST_CASE("map rejects unknown options") {
    ParsedCli p = parse({"map", "--test-file", "a.db", "--out-file", "o", "--wat"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("unknown option") != std::string::npos);
}

TEST_CASE("parses visualize serve") {
    ParsedCli p = parse({"visualize", "serve", "--map-file", "m.json",
                         "--profile-file", "p.csv", "--port", "9090"});
    REQUIRE(p.kind == ParsedCli::Kind::VisualizeServe);
    CHECK(p.visualize.mapFile == "m.json");
    CHECK(p.visualize.profileFile == "p.csv");
    CHECK(p.visualize.port == 9090);
}

TEST_CASE("visualize serve defaults port and allows no profile") {
    ParsedCli p = parse({"visualize", "serve", "--map-file", "m.json"});
    REQUIRE(p.kind == ParsedCli::Kind::VisualizeServe);
    CHECK(p.visualize.port == 8080);
    CHECK(p.visualize.profileFile.empty());
}

TEST_CASE("visualize requires a subcommand") {
    ParsedCli p = parse({"visualize"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("subcommand") != std::string::npos);
}

TEST_CASE("visualize rejects an unknown subcommand") {
    ParsedCli p = parse({"visualize", "dance"});
    CHECK(p.kind == ParsedCli::Kind::Error);
}

TEST_CASE("visualize serve requires --map-file") {
    ParsedCli p = parse({"visualize", "serve", "--port", "9000"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("--map-file") != std::string::npos);
}

TEST_CASE("visualize serve rejects a non-numeric port") {
    ParsedCli p = parse({"visualize", "serve", "--map-file", "m.json", "--port", "abc"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("--port") != std::string::npos);
}

TEST_CASE("no command is an error") {
    ParsedCli p = parse({});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.exitCode == 2);
}

TEST_CASE("unknown command is an error") {
    ParsedCli p = parse({"frobnicate"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("unknown command") != std::string::npos);
}

TEST_CASE("unknown option is an error") {
    ParsedCli p = parse({"profile", "--wat", "x"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("unknown option") != std::string::npos);
}

TEST_CASE("missing value is an error") {
    ParsedCli p = parse({"profile", "--test-file"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("missing value") != std::string::npos);
}

TEST_CASE("timing and quiet options parse") {
    ParsedCli p = parse({"profile", "--test-file", "a.db", "--statements",
                         "s.json", "--out-file", "o.sqlite", "--timing", "relative",
                         "--quiet"});
    REQUIRE(p.kind == ParsedCli::Kind::Profile);
    CHECK(p.profile.relativeTiming == true);
    CHECK(p.profile.quiet == true);
}

TEST_CASE("timing defaults to raw") {
    ParsedCli p = parse({"profile", "--test-file", "a.db", "--statements",
                         "s.json", "--out-file", "o.sqlite"});
    REQUIRE(p.kind == ParsedCli::Kind::Profile);
    CHECK(p.profile.relativeTiming == false);
}

TEST_CASE("invalid timing value is an error") {
    ParsedCli p = parse({"profile", "--test-file", "a.db", "--statements",
                         "s.json", "--out-file", "o.sqlite", "--timing", "bogus"});
    CHECK(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("--timing") != std::string::npos);
}

TEST_CASE("--quiet rejects an inline value") {
    ParsedCli p = parse({"profile", "--test-file", "a.db", "--statements",
                         "s.json", "--out-file", "o.sqlite", "--quiet=1"});
    CHECK(p.kind == ParsedCli::Kind::Error);
}

TEST_CASE("missing required options are reported") {
    ParsedCli p = parse({"profile", "--test-file", "a.db"});
    REQUIRE(p.kind == ParsedCli::Kind::Error);
    CHECK(p.message.find("--statements") != std::string::npos);
    CHECK(p.message.find("--out-file") != std::string::npos);
}
