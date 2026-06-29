#include <doctest/doctest.h>

#include "profile/page_index.hpp"

TEST_CASE("page number derivation") {
    SUBCASE("header read at offset 0 maps to page 1 (SQLite's first page)") {
        CHECK(pageNumberFor(0, 4096) == 1);
    }
    SUBCASE("offsets within a page map to that page (1-based)") {
        CHECK(pageNumberFor(4095, 4096) == 1);
        CHECK(pageNumberFor(4096, 4096) == 2);
        CHECK(pageNumberFor(8192, 4096) == 3);
    }
    SUBCASE("unknown page size yields -1") {
        CHECK(pageNumberFor(12345, 0) == -1);
    }
    SUBCASE("65536 page size (header encoding of 1) works") {
        CHECK(pageNumberFor(65536, 65536) == 2);
    }
}
