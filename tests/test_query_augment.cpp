#include <doctest/doctest.h>

#include "visualize/query_augment.hpp"

TEST_CASE("augmentWithRowids prepends rowid columns for a plain select") {
    auto a = augmentWithRowids("SELECT id, v FROM T ORDER BY id", {"T"});
    REQUIRE(a.ok);
    CHECK(a.sql == "SELECT \"T\".rowid, id, v FROM T ORDER BY id");
    CHECK(a.tables == std::vector<std::string>{"T"});
}

TEST_CASE("augmentWithRowids handles multiple source tables in order") {
    auto a = augmentWithRowids("select a.x, b.y from A a join B b", {"A", "B"});
    REQUIRE(a.ok);
    CHECK(a.sql == "select \"A\".rowid, \"B\".rowid, a.x, b.y from A a join B b");
}

TEST_CASE("augmentWithRowids refuses unsafe or unmappable queries") {
    CHECK_FALSE(augmentWithRowids("SELECT DISTINCT v FROM T", {"T"}).ok);
    CHECK_FALSE(augmentWithRowids("SELECT v FROM T GROUP BY v", {"T"}).ok);
    CHECK_FALSE(augmentWithRowids("WITH c AS (SELECT 1) SELECT * FROM c", {"c"}).ok);
    CHECK_FALSE(augmentWithRowids("SELECT 1", {}).ok);  // no source tables
}
