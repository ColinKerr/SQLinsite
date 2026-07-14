#include "visualize/query_engine.hpp"

#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include <optional>

#include "profile/aggregating_sink.hpp"
#include "profile/profiling_context.hpp"
#include "profile/vfs_shim.hpp"
#include "visualize/cell_page_map.hpp"
#include "visualize/query_augment.hpp"

using nlohmann::json;

namespace {

json valueJson(sqlite3_stmt* s, int c) {
    switch (sqlite3_column_type(s, c)) {
        case SQLITE_NULL: return nullptr;
        case SQLITE_INTEGER: return sqlite3_column_int64(s, c);
        case SQLITE_FLOAT: return sqlite3_column_double(s, c);
        case SQLITE_BLOB:
            return "BLOB(" + std::to_string(sqlite3_column_bytes(s, c)) + " bytes)";
        default: {
            const unsigned char* t = sqlite3_column_text(s, c);
            return t ? json(reinterpret_cast<const char*>(t)) : json(nullptr);
        }
    }
}

json columnNames(sqlite3_stmt* s) {
    json cols = json::array();
    const int n = sqlite3_column_count(s);
    for (int c = 0; c < n; ++c) cols.push_back(sqlite3_column_name(s, c));
    return cols;
}

// Runs a read-only statement to completion, returning {columns, rows}. Throws on
// prepare/step failure (message = sqlite3_errmsg).
json collectResult(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    const int n = sqlite3_column_count(stmt);
    json cols = columnNames(stmt);
    json rows = json::array();
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json r = json::array();
        for (int c = 0; c < n; ++c) r.push_back(valueJson(stmt, c));
        rows.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    return json{{"columns", std::move(cols)}, {"rows", std::move(rows)}};
}

json profilePagesJson(const AggregatingSink& sink) {
    json pages = json::array();
    for (const AggregatingSink::Page& p : sink.pages()) {
        pages.push_back({{"pageNumber", p.pageNumber}, {"reads", p.reads}, {"writes", p.writes}});
    }
    return pages;
}

// Per-column provenance read from a prepared statement (SQLITE_ENABLE_COLUMN_
// METADATA). `tables` holds the distinct source tables in first-seen order;
// `colTable[c]` indexes into it (or -1 when the column has no single source).
struct ColumnMeta {
    json columns = json::array();
    std::vector<std::string> tables;
    std::vector<int> colTable;
};

ColumnMeta readColumns(sqlite3_stmt* s) {
    ColumnMeta m;
    std::map<std::string, int> index;
    const int n = sqlite3_column_count(s);
    for (int c = 0; c < n; ++c) {
        const char* name = sqlite3_column_name(s, c);
        const char* tbl = sqlite3_column_table_name(s, c);
        const char* org = sqlite3_column_origin_name(s, c);
        m.columns.push_back({
            {"name", name ? name : ""},
            {"sourceTable", tbl ? json(tbl) : json(nullptr)},
            {"sourceColumn", org ? json(org) : json(nullptr)},
        });
        if (tbl) {
            auto it = index.find(tbl);
            if (it == index.end()) {
                const int idx = static_cast<int>(m.tables.size());
                m.tables.emplace_back(tbl);
                index.emplace(tbl, idx);
                m.colTable.push_back(idx);
            } else {
                m.colTable.push_back(it->second);
            }
        } else {
            m.colTable.push_back(-1);
        }
    }
    return m;
}

}  // namespace

QueryEngine::QueryEngine(std::string dbPath, int pageSize, const MapDb& map)
    : dbPath_(std::move(dbPath)), pageSize_(pageSize), map_(&map) {}

// Reads a table's column-name → storage index (cid) map via PRAGMA table_info.
// The cid order matches the record's serial-type order, so it indexes each cell's
// bytes within the record (see CellPageMap).
static std::unordered_map<std::string, int> tableCids(sqlite3* db, const std::string& table) {
    std::unordered_map<std::string, int> cids;
    char* q = sqlite3_mprintf("PRAGMA table_info(%Q)", table.c_str());
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, q, -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const int cid = sqlite3_column_int(st, 0);
            const unsigned char* name = sqlite3_column_text(st, 1);
            if (name) cids.emplace(reinterpret_cast<const char*>(name), cid);
        }
    }
    sqlite3_finalize(st);
    sqlite3_free(q);
    return cids;
}

