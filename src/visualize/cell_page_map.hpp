#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

// Maps an individual result cell (one column of one table row) to the set of
// database pages that physically store that column's bytes. It uses the SQLite
// record format (https://sqlite.org/fileformat2.html): a table b-tree leaf cell
// holds the local portion of the record, and payload that exceeds the local
// limit spills onto an overflow-page chain. A column whose bytes live entirely
// in the local portion maps to just the leaf page; a large TEXT/BLOB maps to the
// overflow pages that actually hold it.
//
// The mapper retains its own read-only connection to the database file and reads
// individual pages on demand through the `sqlite_dbpage` vtab (outside the
// profiling VFS, so these structural reads never pollute a query's profile).
// Because only the pages backing the displayed rows are read — never the whole
// file — it stays cheap on very large databases. The connection is opened once
// and reused across queries; parsed per-row record layouts are cached so all
// columns of a row share a single parse. Not thread-safe: the owner must
// serialize calls (QueryEngine does, under its run mutex).
class CellPageMap {
public:
    // Opens `dbPath` read-only. Throws std::runtime_error if it cannot be opened
    // or is not a SQLite database.
    static CellPageMap open(const std::string& dbPath);

    ~CellPageMap();
    CellPageMap(CellPageMap&& other) noexcept;
    CellPageMap& operator=(CellPageMap&& other) noexcept;
    CellPageMap(const CellPageMap&) = delete;
    CellPageMap& operator=(const CellPageMap&) = delete;

    // Pages holding storage column `cid` (0-based, in table/record order) of the
    // row whose table-b-tree leaf cell is on `leafPage` with key `rowid`. Returns
    // an ordered, de-duplicated page list (leaf first when touched, then overflow
    // pages in chain order), or an empty vector when the cell cannot be resolved
    // unambiguously (prefer unresolved over wrong).
    std::vector<std::int64_t> pagesForCell(std::int64_t leafPage, std::int64_t rowid,
                                           int cid);

private:
    CellPageMap() = default;

    // Parsed layout of one table-leaf record, in payload-relative byte offsets.
    struct Layout {
        bool ok = false;
        std::uint64_t payloadBytes = 0;         // total record payload P
        std::uint64_t localBytes = 0;           // bytes stored on the leaf page
        std::vector<std::uint64_t> colStart;    // size = nCols + 1; body offsets
        std::vector<std::int64_t> overflow;     // overflow pages in chain order
    };

    // Reads one page's raw bytes via the retained connection; empty on failure or
    // out-of-range page number.
    std::vector<std::uint8_t> readPage(std::int64_t pageNumber) const;
    const Layout& layoutFor(std::int64_t leafPage, std::int64_t rowid);
    Layout parseLayout(std::int64_t leafPage, std::int64_t rowid) const;
    void close() noexcept;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* pageStmt_ = nullptr;
    int pageSize_ = 0;
    int usableSize_ = 0;
    std::map<std::pair<std::int64_t, std::int64_t>, Layout> cache_;
};
