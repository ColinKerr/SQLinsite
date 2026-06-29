#include "map/db_file.hpp"

#include <stdexcept>

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

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            db, "SELECT pgno, data FROM sqlite_dbpage('main') ORDER BY pgno", -1,
            &stmt, nullptr) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db);
        sqlite3_close(db);
        fail("cannot read pages: " + msg);
    }

    DbFile file;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const std::int64_t pgno = sqlite3_column_int64(stmt, 0);
        const auto* bytes =
            static_cast<const sqlfmt::Byte*>(sqlite3_column_blob(stmt, 1));
        const int len = sqlite3_column_bytes(stmt, 1);
        if (pgno < 1) {
            continue;
        }
        if (static_cast<std::size_t>(pgno) > file.pages_.size()) {
            file.pages_.resize(static_cast<std::size_t>(pgno));
        }
        file.pages_[static_cast<std::size_t>(pgno) - 1].assign(bytes,
                                                               bytes + len);
    }
    const bool ok = (rc == SQLITE_DONE);
    const std::string stepErr = ok ? "" : sqlite3_errmsg(db);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (!ok) {
        fail("error reading pages: " + stepErr);
    }
    if (file.pages_.empty() || file.pages_[0].size() < 100) {
        fail("not a SQLite database (no readable page 1)");
    }

    file.header_ = parseHeader(file.pages_[0]);
    return file;
}

const std::vector<sqlfmt::Byte>& DbFile::page(std::int64_t pageNumber) const {
    if (pageNumber < 1 ||
        static_cast<std::size_t>(pageNumber) > pages_.size()) {
        throw std::out_of_range("page number out of range");
    }
    return pages_[static_cast<std::size_t>(pageNumber) - 1];
}
