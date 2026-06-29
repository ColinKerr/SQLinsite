#include <doctest/doctest.h>

#include <string>

#include <nlohmann/json.hpp>

#include "visualize/profile_reader.hpp"

TEST_CASE("aggregates reads and writes per page number") {
    const std::string csv =
        "Session Name,Statement Index,Time Start,Time End,Page Number,Read or Write\n"
        "S,0,1,2,1,Read\n"
        "S,0,3,4,2,Read\n"
        "S,0,5,6,2,Read\n"
        "S,1,7,8,2,Write\n";

    ProfileAggregate agg = aggregateProfileCsv(csv);
    REQUIRE(agg.pages.size() == 2);
    CHECK(agg.pages[0].pageNumber == 1);
    CHECK(agg.pages[0].reads == 1);
    CHECK(agg.pages[0].writes == 0);
    CHECK(agg.pages[1].pageNumber == 2);
    CHECK(agg.pages[1].reads == 2);
    CHECK(agg.pages[1].writes == 1);
    CHECK(agg.totalReads == 3);
    CHECK(agg.totalWrites == 1);
}

TEST_CASE("handles quoted session names containing commas") {
    const std::string csv =
        "Session Name,Statement Index,Time Start,Time End,Page Number,Read or Write\n"
        "\"weird,name\",0,1,2,5,Read\n";
    ProfileAggregate agg = aggregateProfileCsv(csv);
    REQUIRE(agg.pages.size() == 1);
    CHECK(agg.pages[0].pageNumber == 5);
    CHECK(agg.pages[0].reads == 1);
}

TEST_CASE("empty profile aggregates to nothing") {
    const std::string csv =
        "Session Name,Statement Index,Time Start,Time End,Page Number,Read or Write\n";
    ProfileAggregate agg = aggregateProfileCsv(csv);
    CHECK(agg.pages.empty());
    CHECK(agg.totalReads == 0);
}

TEST_CASE("serializes to the api/profile shape") {
    const std::string csv =
        "Session Name,Statement Index,Time Start,Time End,Page Number,Read or Write\n"
        "S,0,1,2,3,Write\n";
    nlohmann::json j = nlohmann::json::parse(profileJson(aggregateProfileCsv(csv)));
    REQUIRE(j["pages"].size() == 1);
    CHECK(j["pages"][0]["pageNumber"] == 3);
    CHECK(j["pages"][0]["writes"] == 1);
    CHECK(j["totals"]["writes"] == 1);
}