// Second pass (unprofiled): re-run the query with a `"table".rowid` column
// prepended per source table, verify each row's user columns match the original
// results, and fill entry.rowPages with the pages holding each mapped cell's
// bytes (leaf plus any overflow pages, computed per-column by CellPageMap). Each
// cell is an array of page numbers; unresolvable cells stay an empty array
// (prefer unresolved over wrong).
void QueryEngine::mapRowPages(Entry& entry, const std::vector<std::string>& tables,
                             const std::vector<int>& colTable) {
    const std::size_t userN = colTable.size();
    // Pre-fill with empty arrays so unresolved cells carry no pages.
    entry.rowPages = json::array();
    for (std::size_t r = 0; r < entry.rows.size(); ++r) {
        entry.rowPages.push_back(json(std::vector<json>(userN, json::array())));
    }
    if (tables.empty()) return;

    const Augmentation aug = augmentWithRowids(entry.sql, tables);
    if (!aug.ok) return;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }

    // Per-column precise mapper with a retained connection, opened once and reused
    // across runs (reads pages on demand, outside the profiling VFS). If it can't
    // be opened we degrade gracefully to leaf-only pages below.
    if (!cellMapTried_) {
        cellMapTried_ = true;
        try {
            cellMap_.emplace(CellPageMap::open(dbPath_));
        } catch (const std::exception&) {
        }
    }
    std::optional<CellPageMap>& cellMap = cellMap_;

    sqlite3_stmt* stmt = nullptr;
    const int D = static_cast<int>(tables.size());
    if (sqlite3_prepare_v2(db, aug.sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_column_count(stmt) == D + static_cast<int>(userN)) {
        // rowid → leaf page, resolved on demand (indexed) and memoized per table,
        // so only the rowids actually displayed are mapped — no whole-table scan.
        std::vector<std::unordered_map<std::int64_t, std::int64_t>> pageMaps(tables.size());
        std::vector<std::unordered_map<std::string, int>> cidMaps;
        for (const std::string& t : tables) cidMaps.push_back(tableCids(db, t));

        std::int64_t ri = 0;
        while (ri < static_cast<std::int64_t>(entry.rows.size()) &&
               sqlite3_step(stmt) == SQLITE_ROW) {
            // Verify user columns match the original row before trusting rowids.
            bool match = true;
            for (std::size_t c = 0; c < userN && match; ++c) {
                if (valueJson(stmt, D + static_cast<int>(c)) != entry.rows[ri][c]) match = false;
            }
            if (match) {
                for (std::size_t c = 0; c < userN; ++c) {
                    const int ti = colTable[c];
                    if (ti < 0) continue;
                    if (sqlite3_column_type(stmt, ti) == SQLITE_NULL) continue;
                    const std::int64_t rid = sqlite3_column_int64(stmt, ti);
                    auto it = pageMaps[ti].find(rid);
                    if (it == pageMaps[ti].end())
                        it = pageMaps[ti].emplace(rid, map_->leafPageForRowid(tables[ti], rid)).first;
                    const std::int64_t leaf = it->second;
                    if (leaf == 0) continue;  // rowid not found in the map

                    // Resolve the column's storage index, then map its bytes to
                    // pages. Fall back to the leaf page when precise mapping is
                    // unavailable (never worse than leaf-only).
                    std::vector<std::int64_t> pages;
                    int cid = -1;
                    const auto& src = entry.columns[c]["sourceColumn"];
                    if (cellMap && src.is_string()) {
                        auto cit = cidMaps[ti].find(src.get<std::string>());
                        if (cit != cidMaps[ti].end()) cid = cit->second;
                    }
                    if (cellMap && cid >= 0) pages = cellMap->pagesForCell(leaf, rid, cid);
                    if (pages.empty()) pages = {leaf};
                    entry.rowPages[ri][c] = pages;
                }
            }
            ++ri;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

std::string QueryEngine::runJson(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mu_);
    if (registerSQLINSITEVfs() != SQLITE_OK) {
        return R"({"error":"failed to register profiling VFS"})";
    }
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath_.c_str(), &db, SQLITE_OPEN_READONLY, kSQLINSITEVfsName) != SQLITE_OK) {
        json e = {{"error", std::string("cannot open db: ") + sqlite3_errmsg(db)}};
        sqlite3_close(db);
        return e.dump();
    }

    ProfilingContext& ctx = profilingContext();
    ctx.sessionName = "query";
    ctx.statementIndex = 0;
    ctx.pageSize = pageSize_;
    ctx.relativeTiming = false;
    ctx.timeBaseline = 0;

    AggregatingSink sink;
    ctx.out = &sink;  // measure prepare (cold schema reads) + execution

    Entry entry;
    entry.sql = sql;
    std::string error;
    ColumnMeta meta;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
    } else {
        const int n = sqlite3_column_count(stmt);
        meta = readColumns(stmt);
        entry.columns = meta.columns;
        entry.rows = json::array();
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            ++entry.rowCount;
            if (entry.rowCount <= kRowCap) {
                json r = json::array();
                for (int c = 0; c < n; ++c) r.push_back(valueJson(stmt, c));
                entry.rows.push_back(std::move(r));
            } else {
                entry.truncated = true;
            }
        }
        if (rc != SQLITE_DONE) error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
    }

    ctx.out = nullptr;  // stop measuring
    sqlite3_close(db);

    if (!error.empty()) return json{{"error", error}}.dump();

    mapRowPages(entry, meta.tables, meta.colTable);
    entry.id = nextId_++;
    entry.pageCount = sink.distinctPages();
    entry.accesses = sink.accesses();
    entry.profilePages = profilePagesJson(sink);
    json summary = {
        {"queryId", entry.id},
        {"columns", entry.columns},
        {"rowCount", entry.rowCount},
        {"truncated", entry.truncated},
        {"pageCount", entry.pageCount},
        {"accesses", entry.accesses},
        {"profile", {{"pages", entry.profilePages}}},
    };
    history_.push_back(std::move(entry));
    return summary.dump();
}

