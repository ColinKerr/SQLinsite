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

// Loads a SQLite database file's pages and header via the sqlite_dbpage vtab.
class DbFile {
public:
    // Throws std::runtime_error if the file cannot be opened or is not a
    // SQLite database.
    static DbFile open(const std::string& path);

    const DbHeader& header() const { return header_; }
    int pageSize() const { return header_.pageSize; }
    int usableSize() const {
        return header_.pageSize - header_.reservedBytesPerPage;
    }
    std::int64_t pageCount() const {
        return static_cast<std::int64_t>(pages_.size());
    }

    // 1-based page access; throws std::out_of_range for invalid numbers.
    const std::vector<sqlfmt::Byte>& page(std::int64_t pageNumber) const;

    bool autoVacuum() const { return header_.largestRootBtreePage != 0; }
    bool incrementalVacuum() const {
        return autoVacuum() && header_.incrementalVacuumFlag != 0;
    }

private:
    DbHeader header_;
    std::vector<std::vector<sqlfmt::Byte>> pages_;  // index 0 == page 1
};
