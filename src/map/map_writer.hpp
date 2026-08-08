#pragma once

#include <cstdint>
#include <string>

#include "map/db_file.hpp"
#include "map/map_model.hpp"

struct sqlite3;
struct sqlite3_stmt;

struct ObjectRow {
    std::int64_t id = 0;
    std::string type;
    std::string name;
    std::string tableName;
    std::int64_t rootPage = 0;
    std::string sql;
    std::int64_t pageCount = 0;
};

// Writes a `sqlinsite map` SQLite database (schema in commands/map.schema.sql).
// Rows are inserted via prepared statements inside one transaction. Call
// commit() when done. Throws std::runtime_error on any SQLite error.
class MapWriter {
public:
    // Bump when the map schema/semantics change incompatibly. The visualizer
    // refuses to open a map whose meta.formatVersion differs (older or newer).
    // v3: dropped cells.keyJson (index keys now decoded on demand from the source).
    // v4: cells holds only table-interior cells; table-leaf rows live in
    //     page_row_runs (+objectId/isLeaf for rowid→leaf); pages from DBSTAT.
    // v5: added index pages_type on pages(pageType) (structural-groups/tree-roots).
    static constexpr int kFormatVersion = 5;

    explicit MapWriter(const std::string& path);
    ~MapWriter();

    MapWriter(const MapWriter&) = delete;
    MapWriter& operator=(const MapWriter&) = delete;

    void writeMeta(const DbHeader& header, const std::string& sourcePath,
                   std::int64_t pageCount);
    void writeObject(const ObjectRow& object);
    void writePage(const PageInfo& page, std::int64_t objectId);  // -1 == none
    void writeRun(std::int64_t startPage, std::int64_t endPage,
                  const std::string& pageType, std::int64_t objectId);
    void writeTypeCount(const std::string& pageType, std::int64_t count);
    // Appends one page_row_runs row: a maximal contiguous rowid run [startRowId,
    // endRowId] of the subtree rooted at `parentPageNumber` (a table b-tree page).
    // Runs are computed in C++ during the parse pass (see map_builder), so the map
    // never has to reconstruct them from a `cells` table.
    void writeRowRun(std::int64_t parentPageNumber, std::int64_t startRowId,
                     std::int64_t endRowId, std::int64_t objectId, bool isLeaf);
    // Sets one page's subtreePageCount (pages in its subtree, following the tree's
    // child/overflow/freelist-leaf edges) — computed in C++ during the parse pass
    // (see map_builder). Call for every page after all pages are written.
    void writeSubtreeCount(std::int64_t pageNumber, std::int64_t count);
    void commit();

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* meta_ = nullptr;
    sqlite3_stmt* object_ = nullptr;
    sqlite3_stmt* page_ = nullptr;
    sqlite3_stmt* cell_ = nullptr;
    sqlite3_stmt* pointer_ = nullptr;
    sqlite3_stmt* ptrmap_ = nullptr;
    sqlite3_stmt* run_ = nullptr;
    sqlite3_stmt* typeCount_ = nullptr;
    sqlite3_stmt* rowRun_ = nullptr;
    sqlite3_stmt* subtree_ = nullptr;
    bool committed_ = false;
};
