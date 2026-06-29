#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "map/sqlite_format.hpp"

using namespace sqlfmt;
using B = std::vector<std::uint8_t>;

TEST_CASE("big-endian readers") {
    B d = {0x12, 0x34, 0x56, 0x78};
    CHECK(readBE16(d.data()) == 0x1234);
    CHECK(readBE24(d.data()) == 0x123456);
    CHECK(readBE32(d.data()) == 0x12345678u);
}

TEST_CASE("varint decoding") {
    SUBCASE("single byte") {
        B d = {0x7f};
        Varint v = readVarint(d.data(), d.size());
        CHECK(v.value == 0x7f);
        CHECK(v.length == 1);
    }
    SUBCASE("two bytes") {
        B d = {0x81, 0x00};  // 0b1_0000001 0b0_0000000 -> 0x80
        Varint v = readVarint(d.data(), d.size());
        CHECK(v.value == 0x80);
        CHECK(v.length == 2);
    }
    SUBCASE("nine bytes uses all 8 bits of the last") {
        B d = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        Varint v = readVarint(d.data(), d.size());
        CHECK(v.length == 9);
        CHECK(v.value == 0xffffffffffffffffull);
    }
}

TEST_CASE("serial type sizes") {
    CHECK(serialTypeSize(0) == 0);
    CHECK(serialTypeSize(1) == 1);
    CHECK(serialTypeSize(5) == 6);
    CHECK(serialTypeSize(6) == 8);
    CHECK(serialTypeSize(7) == 8);
    CHECK(serialTypeSize(8) == 0);
    CHECK(serialTypeSize(9) == 0);
    CHECK(serialTypeSize(13) == 0);   // text, (13-13)/2
    CHECK(serialTypeSize(14) == 1);   // blob, (14-12)/2
    CHECK(serialTypeSize(23) == 5);   // text, (23-13)/2
}

TEST_CASE("value decoding") {
    SUBCASE("8-bit signed int") {
        B d = {0xff};
        CellValue v = decodeValue(1, d.data(), d.size());
        CHECK(v.type == CellValue::Type::Int);
        CHECK(v.intValue == -1);
    }
    SUBCASE("constants 8 and 9") {
        CHECK(decodeValue(8, nullptr, 0).intValue == 0);
        CHECK(decodeValue(9, nullptr, 0).intValue == 1);
    }
    SUBCASE("text") {
        B d = {'h', 'i'};
        CellValue v = decodeValue(17, d.data(), d.size());  // (17-13)/2 = 2
        CHECK(v.type == CellValue::Type::Text);
        CHECK(v.text == "hi");
        CHECK(v.byteSize == 2);
        CHECK(v.truncated == false);
    }
    SUBCASE("truncated text marks truncation") {
        B d = {'h'};
        CellValue v = decodeValue(19, d.data(), d.size());  // declares 3 bytes
        CHECK(v.truncated == true);
    }
}

TEST_CASE("record header parsing") {
    // header size = 3, then two serial types: 1 (int8) and 0 (null)
    B d = {0x03, 0x01, 0x00};
    RecordHeader h = parseRecordHeader(d.data(), d.size());
    CHECK(h.headerSize == 3);
    REQUIRE(h.serialTypes.size() == 2);
    CHECK(h.serialTypes[0] == 1);
    CHECK(h.serialTypes[1] == 0);
}

TEST_CASE("btree header parsing") {
    B page(4096, 0);
    page[0] = 13;          // table leaf
    page[3] = 0x00; page[4] = 0x02;  // 2 cells
    page[5] = 0x0f; page[6] = 0x00;  // content start 3840
    page[7] = 1;           // fragmented free bytes
    BtreeHeader h = parseBtreeHeader(page.data(), 0);
    CHECK(h.type == 13);
    CHECK(h.cellCount == 2);
    CHECK(h.cellContentStart == 0x0f00);
    CHECK(h.fragmentedFreeBytes == 1);
    CHECK(h.headerSize == 8);
    CHECK(h.rightmostPointer == -1);
}

TEST_CASE("interior btree header has rightmost pointer") {
    B page(4096, 0);
    page[0] = 5;  // table interior
    page[8] = 0x00; page[9] = 0x00; page[10] = 0x00; page[11] = 0x07;
    BtreeHeader h = parseBtreeHeader(page.data(), 0);
    CHECK(h.headerSize == 12);
    CHECK(h.rightmostPointer == 7);
}

TEST_CASE("local payload split") {
    SUBCASE("fits locally") {
        PayloadSplit s = localPayload(100, 4096, true);
        CHECK(s.localBytes == 100);
        CHECK(s.hasOverflow == false);
    }
    SUBCASE("table leaf overflows") {
        PayloadSplit s = localPayload(10000, 4096, true);
        CHECK(s.hasOverflow == true);
        CHECK(s.localBytes < 10000);
        CHECK(s.localBytes <= 4096u - 35);
    }
}
