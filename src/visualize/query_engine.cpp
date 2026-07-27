#include "visualize/query_engine.hpp"

#include <limits>
#include <unordered_map>
#include <unordered_set>
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

// Per-column provenance from a prepared statement (SQLITE_ENABLE_COLUMN_METADATA):
// [{name, sourceTable|null, sourceColumn|null}]. sourceTable is the underlying table
// name (never the FROM-clause alias — see attributeColumns for alias handling).
json readColumns(sqlite3_stmt* s) {
    json cols = json::array();
    const int n = sqlite3_column_count(s);
    for (int c = 0; c < n; ++c) {
        const char* name = sqlite3_column_name(s, c);
        const char* tbl = sqlite3_column_table_name(s, c);
        const char* org = sqlite3_column_origin_name(s, c);
        cols.push_back({
            {"name", name ? name : ""},
            {"sourceTable", tbl ? json(tbl) : json(nullptr)},
            {"sourceColumn", org ? json(org) : json(nullptr)},
        });
    }
    return cols;
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

static bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// The distinct FROM instances a query's columns map to, plus each output column's
// instance index (-1 = unmapped). One rowid column is later prepended per used
// instance, so a self-joined table yields two independent instances (es, rl).
struct Attribution {
    std::vector<FromInstance> used;
    std::vector<int> colInstance;
};

// Attributes each output column to a FROM instance. A column whose table appears
// exactly once in the FROM clause is attributed by that table alone (authoritative,
// no SQL parsing). Only columns of a *self-joined* table need the SELECT-list parse
// to pick the alias, and those are trusted only when the item widths line up exactly
// (star widths taken from PRAGMA table_info) — otherwise they stay unmapped rather
// than risk a wrong page. Columns from a source the FROM parse missed fall back to
// referencing the table by name (best effort).
static Attribution attributeColumns(sqlite3* db, const std::string& sql,
                                    const json& columns) {
    const int N = static_cast<int>(columns.size());
    Attribution attr;
    attr.colInstance.assign(N, -1);

    const std::vector<FromInstance> insts = parseFromInstances(sql);
    if (insts.empty()) return attr;
    const std::vector<SelectItem> items = parseSelectItems(sql);

    auto findRef = [&](const std::string& r) -> int {
        for (std::size_t k = 0; k < insts.size(); ++k)
            if (ieq(insts[k].ref, r)) return static_cast<int>(k);
        return -1;
    };

    // Walk the SELECT list, assigning each output column its instance where the
    // item makes it explicit; `aligned` holds only while every item's output width
    // is known exactly (so a self-join attribution can be trusted).
    std::vector<int> byItem(N, -1);
    int oc = 0;
    bool aligned = true;
    auto expand = [&](int k) {
        if (!aligned) return;
        if (k < 0 || insts[static_cast<std::size_t>(k)].table.empty()) { aligned = false; return; }
        const int cnt = static_cast<int>(tableCids(db, insts[static_cast<std::size_t>(k)].table).size());
        if (cnt <= 0) { aligned = false; return; }
        for (int j = 0; j < cnt; ++j) {
            if (oc < N) byItem[oc++] = k;
            else { aligned = false; return; }
        }
    };
    for (const SelectItem& it : items) {
        if (!aligned) break;
        if (it.kind == SelectItem::Star) {
            for (std::size_t k = 0; k < insts.size() && aligned; ++k) expand(static_cast<int>(k));
        } else if (it.kind == SelectItem::TableStar) {
            expand(findRef(it.alias));
        } else {
            if (oc < N) byItem[oc++] = it.alias.empty() ? -1 : findRef(it.alias);
            else aligned = false;
        }
    }
    if (oc != N) aligned = false;

    std::vector<FromInstance> all = insts;  // may append synthetic name-only instances
    std::vector<int> chosen(N, -1);
    for (int c = 0; c < N; ++c) {
        if (!columns[c]["sourceTable"].is_string()) continue;  // expression / no source
        const std::string tbl = columns[c]["sourceTable"].get<std::string>();
        int match = -1, matches = 0;
        for (std::size_t k = 0; k < insts.size(); ++k)
            if (!insts[k].table.empty() && ieq(insts[k].table, tbl)) { match = static_cast<int>(k); ++matches; }
        if (matches == 1) {
            chosen[c] = match;
        } else if (matches >= 2) {  // self-join: trust the aligned SELECT-list attribution
            if (aligned && byItem[c] >= 0 && ieq(insts[static_cast<std::size_t>(byItem[c])].table, tbl))
                chosen[c] = byItem[c];
        } else {  // table not seen in FROM parse — reference it by name
            int syn = -1;
            for (std::size_t k = 0; k < all.size(); ++k)
                if (all[k].table == tbl && all[k].ref == tbl) { syn = static_cast<int>(k); break; }
            if (syn < 0) { syn = static_cast<int>(all.size()); all.push_back({tbl, tbl}); }
            chosen[c] = syn;
        }
    }

    std::vector<int> remap(all.size(), -1);
    for (int c = 0; c < N; ++c) {
        const int k = chosen[c];
        if (k < 0) continue;
        if (remap[static_cast<std::size_t>(k)] < 0) {
            remap[static_cast<std::size_t>(k)] = static_cast<int>(attr.used.size());
            attr.used.push_back(all[static_cast<std::size_t>(k)]);
        }
        attr.colInstance[c] = remap[static_cast<std::size_t>(k)];
    }
    return attr;
}

// Second pass (unprofiled): re-run the query with a `<ref>.rowid` column
// prepended per source table, verify each row's user columns match the original
// results, and fill entry.rowPages with the pages holding each mapped cell's
// bytes (leaf plus any overflow pages, computed per-column by CellPageMap). Each
// cell is an array of page numbers; unresolvable cells stay an empty array
// (prefer unresolved over wrong).
void QueryEngine::mapRowPages(Entry& entry, const std::vector<FromInstance>& used,
                             const std::vector<int>& colInstance,
                             const std::vector<int>& colCid,
                             const std::vector<std::vector<std::int64_t>>& rowids) {
    const std::size_t userN = entry.columns.size();
    // Pre-fill with empty arrays so unresolved cells carry no pages.
    entry.rowPages = json::array();
    for (std::size_t r = 0; r < entry.rows.size(); ++r) {
        entry.rowPages.push_back(json(std::vector<json>(userN, json::array())));
    }
    if (used.empty() || rowids.empty()) return;

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

    const std::size_t D = used.size();
    constexpr std::int64_t kNone = std::numeric_limits<std::int64_t>::min();

    // Pass 1: from the rowids captured during the single execution, record each cell
    // that needs a page and collect the distinct rowids per instance.
    struct Need { std::size_t ri; std::size_t c; int ti; std::int64_t rid; };
    std::vector<Need> needs;
    std::vector<std::vector<std::int64_t>> wanted(D);
    std::vector<std::unordered_set<std::int64_t>> seen(D);
    for (std::size_t ri = 0; ri < rowids.size() && ri < entry.rows.size(); ++ri) {
        for (std::size_t c = 0; c < userN; ++c) {
            const int ti = colInstance[c];
            if (ti < 0 || static_cast<std::size_t>(ti) >= D) continue;
            const std::int64_t rid = rowids[ri][static_cast<std::size_t>(ti)];
            if (rid == kNone) continue;
            needs.push_back({ri, c, ti, rid});
            if (seen[ti].insert(rid).second) wanted[ti].push_back(rid);
        }
    }

    // Pass 2: batch-resolve rowid → leaf page per instance (one query each) — a
    // point lookup per rowid, so this stays fast for wide tables (many pages).
    std::vector<std::unordered_map<std::int64_t, std::int64_t>> pageMaps(D);
    for (std::size_t ti = 0; ti < D; ++ti)
        pageMaps[ti] = map_->leafPagesForRowids(used[ti].table, wanted[ti]);

    // Pass 3: map each cell's bytes to pages — precise via CellPageMap, else the
    // leaf page (never worse than leaf-only). Unresolved rowids stay empty.
    for (const Need& nd : needs) {
        auto it = pageMaps[static_cast<std::size_t>(nd.ti)].find(nd.rid);
        if (it == pageMaps[static_cast<std::size_t>(nd.ti)].end() || it->second == 0) continue;
        const std::int64_t leaf = it->second;
        std::vector<std::int64_t> pages;
        if (cellMap && colCid[nd.c] >= 0) pages = cellMap->pagesForCell(leaf, nd.rid, colCid[nd.c]);
        if (pages.empty()) pages = {leaf};
        entry.rowPages[nd.ri][nd.c] = pages;
    }
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

    // Row→page mapping is done in a SINGLE execution: we first prepare the plain
    // query only to read column metadata (no stepping), attribute each column to a
    // FROM instance, and build an augmented query with a rowid column prepended per
    // instance. That augmented query is then the one actually executed and profiled,
    // so its rows ARE the displayed results and the rowids come for free — no second
    // execution of a potentially expensive query. If augmentation isn't applicable
    // we execute the plain query and simply skip mapping.
    std::vector<FromInstance> used;     // instances a rowid column is collected for
    std::vector<int> colInstance;       // user column → index into `used` (-1 = none)
    std::vector<int> colCid;            // user column → record-field index (-1 = leaf-only)
    std::string runSql = sql;           // the statement actually executed
    int D = 0;                          // prepended rowid columns (0 = no mapping)

    sqlite3_stmt* meta = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &meta, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
    } else {
        entry.columns = readColumns(meta);
        sqlite3_finalize(meta);
        const std::size_t userN = entry.columns.size();

        const Attribution attr = attributeColumns(db, sql, entry.columns);
        const Augmentation aug =
            attr.used.empty() ? Augmentation{} : augmentWithRowids(sql, attr.used);
        if (aug.ok) {
            std::vector<std::unordered_map<std::string, int>> cidMaps;
            for (const FromInstance& inst : attr.used) cidMaps.push_back(tableCids(db, inst.table));
            colCid.assign(userN, -1);
            for (std::size_t c = 0; c < userN; ++c) {
                const int ti = attr.colInstance[c];
                if (ti < 0) continue;
                const auto& src = entry.columns[c]["sourceColumn"];
                if (!src.is_string()) continue;
                auto cit = cidMaps[static_cast<std::size_t>(ti)].find(src.get<std::string>());
                if (cit != cidMaps[static_cast<std::size_t>(ti)].end()) colCid[c] = cit->second;
            }
            used = attr.used;
            colInstance = attr.colInstance;
            runSql = aug.sql;
            D = static_cast<int>(used.size());
        }
    }

    std::vector<std::vector<std::int64_t>> rowids;  // [stored row][instance]; INT64_MIN = NULL
    if (error.empty()) {
        constexpr std::int64_t kNone = std::numeric_limits<std::int64_t>::min();
        const int userN = static_cast<int>(entry.columns.size());
        sqlite3_stmt* stmt = nullptr;
        // Execute the augmented query; if it unexpectedly fails to prepare or its
        // shape doesn't match, fall back to the plain query with no mapping.
        if (sqlite3_prepare_v2(db, runSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK ||
            sqlite3_column_count(stmt) != D + userN) {
            if (stmt) { sqlite3_finalize(stmt); stmt = nullptr; }
            D = 0;
            used.clear();
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                error = sqlite3_errmsg(db);
        }
        if (error.empty()) {
            entry.rows = json::array();
            int rc;
            while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                ++entry.rowCount;
                if (entry.rowCount <= kRowCap) {
                    json r = json::array();
                    for (int c = 0; c < userN; ++c) r.push_back(valueJson(stmt, D + c));
                    entry.rows.push_back(std::move(r));
                    if (D > 0) {
                        std::vector<std::int64_t> rr(static_cast<std::size_t>(D));
                        for (int ti = 0; ti < D; ++ti)
                            rr[static_cast<std::size_t>(ti)] =
                                sqlite3_column_type(stmt, ti) == SQLITE_NULL
                                    ? kNone : sqlite3_column_int64(stmt, ti);
                        rowids.push_back(std::move(rr));
                    }
                } else {
                    entry.truncated = true;
                }
            }
            if (rc != SQLITE_DONE) error = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
        }
    }

    ctx.out = nullptr;  // stop measuring (mapping below reads the map/cellMap dbs)
    sqlite3_close(db);

    if (!error.empty()) return json{{"error", error}}.dump();

    mapRowPages(entry, used, colInstance, colCid, rowids);
    entry.id = nextId_++;
    entry.pageCount = sink.distinctPages();
    entry.accesses = sink.accesses();
    entry.profilePages = profilePagesJson(sink);
    // The per-page profile can be huge (a wide analytical query touches hundreds of
    // thousands of pages). It's only needed by the map overlay (a secondary tab), so
    // keep the run response small: inline it only when small, otherwise defer it to
    // an on-demand /api/query/:id/profile fetch. The full profile stays stored here.
    const bool deferProfile =
        static_cast<std::int64_t>(entry.profilePages.size()) > kInlineProfileCap;
    json summary = {
        {"queryId", entry.id},
        {"columns", entry.columns},
        {"rowCount", entry.rowCount},
        {"truncated", entry.truncated},
        {"pageCount", entry.pageCount},
        {"accesses", entry.accesses},
        {"profileDeferred", deferProfile},
        {"profile", {{"pages", deferProfile ? json::array() : entry.profilePages}}},
    };
    history_.push_back(std::move(entry));
    return summary.dump();
}

std::string QueryEngine::profileJson(int id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const Entry* e = find(id);
    if (e == nullptr) return R"({"error":"no such query"})";
    return json{{"pages", e->profilePages}}.dump();
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
    const bool deferProfile =
        static_cast<std::int64_t>(e->profilePages.size()) > kInlineProfileCap;
    return json{
        {"id", e->id}, {"sql", e->sql}, {"columns", e->columns},
        {"rowCount", e->rowCount}, {"truncated", e->truncated},
        {"pageCount", e->pageCount}, {"accesses", e->accesses},
        {"profileDeferred", deferProfile},
        {"profile", {{"pages", deferProfile ? json::array() : e->profilePages}}},
    }.dump();
}
