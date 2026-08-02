#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map/sqlite_format.hpp"

// Parsed 100-byte database header (https://sqlite.org/fileformat2.html §1.3).
struct DbHeader {
    int pageSize = 0;               // resolved (a stored 1 means 65536)
    int reservedBytesPerPage = 0;
    int writeVersion = 1;           // 1 legacy, 2 wal
    int readVersion = 1;
    std::int64_t pageCountInHeader = 0;
    std::int64_t freelistTrunkPage = 0;
    std::int64_t freelistPageCount = 0;
    std::int64_t schemaCookie = 0;
    int schemaFormat = 0;
    int textEncoding = 1;           // 1 utf8, 2 utf16le, 3 utf16be
    std::int64_t largestRootBtreePage = 0;   // offset 52: nonzero ⇒ auto-vacuum
    int incrementalVacuumFlag = 0;  // offset 64
    std::int64_t userVersion = 0;
    std::int64_t applicationId = 0;
    std::int64_t sqliteVersionNumber = 0;
};

struct sqlite3;
struct sqlite3_stmt;

// Streams a SQLite database file's pages on demand via the sqlite_dbpage vtab.
// Rather than loading the whole file into memory up front (an 8 GB db needs 8 GB
// of RSS and thrashes on smaller machines), it keeps the source connection open
// and fetches one page at a time — SQLite's page cache plus the OS file cache hold
// what's hot, keeping the builder's own footprint tiny.
class DbFile {
public:
    // Throws std::runtime_error if the file cannot be opened or is not a
    // SQLite database.
    static DbFile open(const std::string& path);

    ~DbFile();
    DbFile(DbFile&& other) noexcept;
    DbFile& operator=(DbFile&& other) noexcept;
    DbFile(const DbFile&) = delete;
    DbFile& operator=(const DbFile&) = delete;

    const DbHeader& header() const { return header_; }
    int pageSize() const { return header_.pageSize; }
    int usableSize() const {
        return header_.pageSize - header_.reservedBytesPerPage;
    }
    std::int64_t pageCount() const { return pageCount_; }

    // 1-based page access; throws for invalid numbers or a read error. The
    // returned reference points at an internal buffer that is overwritten by the
    // next page() call — callers must consume it before requesting another page
    // (the map builder parses one page at a time, so this holds). Reusing the
    // buffer avoids a heap allocation per page.
    const std::vector<sqlfmt::Byte>& page(std::int64_t pageNumber) const;

    bool autoVacuum() const { return header_.largestRootBtreePage != 0; }
    bool incrementalVacuum() const {
        return autoVacuum() && header_.incrementalVacuumFlag != 0;
    }

private:
    DbFile() = default;

    DbHeader header_;
    std::int64_t pageCount_ = 0;
    ::sqlite3* db_ = nullptr;
    ::sqlite3_stmt* pageStmt_ = nullptr;  // "SELECT data FROM sqlite_dbpage WHERE pgno=?"
    mutable std::vector<sqlfmt::Byte> pageBuf_;  // reused by page(); see its comment
};