const QueryEngine::Entry* QueryEngine::find(int id) const {
    for (const Entry& e : history_) if (e.id == id) return &e;
    return nullptr;
}

std::string QueryEngine::rowsJson(int id, std::int64_t from, std::int64_t to) const {
    std::lock_guard<std::mutex> lock(mu_);
    const Entry* e = find(id);
    if (e == nullptr) return R"({"error":"no such query"})";
    from = std::max<std::int64_t>(0, from);
    to = std::min<std::int64_t>(to, static_cast<std::int64_t>(e->rows.size()));
    json rows = json::array();
    json pages = json::array();  // per-cell page-number arrays parallel to rows
    for (std::int64_t i = from; i < to; ++i) {
        rows.push_back(e->rows[i]);
        pages.push_back(e->rowPages[i]);
    }
    return json{{"columns", e->columns}, {"rows", std::move(rows)},
                {"rowPages", std::move(pages)}, {"rowCount", e->rowCount}}.dump();
}

std::string QueryEngine::explainJson(const std::string& sql) const {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        json e = {{"error", std::string("cannot open db: ") + sqlite3_errmsg(db)}};
        sqlite3_close(db);
        return e.dump();
    }
    json out;
    try {
        out["queryPlan"] = collectResult(db, "EXPLAIN QUERY PLAN " + sql);
        out["explain"] = collectResult(db, "EXPLAIN " + sql);
    } catch (const std::exception& ex) {
        out = json{{"error", ex.what()}};
    }
    sqlite3_close(db);
    return out.dump();
}

