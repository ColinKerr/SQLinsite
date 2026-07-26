#include <doctest/doctest.h>

#include "visualize/query_augment.hpp"

namespace {
FromInstance inst(const std::string& table, const std::string& ref) { return {table, ref}; }
}

TEST_CASE("augmentWithRowids prepends a rowid per instance, by its reference") {
    auto a = augmentWithRowids("SELECT id, v FROM T ORDER BY id", {inst("T", "T")});
    REQUIRE(a.ok);
    CHECK(a.sql == "SELECT \"T\".rowid, id, v FROM T ORDER BY id");

    // Aliased: the rowid uses the alias so it resolves.
    auto b = augmentWithRowids("SELECT ge.g FROM bis_GeometricElement3d ge",
                               {inst("bis_GeometricElement3d", "ge")});
    REQUIRE(b.ok);
    CHECK(b.sql == "SELECT \"ge\".rowid, ge.g FROM bis_GeometricElement3d ge");

    // A self-joined table contributes two independent rowids (its two aliases).
    auto c = augmentWithRowids("select es.a, rl.b from R es join R rl on es.id=rl.pid",
                               {inst("R", "es"), inst("R", "rl")});
    REQUIRE(c.ok);
    CHECK(c.sql ==
          "select \"es\".rowid, \"rl\".rowid, es.a, rl.b from R es join R rl on es.id=rl.pid");
}

TEST_CASE("augmentWithRowids refuses unsafe or unmappable queries") {
    CHECK_FALSE(augmentWithRowids("SELECT DISTINCT v FROM T", {inst("T", "T")}).ok);
    CHECK_FALSE(augmentWithRowids("SELECT v FROM T GROUP BY v", {inst("T", "T")}).ok);
    CHECK_FALSE(augmentWithRowids("WITH c AS (SELECT 1) SELECT * FROM c", {inst("c", "c")}).ok);
    CHECK_FALSE(augmentWithRowids("SELECT 1", {}).ok);  // no instances
}

TEST_CASE("parseFromInstances lists every FROM reference in order, self-joins included") {
    auto f = parseFromInstances(
        "SELECT ge.GeometryStream, ema.ps5, es.js2, es.js3, rl.* "
        "FROM bis_GeometricElement3d ge "
        "JOIN bis_ElementMultiAspect ema ON ema.ElementId = ge.ElementId "
        "JOIN bis_InformationReferenceElement es ON es.ElementId = ema.ps7 "
        "JOIN bis_InformationReferenceElement rl ON rl.ElementId = es.js1 "
        "WHERE length(GeometryStream) > 200000");
    REQUIRE(f.size() == 4);
    CHECK(f[0].table == "bis_GeometricElement3d");   CHECK(f[0].ref == "ge");
    CHECK(f[1].table == "bis_ElementMultiAspect");   CHECK(f[1].ref == "ema");
    CHECK(f[2].table == "bis_InformationReferenceElement"); CHECK(f[2].ref == "es");
    CHECK(f[3].table == "bis_InformationReferenceElement"); CHECK(f[3].ref == "rl");
}

TEST_CASE("parseFromInstances handles plain, AS, and schema-qualified names") {
    auto a = parseFromInstances("SELECT * FROM T");
    REQUIRE(a.size() == 1);
    CHECK(a[0].table == "T");  CHECK(a[0].ref == "T");

    auto b = parseFromInstances("SELECT x FROM T AS t, U u");
    REQUIRE(b.size() == 2);
    CHECK(b[0].ref == "t");  CHECK(b[1].table == "U");  CHECK(b[1].ref == "u");

    auto c = parseFromInstances("SELECT x FROM main.T x");
    REQUIRE(c.size() == 1);
    CHECK(c[0].table == "T");  CHECK(c[0].ref == "x");
}

TEST_CASE("parseSelectItems classifies columns, stars, and table-stars") {
    auto it = parseSelectItems(
        "SELECT ge.GeometryStream, ema.ps5, es.js2, es.js3, rl.* FROM x");
    REQUIRE(it.size() == 5);
    CHECK(it[0].kind == SelectItem::Simple);     CHECK(it[0].alias == "ge");
    CHECK(it[1].alias == "ema");
    CHECK(it[2].alias == "es");
    CHECK(it[3].alias == "es");
    CHECK(it[4].kind == SelectItem::TableStar);  CHECK(it[4].alias == "rl");

    auto star = parseSelectItems("SELECT * FROM T");
    REQUIRE(star.size() == 1);
    CHECK(star[0].kind == SelectItem::Star);

    // An expression has no alias; a bare column has none either.
    auto ex = parseSelectItems("SELECT length(v), v FROM T");
    REQUIRE(ex.size() == 2);
    CHECK(ex[0].kind == SelectItem::Simple);  CHECK(ex[0].alias.empty());
    CHECK(ex[1].alias.empty());
}
