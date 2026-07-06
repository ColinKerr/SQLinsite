#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "visualize/profile_reader.hpp"

struct sqlite3;

// Per-object page totals used by the live-query schema panel.
struct MapObjStat {
    std::int64_t pageCount = 0;
    std::int64_t accessedPages = 0;  // distinct pages touched by the loaded profile
};

// Opens a `sqlinsite map` SQLite file read-only and answers the visualize
// query API. Optionally holds an in-memory profile table for overlays.
class MapDb {
public:
    // Throws std::runtime_error if the file is not a valid map.
    explicit MapDb(const std::string& mapPath);
    ~MapDb();

    MapDb(const MapDb&) = delete;
    MapDb& operator=(const MapDb&) = delete;

    // Aggregates a profile CSV into a temp table for overlay queries.
    void loadProfile(const std::string& csvPath);

    // Reported in /api/meta so the front-end can enable the live Query view.
    void setHasDb(bool v) { hasDb_ = v; }

    // Per-object page/accessed counts keyed by object name (for /api/schema).
    std::map<std::string, MapObjStat> objectStats() const;

    // rowid → leaf page for every row of a rowid table (for row→page mapping in
    // the live-query results). One scan of the table's cells; empty if unknown.
    std::unordered_map<std::int64_t, std::int64_t> rowidLeafPages(
        const std::string& tableName) const;

    // Largest page range /api/pages will serialize; beyond this the client must
    // use runs instead.
    static constexpr std::int64_t kPageRangeCap = 2000000;

    // Selected profile leaves (session/statement ids). Empty means "all".
    using LeafFilter = std::vector<int>;

    std::string metaJson() const;
    // Sets tooLarge when (to-from+1) exceeds kPageRangeCap (response is empty).
    std::string pagesJson(std::int64_t from, std::int64_t to, bool& tooLarge) const;
    // Structural runs from the map; when profiled is set and a profile is loaded,
    // runs are recomputed to the contiguous spans accessed by the selected leaves.
    std::string runsJson(std::int64_t from, std::int64_t to, bool profiled,
                         const LeafFilter& sel) const;
    // Pages owned by one object, by 0-based ordinal window [from, to] (Tables view).
    std::string objectPagesJson(std::int64_t objectId, std::int64_t from,
                                std::int64_t to, bool& tooLarge) const;
    // Empty string if the page does not exist.
    std::string pageJson(std::int64_t pageNumber, const LeafFilter& sel) const;
    std::string profilePagesJson(std::int64_t from, std::int64_t to,
                                 const LeafFilter& sel) const;

private:
    sqlite3* db_ = nullptr;
    bool hasProfile_ = false;
    bool hasDb_ = false;
    std::vector<ProfileLeaf> leaves_;  // profile session/statement manifest
};