std::string QueryEngine::historyJson() const {
    std::lock_guard<std::mutex> lock(mu_);
    json arr = json::array();
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        arr.push_back({{"id", it->id}, {"sql", it->sql},
                       {"pageCount", it->pageCount}, {"accesses", it->accesses}});
    }
    return json{{"history", std::move(arr)}}.dump();
}

std::string QueryEngine::historyEntryJson(int id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const Entry* e = find(id);
    if (e == nullptr) return R"({"error":"no such query"})";
    return json{
        {"id", e->id}, {"sql", e->sql}, {"columns", e->columns},
        {"rowCount", e->rowCount}, {"truncated", e->truncated},
        {"pageCount", e->pageCount}, {"accesses", e->accesses},
        {"profile", {{"pages", e->profilePages}}},
    }.dump();
}

std::string QueryEngine::schemaJson(const std::map<std::string, MapObjStat>& counts) const {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        json e = {{"error", std::string("cannot open db: ") + sqlite3_errmsg(db)}};
        sqlite3_close(db);
        return e.dump();
    }

    auto stat = [&](const std::string& name) -> MapObjStat {
        auto it = counts.find(name);
        return it == counts.end() ? MapObjStat{} : it->second;
    };

    auto pragmaNames = [&](const char* pragma, const std::string& arg, int col) {
        json names = json::array();
        char* q = sqlite3_mprintf("PRAGMA %s(%Q)", pragma, arg.c_str());
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q, -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* t = sqlite3_column_text(st, col);
                if (t) names.push_back(reinterpret_cast<const char*>(t));
            }
        }
        sqlite3_finalize(st);
        sqlite3_free(q);
        return names;
    };

    auto columns = [&](const std::string& name) {
        json cols = json::array();
        char* q = sqlite3_mprintf("PRAGMA table_info(%Q)", name.c_str());
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q, -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                cols.push_back({
                    {"name", reinterpret_cast<const char*>(sqlite3_column_text(st, 1))},
                    {"type", sqlite3_column_text(st, 2)
                                 ? json(reinterpret_cast<const char*>(sqlite3_column_text(st, 2)))
                                 : json("")},
                });
            }
        }
        sqlite3_finalize(st);
        sqlite3_free(q);
        return cols;
    };

    auto triggers = [&](const std::string& name) {
        json trg = json::array();
        char* q = sqlite3_mprintf(
            "SELECT name FROM sqlite_schema WHERE type='trigger' AND tbl_name=%Q ORDER BY name",
            name.c_str());
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q, -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                trg.push_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
            }
        }
        sqlite3_finalize(st);
        sqlite3_free(q);
        return trg;
    };

    auto namesOfType = [&](const char* type) {
        std::vector<std::string> out;
        char* q = sqlite3_mprintf(
            "SELECT name FROM sqlite_schema WHERE type=%Q AND name NOT LIKE 'sqlite_%%' ORDER BY name",
            type);
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, q, -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                out.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)));
            }
        }
        sqlite3_finalize(st);
        sqlite3_free(q);
        return out;
    };

    json tables = json::array();
    for (const std::string& name : namesOfType("table")) {
        const MapObjStat s = stat(name);
        json idxs = json::array();
        for (const auto& idxName : pragmaNames("index_list", name, 1)) {
            const MapObjStat is = stat(idxName.get<std::string>());
            idxs.push_back({{"name", idxName}, {"pageCount", is.pageCount},
                            {"accessedPages", is.accessedPages}});
        }
        tables.push_back({
            {"name", name}, {"pageCount", s.pageCount}, {"accessedPages", s.accessedPages},
            {"columns", columns(name)}, {"indexes", std::move(idxs)}, {"triggers", triggers(name)},
        });
    }

    json views = json::array();
    for (const std::string& name : namesOfType("view")) {
        views.push_back({{"name", name}, {"columns", columns(name)}, {"triggers", triggers(name)}});
    }

    sqlite3_close(db);
    return json{{"tables", std::move(tables)}, {"views", std::move(views)}}.dump();
}
