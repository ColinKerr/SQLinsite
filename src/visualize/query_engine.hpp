#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "visualize/cell_page_map.hpp"
#include "visualize/map_db.hpp"
#include "visualize/query_augment.hpp"

// Runs user SQL against the mapped database (opened read-only) for the live-query
// view. Each run uses a *fresh* connection routed through the sqlinsite VFS so
// page access is measured with a cold cache. Runs are serialized (the VFS
// profiling context is process-global) and stored in an in-memory history.
class QueryEngine {
public:
    // Largest number of result rows captured/stored per run.
    static constexpr std::int64_t kRowCap = 100000;

    // `map` supplies rowid→page lookups for row→page mapping; it must outlive
    // this engine (owned alongside it by the server).
    QueryEngine(std::string dbPath, int pageSize, const MapDb& map);

    // Runs `sql`, profiling execution. Returns /api/query/run JSON:
    // {queryId, columns, rowCount, truncated, pageCount, accesses, profile:{pages}}
    // or {error}. Stored in history on success.
    std::string runJson(const std::string& sql);

    // Rows [from, to) of a stored run: {columns, rows, rowCount} or {error}.
    std::string rowsJson(int id, std::int64_t from, std::int64_t to) const;

    // EXPLAIN QUERY PLAN + EXPLAIN of `sql`:
    // {queryPlan:{columns,rows}, explain:{columns,rows}} or {error}.
    std::string explainJson(const std::string& sql) const;

    // Past runs, newest first: [{id, sql, pageCount, accesses}].
    std::string historyJson() const;
    // A stored run's metadata (no rows): {id, sql, columns, rowCount, pageCount,
    // accesses, profile:{pages}} or {error}.
    std::string historyEntryJson(int id) const;

private:
    struct Entry {
        int id = 0;
        std::string sql;
        nlohmann::json columns;      // [{name, sourceTable|null, sourceColumn|null}]
        nlohmann::json rows;         // [[value,...],...]
        nlohmann::json rowPages;     // [[[pageNumber,...],...],...] parallel to rows
        std::int64_t rowCount = 0;
        bool truncated = false;
        std::int64_t pageCount = 0;
        std::int64_t accesses = 0;
        nlohmann::json profilePages;
    };

    const Entry* find(int id) const;
    // Fills entry.rowPages: attributes each output column to a FROM instance, re-runs
    // the query with a rowid per instance, and maps each cell to its pages.
    void mapRowPages(Entry& entry);

    std::string dbPath_;
    int pageSize_;
    const MapDb* map_;
    mutable std::mutex mu_;
    std::vector<Entry> history_;
    int nextId_ = 1;
    // Per-column cell→page mapper with a retained read-only connection, opened
    // once (lazily) and reused across runs. Guarded by mu_ like the rest of the
    // engine. `cellMapTried_` avoids re-attempting a failed open on every run.
    std::optional<CellPageMap> cellMap_;
    bool cellMapTried_ = false;
};
