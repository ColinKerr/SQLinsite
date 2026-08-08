#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "visualize/manifest_db.hpp"
#include "test_util.hpp"

namespace {

void putBE32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(v >> 24); b.push_back(v >> 16); b.push_back(v >> 8); b.push_back(v);
}

// A 16-byte block id filled with byte `v`.
std::vector<std::uint8_t> blkId(std::uint8_t v) { return std::vector<std::uint8_t>(16, v); }

// Builds a synthetic v4 manifest:
//   db1 "BASELINE.bim" (parentless, 2 blocks: A, B)
//   db2 "data.bim"     (child of db1, 3 blocks: delta index0->C, index2->D => [C,B,D])
// Block ids are 16 bytes; szBlk=8192 so pagesPerBlock=2 at a 4096 page size.
std::string buildManifest(const std::string& path) {
    const std::uint32_t szBlk = 8192, nName = 16;
    std::vector<std::uint8_t> b;
    // header (24 bytes)
    putBE32(b, 4);        // version
    putBE32(b, szBlk);    // block size
    putBE32(b, 2);        // nDb
    putBE32(b, 0);        // nDelete
    putBE32(b, nName);    // block id size
    putBE32(b, 2);        // maxDbId

    const std::uint32_t arr1 = 24 + 152 * 2;      // db1 array offset
    const std::uint32_t arr2 = arr1 + 2 * nName;  // db2 delta array offset

    auto dbHeader = [&](std::uint32_t id, std::uint32_t parent, std::uint32_t ver,
                        std::uint32_t arrOff, std::uint32_t nBlk, std::uint32_t nEntry,
                        const std::string& name) {
        putBE32(b, id); putBE32(b, parent); putBE32(b, ver); putBE32(b, arrOff);
        putBE32(b, nBlk); putBE32(b, nEntry);
        std::vector<std::uint8_t> nm(128, 0);
        for (std::size_t i = 0; i < name.size() && i < 128; ++i) nm[i] = name[i];
        b.insert(b.end(), nm.begin(), nm.end());
    };
    dbHeader(1, 0, 1, arr1, 2, 0, "BASELINE.bim");
    dbHeader(2, 1, 2, arr2, 3, 2, "data.bim");

    // db1 blocks: A, B
    for (std::uint8_t v : {0xAA, 0xBB}) { auto id = blkId(v); b.insert(b.end(), id.begin(), id.end()); }
    // db2 deltas: (index 0 -> C), (index 2 -> D)
    putBE32(b, 0); { auto id = blkId(0xCC); b.insert(b.end(), id.begin(), id.end()); }
    putBE32(b, 2); { auto id = blkId(0xDD); b.insert(b.end(), id.begin(), id.end()); }

    writeBinaryFile(path, b);
    return path;
}

std::int64_t scalar(const std::string& dbPath, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
    std::int64_t v = -1;
    if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    sqlite3_close(db);
    return v;
}

std::string text(const std::string& dbPath, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
    std::string v;
    if (sqlite3_step(s) == SQLITE_ROW) {
        const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        if (t) v = t;
    }
    sqlite3_finalize(s);
    sqlite3_close(db);
    return v;
}

}  // namespace

TEST_CASE("manifest parses, resolves child deltas, and auto-selects by block count") {
    const std::string path = buildManifest(tmpPath("m.bcv"));
    // pageSize 4096, szBlk 8192 -> pagesPerBlock 2. pageCount 5 -> expected ceil(5/2)=3
    // blocks, which matches db2 (data.bim), not BASELINE.
    ManifestDb m(path, "", /*mapPageCount=*/5, /*pageSize=*/4096);

    CHECK(m.blockSize() == 8192);
    CHECK(m.pagesPerBlock() == 2);
    CHECK(m.selectedDbName() == "data.bim");
    CHECK(m.selectedDbId() == 2);
    CHECK(m.blockCount() == 3);
    CHECK(m.match() == true);

    const std::string db = m.path();
    CHECK(scalar(db, "SELECT manifestMatch FROM meta") == 1);
    CHECK(scalar(db, "SELECT selectedDbId FROM meta") == 2);
    CHECK(scalar(db, "SELECT count(*) FROM databases") == 2);
    CHECK(scalar(db, "SELECT isSelected FROM databases WHERE name='data.bim'") == 1);

    // db2's resolved array is [C, B, D]: index 1 inherited from the parent (shared),
    // indices 0 and 2 overridden by the delta (not shared).
    CHECK(text(db, "SELECT blockId FROM blocks WHERE dbId=2 AND blockIndex=0") ==
          std::string(32, 'c'));
    CHECK(text(db, "SELECT blockId FROM blocks WHERE dbId=2 AND blockIndex=1") ==
          std::string(32, 'b'));
    CHECK(text(db, "SELECT blockId FROM blocks WHERE dbId=2 AND blockIndex=2") ==
          std::string(32, 'd'));
    CHECK(scalar(db, "SELECT sharedWithParent FROM blocks WHERE dbId=2 AND blockIndex=1") == 1);
    CHECK(scalar(db, "SELECT sharedWithParent FROM blocks WHERE dbId=2 AND blockIndex=0") == 0);
    CHECK(scalar(db, "SELECT count(*) FROM blocks WHERE dbId=2") == 3);
}

TEST_CASE("manifest honors an explicit db name and reports mismatch") {
    const std::string path = buildManifest(tmpPath("m2.bcv"));

    // Explicit name overrides auto-selection.
    ManifestDb baseline(path, "BASELINE.bim", 5, 4096);
    CHECK(baseline.selectedDbName() == "BASELINE.bim");
    CHECK(baseline.blockCount() == 2);
    CHECK(baseline.match() == false);  // expects 3 blocks, BASELINE has 2

    // A page count that no db's block count matches -> no auto match.
    ManifestDb bad(path, "", /*mapPageCount=*/100, 4096);  // expected ceil(100/2)=50
    CHECK(bad.match() == false);
    CHECK(!bad.matchReason().empty());
}
