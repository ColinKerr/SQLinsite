#include <doctest/doctest.h>

#include "visualize/run_coalesce.hpp"

TEST_CASE("coalesces contiguous same-type same-object pages into one run") {
    std::vector<PageMeta> pages = {
        {1, "table-leaf", 7},
        {2, "table-leaf", 7},
        {3, "table-leaf", 7},
    };
    auto runs = coalesceRuns(pages);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].startPage == 1);
    CHECK(runs[0].endPage == 3);
    CHECK(runs[0].pageType == "table-leaf");
    CHECK(runs[0].objectId == 7);
}

TEST_CASE("breaks runs on gaps, type changes, and object changes") {
    std::vector<PageMeta> pages = {
        {1, "table-leaf", 7},
        {2, "table-leaf", 7},
        {4, "table-leaf", 7},          // gap (3 missing)
        {5, "index-leaf", 7},          // type change
        {6, "index-leaf", 8},          // object change
        {7, "overflow", std::nullopt}, // structural (null object)
        {8, "overflow", std::nullopt},
    };
    auto runs = coalesceRuns(pages);
    REQUIRE(runs.size() == 5);
    CHECK(runs[0].startPage == 1);
    CHECK(runs[0].endPage == 2);
    CHECK(runs[1].startPage == 4);
    CHECK(runs[1].endPage == 4);
    CHECK(runs[2].startPage == 5);
    CHECK(runs[3].startPage == 6);
    CHECK(runs[4].startPage == 7);
    CHECK(runs[4].endPage == 8);
    CHECK_FALSE(runs[4].objectId.has_value());
}

TEST_CASE("empty input yields no runs") {
    CHECK(coalesceRuns({}).empty());
}
