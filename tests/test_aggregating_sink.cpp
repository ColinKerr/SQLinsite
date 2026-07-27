#include <doctest/doctest.h>

#include "profile/aggregating_sink.hpp"

TEST_CASE("aggregates reads/writes per page and totals") {
    AggregatingSink sink;
    sink.record("q", 0, 1, 2, 5, AccessType::Read);
    sink.record("q", 0, 3, 4, 5, AccessType::Read);
    sink.record("q", 0, 5, 6, 2, AccessType::Write);
    sink.record("q", 0, 7, 8, 9, AccessType::Read);

    CHECK(sink.reads() == 3);
    CHECK(sink.writes() == 1);
    CHECK(sink.accesses() == 4);
    CHECK(sink.distinctPages() == 3);

    const auto pages = sink.pages();
    REQUIRE(pages.size() == 3);
    // ascending by page number
    CHECK(pages[0].pageNumber == 2);
    CHECK(pages[0].writes == 1);
    CHECK(pages[1].pageNumber == 5);
    CHECK(pages[1].reads == 2);
    CHECK(pages[2].pageNumber == 9);
    CHECK(pages[2].reads == 1);
}

TEST_CASE("empty sink") {
    AggregatingSink sink;
    CHECK(sink.accesses() == 0);
    CHECK(sink.distinctPages() == 0);
    CHECK(sink.pages().empty());
}
