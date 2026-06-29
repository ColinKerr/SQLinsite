#pragma once

#include <cstdint>
#include <string>

struct sqlite3;

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

    // Largest page range /api/pages will serialize; beyond this the client must
    // use runs instead.
    static constexpr std::int64_t kPageRangeCap = 2000000;

    std::string metaJson() const;
    // Sets tooLarge when (to-from+1) exceeds kPageRangeCap (response is empty).
    std::string pagesJson(std::int64_t from, std::int64_t to, bool& tooLarge) const;
    std::string runsJson(std::int64_t from, std::int64_t to) const;
    // Pages owned by one object, by 0-based ordinal window [from, to] (Tables view).
    std::string objectPagesJson(std::int64_t objectId, std::int64_t from,
                                std::int64_t to, bool& tooLarge) const;
    // Empty string if the page does not exist.
    std::string pageJson(std::int64_t pageNumber) const;
    std::string profilePagesJson(std::int64_t from, std::int64_t to) const;
    std::string profileHistogramJson(std::int64_t from, std::int64_t to,
                                     int bins) const;

private:
    sqlite3* db_ = nullptr;
    bool hasProfile_ = false;
};
