#include "map/db_file.hpp"

#include <stdexcept>
#include <utility>

#include <sqlite3.h>

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("map: " + message);
}

DbHeader parseHeader(const std::vector<sqlfmt::Byte>& page1) {
    using sqlfmt::readBE16;
    using sqlfmt::readBE32;
    const sqlfmt::Byte* p = page1.data();

    DbHeader h;
    const std::uint32_t storedPageSize = readBE16(p + 16);
    h.pageSize = storedPageSize == 1 ? 65536 : static_cast<int>(storedPageSize);
    h.writeVersion = p[18];
    h.readVersion = p[19];
    h.reservedBytesPerPage = p[20];
    h.pageCountInHeader = readBE32(p + 28);
    h.freelistTrunkPage = readBE32(p + 32);
    h.freelistPageCount = readBE32(p + 36);
    h.schemaCookie = readBE32(p + 40);
    h.schemaFormat = static_cast<int>(readBE32(p + 44));
    h.largestRootBtreePage = readBE32(p + 52);
    h.textEncoding = static_cast<int>(readBE32(p + 56));
    h.userVersion = readBE32(p + 60);
    h.incrementalVacuumFlag = static_cast<int>(readBE32(p + 64));
    h.applicationId = readBE32(p + 68);
    h.sqliteVersionNumber = readBE32(p + 96);
    return h;
}

}  // namespace

DbFile DbFile::open(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db);
        sqlite3_close(db);
        fail("cannot open " + path + ": " + msg);
    }

    DbFile file;
    file.db_ = db;
    if (sqlite3_prepare_v2(
            db, "SELECT data FROM sqlite_dbpage('main') WHERE pgno=?1", -1,
            &file.pageStmt_, nullptr) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db);
        fail("cannot read pages: " + msg);  // ~DbFile closes db_
    }

    // Page count from the vtab (authoritative even if the header count is stale).
    sqlite3_stmt* cnt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT max(pgno) FROM sqlite_dbpage('main')", -1,
                           &cnt, nullptr) == SQLITE_OK &&
        sqlite3_step(cnt) == SQLITE_ROW) {
        file.pageCount_ = sqlite3_column_int64(cnt, 0);
    }
    sqlite3_finalize(cnt);
    if (file.pageCount_ < 1) fail("not a SQLite database (no pages)");

    const std::vector<sqlfmt::Byte> page1 = file.page(1);
    if (page1.size() < 100) fail("not a SQLite database (no readable page 1)");
    file.header_ = parseHeader(page1);
    return file;
}

const std::vector<sqlfmt::Byte>& DbFile::page(std::int64_t pageNumber) const {
    if (pageNumber < 1 || pageNumber > pageCount_) {
        throw std::out_of_range("page number out of range");
    }
    sqlite3_reset(pageStmt_);
    sqlite3_bind_int64(pageStmt_, 1, pageNumber);
    if (sqlite3_step(pageStmt_) != SQLITE_ROW) {
        sqlite3_reset(pageStmt_);
        throw std::runtime_error("map: cannot read page " +
                                 std::to_string(pageNumber));
    }
    const auto* bytes =
        static_cast<const sqlfmt::Byte*>(sqlite3_column_blob(pageStmt_, 0));
    const int len = sqlite3_column_bytes(pageStmt_, 0);
    pageBuf_.assign(bytes, bytes + len);  // reuses capacity — no per-page alloc
    sqlite3_reset(pageStmt_);
    return pageBuf_;
}

DbFile::~DbFile() {
    if (pageStmt_) sqlite3_finalize(pageStmt_);
    if (db_) sqlite3_close(db_);
}

DbFile::DbFile(DbFile&& other) noexcept
    : header_(other.header_), pageCount_(other.pageCount_), db_(other.db_),
      pageStmt_(other.pageStmt_) {
    other.db_ = nullptr;
    other.pageStmt_ = nullptr;
}

DbFile& DbFile::operator=(DbFile&& other) noexcept {
    if (this != &other) {
        if (pageStmt_) sqlite3_finalize(pageStmt_);
        if (db_) sqlite3_close(db_);
        header_ = other.header_;
        pageCount_ = other.pageCount_;
        db_ = other.db_;
        pageStmt_ = other.pageStmt_;
        other.db_ = nullptr;
        other.pageStmt_ = nullptr;
    }
    return *this;
}
