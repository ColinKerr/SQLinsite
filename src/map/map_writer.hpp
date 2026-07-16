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
    static constexpr int kFormatVersion = 2;

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
    // Precomputes page_row_runs from the already-written cells/pointers/pages
    // (one INSERT…SELECT). Call after all pages are written, before commit().
    void writeRowRuns(std::int64_t pageCount);
    // Fills pages.subtreePageCount (pages in each page's subtree, following the
    // tree's child/overflow/freelist-leaf edges). Call after all pages/pointers
    // are written, before commit().
    void writeSubtreeCounts(std::int64_t pageCount);
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
    bool committed_ = false;
};
