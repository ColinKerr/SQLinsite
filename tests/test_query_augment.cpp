#include <doctest/doctest.h>

#include "visualize/query_augment.hpp"

TEST_CASE("augmentWithRowids prepends rowid columns for a plain select") {
    auto a = augmentWithRowids("SELECT id, v FROM T ORDER BY id", {"T"});
    REQUIRE(a.ok);
    CHECK(a.sql == "SELECT \"T\".rowid, id, v FROM T ORDER BY id");
    CHECK(a.tables == std::vector<std::string>{"T"});
}

TEST_CASE("augmentWithRowids references a table by its FROM-clause alias") {
    // The rowid must be qualified with the alias, not the table name — otherwise
    // SQLite can't resolve "<table>".rowid once the table is aliased.
    auto a = augmentWithRowids("SELECT ge.GeometryStream FROM bis_GeometricElement3d ge",
                               {"bis_GeometricElement3d"});
    REQUIRE(a.ok);
    CHECK(a.sql ==
          "SELECT \"ge\".rowid, ge.GeometryStream FROM bis_GeometricElement3d ge");

    // `AS <alias>` form.
    auto b = augmentWithRowids("SELECT t.v FROM T AS t", {"T"});
    REQUIRE(b.ok);
    CHECK(b.sql == "SELECT \"t\".rowid, t.v FROM T AS t");
}

TEST_CASE("augmentWithRowids handles multiple source tables, aliased and not") {
    // Aliased join: each rowid uses the table's alias.
    auto a = augmentWithRowids("select a.x, b.y from A a join B b", {"A", "B"});
    REQUIRE(a.ok);
    CHECK(a.sql == "select \"a\".rowid, \"b\".rowid, a.x, b.y from A a join B b");

    // Unaliased join keeps the table names.
    auto b = augmentWithRowids("select A.x, B.y from A join B on A.id=B.id", {"A", "B"});
    REQUIRE(b.ok);
    CHECK(b.sql ==
          "select \"A\".rowid, \"B\".rowid, A.x, B.y from A join B on A.id=B.id");
}

TEST_CASE("augmentWithRowids ignores aliases inside strings/comments") {
    // 'from X y' in a literal must not be parsed as a table reference.
    auto a = augmentWithRowids("SELECT v FROM T WHERE v='from X y'", {"T"});
    REQUIRE(a.ok);
    CHECK(a.sql == "SELECT \"T\".rowid, v FROM T WHERE v='from X y'");
}

TEST_CASE("augmentWithRowids refuses unsafe or unmappable queries") {
    CHECK_FALSE(augmentWithRowids("SELECT DISTINCT v FROM T", {"T"}).ok);
    CHECK_FALSE(augmentWithRowids("SELECT v FROM T GROUP BY v", {"T"}).ok);
    CHECK_FALSE(augmentWithRowids("WITH c AS (SELECT 1) SELECT * FROM c", {"c"}).ok);
    CHECK_FALSE(augmentWithRowids("SELECT 1", {}).ok);  // no source tables
}
