#include "visualize/map_db.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "map/map_writer.hpp"

using nlohmann::json;

namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("visualize: " + what);
}

// Reads a column as a JSON value (null-aware).
json columnJson(sqlite3_stmt* s, int col) {
    switch (sqlite3_column_type(s, col)) {
        case SQLITE_NULL: return nullptr;
        case SQLITE_INTEGER: return sqlite3_column_int64(s, col);
        case SQLITE_FLOAT: return sqlite3_column_double(s, col);
        default: {
            const unsigned char* t = sqlite3_column_text(s, col);
            return t ? json(reinterpret_cast<const char*>(t)) : json(nullptr);
        }
    }
}

// Runs a query and returns an array of row objects keyed by column name.
json queryRows(sqlite3* db, const std::string& sql,
               const std::vector<std::int64_t>& binds = {}) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        fail(std::string("query failed: ") + sqlite3_errmsg(db));
    }
    for (std::size_t i = 0; i < binds.size(); ++i) {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), binds[i]);
    }
    json rows = json::array();
    const int cols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json row = json::object();
        for (int c = 0; c < cols; ++c) {
            row[sqlite3_column_name(stmt, c)] = columnJson(stmt, c);
        }
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return rows;
}

// Builds an " AND leafId IN (...)" fragment for the selected leaves, or empty
// when nothing is selected (meaning "all leaves"). The ids are integers parsed
// server-side, so inlining them is injection-safe.
std::string sourceInClause(const std::vector<int>& sel) {
    if (sel.empty()) return {};
    std::string s = " AND sourceId IN (";
    for (std::size_t i = 0; i < sel.size(); ++i) {
        if (i) s += ',';
        s += std::to_string(sel[i]);
    }
    s += ')';
    return s;
}

bool tableExists(sqlite3* db, const char* name) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
                       "SELECT 1 FROM sqlite_schema WHERE type='table' AND name=?",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

}  // namespace

ReadPool::~ReadPool() {
    for (auto& [id, c] : conns_) sqlite3_close(c);
}

void ReadPool::init(std::string path, std::function<void(sqlite3*)> onOpen) {
    path_ = std::move(path);
    onOpen_ = std::move(onOpen);
}

ReadPool::operator sqlite3*() const {
    const std::thread::id id = std::this_thread::get_id();
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = conns_.find(id);
        if (it != conns_.end()) return it->second;
    }
    // Open (and seed) this thread's connection outside the lock so the one-time
    // per-thread setup never blocks other threads' queries.
    sqlite3* c = nullptr;
    if (sqlite3_open_v2(path_.c_str(), &c, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(c);
        return nullptr;
    }
    if (onOpen_) onOpen_(c);
    std::lock_guard<std::mutex> lk(mu_);
    conns_[id] = c;
    return c;
}

MapDb::MapDb(const std::string& mapPath) : mapPath_(mapPath) {
    // Validate on a throwaway connection (clear error message on failure), then set
    // up the per-thread read pool. onConnOpen seeds each connection's profile table.
    sqlite3* v = nullptr;
    if (sqlite3_open_v2(mapPath.c_str(), &v, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(v);
        sqlite3_close(v);
        fail("cannot open map file: " + msg);
    }
    for (const char* t : {"meta", "objects", "pages", "runs", "type_counts"}) {
        if (!tableExists(v, t)) {
            sqlite3_close(v);
            fail("not a sqlinsite map (missing table '" + std::string(t) + "')");
        }
    }
    sqlite3_close(v);
    db_.init(mapPath, [this](sqlite3* c) { onConnOpen(c); });
}

MapDb::~MapDb() = default;

void MapDb::loadProfile(const std::string& profileDbPath) {
    // Import the input profile as 'input' sources into the shared profile db; each
    // pool connection ATTACHes it on open (see onConnOpen). Always called before
    // serving, so every connection is opened after this.
    profile_.importInputProfile(profileDbPath);
    hasProfile_ = true;
}

std::int64_t MapDb::addQuerySource(const std::string& sql, int queryId,
                                   const std::vector<ProfileDb::PageCount>& pages) {
    const std::int64_t sourceId = profile_.addQuerySource(sql, queryId, pages);
    hasProfile_ = true;  // an overlay is now available even without --profile-file
    return sourceId;
}

void MapDb::loadManifest(const std::string& manifestPath, const std::string& dbName) {
    // Read the map's page size/count from a short-lived private connection so we do
    // NOT open the pool connection before manifest_ is set — pool connections ATTACH
    // `manifest` in onConnOpen only when it already exists.
    int pageSize = 0;
    std::int64_t pageCount = 0;
    sqlite3* c = nullptr;
    if (sqlite3_open_v2(mapPath_.c_str(), &c, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        json m = queryRows(c, "SELECT pageSize, pageCount FROM meta LIMIT 1");
        if (!m.empty()) {
            if (!m[0]["pageSize"].is_null()) pageSize = m[0]["pageSize"].get<int>();
            if (!m[0]["pageCount"].is_null()) pageCount = m[0]["pageCount"].get<std::int64_t>();
        }
    }
    sqlite3_close(c);
    manifest_ = std::make_unique<ManifestDb>(manifestPath, dbName, pageCount, pageSize);
}

std::string MapDb::profileDbPath() const { return profile_.path(); }
std::string MapDb::manifestDbPath() const {
    return manifest_ ? manifest_->path() : std::string();
}

void MapDb::onConnOpen(sqlite3* c) const {
    // ATTACH the shared profile db so overlay queries can read `prof.page_access`
    // and `prof.sources`. Always attached (even with no --profile-file) so
    // interactive Query-view runs can be overlaid too.
    char* sql = sqlite3_mprintf("ATTACH DATABASE %Q AS prof", profile_.path().c_str());
    sqlite3_exec(c, sql, nullptr, nullptr, nullptr);
    sqlite3_free(sql);
    // ATTACH the CBS manifest (blocks/databases/meta) when present, so block
    // endpoints can join `manifest.blocks` against the map's pages.
    if (manifest_) {
        char* ms = sqlite3_mprintf("ATTACH DATABASE %Q AS manifest", manifest_->path().c_str());
        sqlite3_exec(c, ms, nullptr, nullptr, nullptr);
        sqlite3_free(ms);
    }
}

std::string MapDb::metaJson() const {
    json meta = json::object();
    json rows = queryRows(db_, "SELECT * FROM meta LIMIT 1");
    if (!rows.empty()) meta = rows[0];

    // The profile selection tree (loaded 'input' + interactive 'query' sources) is
    // served separately by /api/profile/sources, which the client refetches after
    // each run to pick up new sources.
    json j = {
        {"meta", meta},
        // The format version this build understands; the front-end compares it to
        // meta.formatVersion and refuses to render an older/newer map.
        {"expectedFormatVersion", MapWriter::kFormatVersion},
        {"objects",
         queryRows(db_,
                   "SELECT id,type,name,tableName,rootPage,pageCount,"
                   "COALESCE((SELECT MIN(pageNumber) FROM pages WHERE objectId=objects.id),"
                   "rootPage) AS startPage,"
                   "COALESCE((SELECT MIN(pageNumber) FROM pages WHERE objectId=objects.id "
                   "AND pageType LIKE '%-leaf'),"
                   "(SELECT MIN(pageNumber) FROM pages WHERE objectId=objects.id),"
                   "rootPage) AS startLeafPage "
                   "FROM objects ORDER BY id")},
        {"typeCounts",
         queryRows(db_, "SELECT pageType,count FROM type_counts ORDER BY pageType")},
        {"hasProfile", hasProfile_},
        {"hasDb", hasDb_},
        {"hasManifest", manifest_ != nullptr},
    };
    if (manifest_) {
        j["blockSize"] = manifest_->blockSize();
        j["pagesPerBlock"] = manifest_->pagesPerBlock();
        j["blockCount"] = manifest_->blockCount();
        j["manifestDbName"] = manifest_->selectedDbName();
        j["manifestMatch"] = manifest_->match();
        j["manifestMatchReason"] = manifest_->matchReason();
    }
    return j.dump();
}

std::unordered_map<std::int64_t, std::int64_t> MapDb::leafPagesForRowids(
    const std::string& tableName, const std::vector<std::int64_t>& rowids) const {
    std::unordered_map<std::int64_t, std::int64_t> out;
    if (rowids.empty()) return out;

    // Short-lived private connection so this never touches the shared db_ (used by
    // other requests concurrently). The map is read-only.
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(mapPath_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return out;
    }
    out.reserve(rowids.size());

    // Resolve the table's objectId once, then locate each rowid's leaf via a single
    // indexed lookup on page_row_runs: among this object's leaf runs (disjoint,
    // ascending — page_row_runs_leaf), the one with the largest startRowId ≤ R is R's
    // leaf when its endRowId ≥ R. O(log runs) per rowid, no whole-table scan.
    std::int64_t objId = -1;
    sqlite3_stmt* obj = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id FROM objects WHERE name=?1 AND type='table'",
                           -1, &obj, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(obj, 1, tableName.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(obj) == SQLITE_ROW) objId = sqlite3_column_int64(obj, 0);
    }
    sqlite3_finalize(obj);
    if (objId < 0) { sqlite3_close(db); return out; }

    sqlite3_stmt* sel = nullptr;
    if (sqlite3_prepare_v2(
            db,
            "SELECT parentPageNumber FROM page_row_runs "
            "WHERE objectId=?1 AND isLeaf=1 AND startRowId<=?2 AND endRowId>=?2 "
            "ORDER BY startRowId DESC LIMIT 1",
            -1, &sel, nullptr) == SQLITE_OK) {
        for (const std::int64_t rid : rowids) {
            sqlite3_bind_int64(sel, 1, objId);
            sqlite3_bind_int64(sel, 2, rid);
            if (sqlite3_step(sel) == SQLITE_ROW) out[rid] = sqlite3_column_int64(sel, 0);
            sqlite3_reset(sel);
        }
    }
    sqlite3_finalize(sel);
    sqlite3_close(db);
    return out;
}

std::string MapDb::pagesJson(std::int64_t from, std::int64_t to,
                             bool& tooLarge) const {
    tooLarge = (to - from + 1) > kPageRangeCap;
    if (tooLarge) return {};
    json j = {{"pages", queryRows(db_,
                                  "SELECT pageNumber,pageType,objectId FROM pages "
                                  "WHERE pageNumber BETWEEN ? AND ? ORDER BY pageNumber",
                                  {from, to})}};
    return j.dump();
}

std::string MapDb::runsJson(std::int64_t from, std::int64_t to) const {
    json j = {{"runs", queryRows(db_,
                                 "SELECT startPage,endPage,pageType,objectId FROM runs "
                                 "WHERE startPage <= ? AND endPage >= ? ORDER BY startPage",
                                 {to, from})}};
    return j.dump();
}

std::string MapDb::minimapJson(int buckets) const {
    if (buckets < 1) buckets = 1;
    // Concurrent threads may race here now (per-thread connections); the cache is
    // shared, so guard it. First caller computes; the rest wait then hit the cache.
    std::lock_guard<std::mutex> lk(minimapMu_);
    if (minimapCacheBuckets_ == buckets && !minimapCache_.empty()) return minimapCache_;

    json m = queryRows(db_, "SELECT pageCount FROM meta LIMIT 1");
    const std::int64_t pageCount =
        (!m.empty() && !m[0]["pageCount"].is_null()) ? m[0]["pageCount"].get<std::int64_t>() : 0;
    if (pageCount <= 0) {
        return json({{"pageCount", 0}, {"buckets", json::array()}}).dump();
    }
    // Never more buckets than pages; page b (1-based) → bucket (b-1)*n/pageCount,
    // bucket k spans pages [k*pageCount/n + 1, (k+1)*pageCount/n].
    const std::int64_t n = std::min<std::int64_t>(buckets, pageCount);

    // Tally owned pages per object within each bucket, from the runs table (which
    // coalesces every page, owned or not). objectId -1 stands in for NULL/unowned.
    std::vector<std::unordered_map<std::int64_t, std::int64_t>> tally(static_cast<std::size_t>(n));
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT startPage,endPage,objectId FROM runs", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const std::int64_t s = sqlite3_column_int64(stmt, 0);
        const std::int64_t e = sqlite3_column_int64(stmt, 1);
        const std::int64_t obj =
            sqlite3_column_type(stmt, 2) == SQLITE_NULL ? -1 : sqlite3_column_int64(stmt, 2);
        // Split the run at bucket boundaries, adding each slice to its bucket.
        std::int64_t p = std::max<std::int64_t>(1, s);
        while (p <= e) {
            std::int64_t k = (p - 1) * n / pageCount;
            if (k >= n) k = n - 1;
            std::int64_t bucketEnd = (k + 1) * pageCount / n; // last page of bucket k
            const std::int64_t segEnd = std::min(e, std::max(p, bucketEnd));
            tally[static_cast<std::size_t>(k)][obj] += segEnd - p + 1;
            p = segEnd + 1;
        }
    }
    sqlite3_finalize(stmt);

    // Dominant object per bucket (most pages; ties → smaller objectId for determinism).
    std::vector<std::int64_t> dom(static_cast<std::size_t>(n), -1);
    for (std::int64_t k = 0; k < n; ++k) {
        std::int64_t best = -1, bestCount = -1;
        for (const auto& [obj, cnt] : tally[static_cast<std::size_t>(k)]) {
            if (cnt > bestCount || (cnt == bestCount && obj < best)) { best = obj; bestCount = cnt; }
        }
        dom[static_cast<std::size_t>(k)] = best;
    }

    // Emit contiguous spans, merging adjacent buckets with the same dominant object.
    json arr = json::array();
    for (std::int64_t k = 0; k < n;) {
        std::int64_t k2 = k;
        while (k2 + 1 < n && dom[static_cast<std::size_t>(k2 + 1)] == dom[static_cast<std::size_t>(k)]) ++k2;
        const std::int64_t startPage = k * pageCount / n + 1;
        const std::int64_t endPage = (k2 + 1) * pageCount / n;
        const std::int64_t obj = dom[static_cast<std::size_t>(k)];
        arr.push_back({{"startPage", startPage}, {"endPage", endPage},
                       {"objectId", obj < 0 ? json(nullptr) : json(obj)}});
        k = k2 + 1;
    }

    minimapCache_ = json({{"pageCount", pageCount}, {"buckets", std::move(arr)}}).dump();
    minimapCacheBuckets_ = buckets;
    return minimapCache_;
}

std::string MapDb::objectPagesJson(std::int64_t objectId, std::int64_t from,
                                   std::int64_t to, bool& tooLarge) const {
    tooLarge = (to - from + 1) > kPageRangeCap;
    if (tooLarge) return {};
    const std::int64_t limit = to - from + 1;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
                       "SELECT pageNumber,pageType FROM pages WHERE objectId=? "
                       "ORDER BY pageNumber LIMIT ? OFFSET ?",
                       -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, objectId);
    sqlite3_bind_int64(stmt, 2, limit);
    sqlite3_bind_int64(stmt, 3, from);
    json pages = json::array();
    std::int64_t ordinal = from;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        pages.push_back({{"ordinal", ordinal++},
                         {"pageNumber", sqlite3_column_int64(stmt, 0)},
                         {"pageType",
                          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))}});
    }
    sqlite3_finalize(stmt);
    return json({{"pages", std::move(pages)}}).dump();
}

std::string MapDb::objectPageOrdinalJson(std::int64_t objectId, std::int64_t page) const {
    // The page must belong to the object; its ordinal is how many of the object's
    // pages precede it (pages are laid out in the band ordered by pageNumber).
    json owns = queryRows(db_, "SELECT 1 FROM pages WHERE pageNumber=?1 AND objectId=?2",
                          {page, objectId});
    if (owns.empty()) return json({{"ordinal", -1}}).dump();
    json n = queryRows(db_, "SELECT COUNT(*) AS n FROM pages WHERE objectId=?1 AND pageNumber<?2",
                       {objectId, page});
    return json({{"ordinal", n[0]["n"]}}).dump();
}

const std::vector<OrdinalRun>& MapDb::objectRuns(std::int64_t objectId) const {
    {
        std::lock_guard<std::mutex> lk(objRunsMu_);
        auto it = objRunsCache_.find(objectId);
        if (it != objRunsCache_.end()) return it->second;
    }
    // Build outside the lock: read the object's physical runs in pageNumber order
    // (identical to the Pages-view runs, filtered by objectId) and accumulate an
    // ordinal offset — the object's page-number gaps collapse in ordinal space, but
    // each run is kept whole so the runs match the Pages view exactly.
    std::vector<OrdinalRun> built;
    sqlite3* conn = db_;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(conn,
                           "SELECT startPage, endPage, pageType FROM runs "
                           "WHERE objectId=?1 ORDER BY startPage",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, objectId);
        std::int64_t ord = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const std::int64_t startPage = sqlite3_column_int64(stmt, 0);
            const std::int64_t endPage = sqlite3_column_int64(stmt, 1);
            const std::int64_t len = endPage - startPage + 1;
            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            built.push_back({ord, ord + len - 1, startPage, endPage, t ? t : ""});
            ord += len;
        }
        sqlite3_finalize(stmt);
    }
    std::lock_guard<std::mutex> lk(objRunsMu_);
    auto [it, _] = objRunsCache_.emplace(objectId, std::move(built));
    return it->second;
}

std::string MapDb::objectRunsJson(std::int64_t objectId, std::int64_t from, std::int64_t to) const {
    const std::vector<OrdinalRun>& runs = objectRuns(objectId);
    // runs are ordinal-contiguous and sorted; binary-search the first one that
    // reaches `from`, then emit until past `to`.
    std::size_t lo = 0, hi = runs.size();
    while (lo < hi) {
        std::size_t m = (lo + hi) / 2;
        if (runs[m].endOrdinal < from) lo = m + 1; else hi = m;
    }
    json arr = json::array();
    for (std::size_t i = lo; i < runs.size() && runs[i].startOrdinal <= to; ++i) {
        arr.push_back({{"startOrdinal", runs[i].startOrdinal},
                       {"endOrdinal", runs[i].endOrdinal},
                       {"startPage", runs[i].startPage},
                       {"endPage", runs[i].endPage},
                       {"pageType", runs[i].pageType}});
    }
    return json({{"runs", std::move(arr)}}).dump();
}

namespace {
bool isFreePageType(const std::string& t) {
    return t == "freelist-trunk" || t == "freelist-leaf" || t == "unallocated";
}
}  // namespace

const std::vector<BlockAgg>& MapDb::blocksAgg() const {
    std::lock_guard<std::mutex> lk(blocksMu_);
    if (blocksBuilt_) return blocksCache_;
    blocksBuilt_ = true;
    if (!manifest_ || !manifest_->match()) return blocksCache_;  // stays empty

    const std::int64_t ppb = manifest_->pagesPerBlock();
    const std::int64_t nBlk = manifest_->blockCount();
    const std::int64_t selId = manifest_->selectedDbId();
    if (ppb <= 0 || nBlk <= 0) return blocksCache_;

    blocksCache_.resize(static_cast<std::size_t>(nBlk));
    for (std::int64_t i = 0; i < nBlk; ++i) blocksCache_[static_cast<std::size_t>(i)].blockIndex = i;

    // Block ids + shared flag from the manifest.
    {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT blockIndex, blockId, sharedWithParent FROM manifest.blocks "
                "WHERE dbId=?1", -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(s, 1, selId);
            while (sqlite3_step(s) == SQLITE_ROW) {
                const std::int64_t bi = sqlite3_column_int64(s, 0);
                if (bi < 0 || bi >= nBlk) continue;
                BlockAgg& a = blocksCache_[static_cast<std::size_t>(bi)];
                const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
                a.blockId = id ? id : "";
                a.sharedWithParent = sqlite3_column_int(s, 2);
            }
            sqlite3_finalize(s);
        }
    }

    // Distribute the map's pre-coalesced runs across the blocks they span (a run
    // rarely spans more than one block), tallying real/free pages and per-object
    // page counts for the dominant object + object mix.
    std::vector<std::map<std::int64_t, std::int64_t>> objCounts(static_cast<std::size_t>(nBlk));
    sqlite3_stmt* rs = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT startPage,endPage,pageType,objectId FROM runs "
                                "ORDER BY startPage", -1, &rs, nullptr) == SQLITE_OK) {
        while (sqlite3_step(rs) == SQLITE_ROW) {
            const std::int64_t sp = sqlite3_column_int64(rs, 0);
            const std::int64_t ep = sqlite3_column_int64(rs, 1);
            const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(rs, 2));
            const bool freeType = isFreePageType(t ? t : "");
            const std::int64_t objId =
                sqlite3_column_type(rs, 3) == SQLITE_NULL ? -1 : sqlite3_column_int64(rs, 3);
            const std::int64_t b0 = (sp - 1) / ppb, b1 = (ep - 1) / ppb;
            for (std::int64_t bi = b0; bi <= b1 && bi < nBlk; ++bi) {
                const std::int64_t lo = std::max(sp, bi * ppb + 1);
                const std::int64_t hi = std::min(ep, (bi + 1) * ppb);
                const std::int64_t cnt = hi - lo + 1;
                if (cnt <= 0) continue;
                BlockAgg& a = blocksCache_[static_cast<std::size_t>(bi)];
                a.realPages += cnt;
                if (freeType) a.freePages += cnt;
                objCounts[static_cast<std::size_t>(bi)][objId] += cnt;
            }
        }
        sqlite3_finalize(rs);
    }

    for (std::int64_t bi = 0; bi < nBlk; ++bi) {
        BlockAgg& a = blocksCache_[static_cast<std::size_t>(bi)];
        std::vector<std::pair<std::int64_t, std::int64_t>> mix(
            objCounts[static_cast<std::size_t>(bi)].begin(),
            objCounts[static_cast<std::size_t>(bi)].end());
        std::sort(mix.begin(), mix.end(),
                  [](const auto& x, const auto& y) { return x.second > y.second; });
        a.objectMix = std::move(mix);
        // Dominant = the owning object (objectId >= 0) with the most pages.
        for (const auto& [oid, c] : a.objectMix) {
            (void)c;
            if (oid >= 0) { a.dominantObjectId = oid; break; }
        }
    }
    return blocksCache_;
}

std::string MapDb::blocksJson(std::int64_t from, std::int64_t to) const {
    const std::vector<BlockAgg>& all = blocksAgg();
    if (all.empty()) return R"({"blocks":[]})";
    const std::int64_t ppb = manifest_->pagesPerBlock();
    json m = queryRows(db_, "SELECT pageCount FROM meta LIMIT 1");
    const std::int64_t pageCount =
        (!m.empty() && !m[0]["pageCount"].is_null()) ? m[0]["pageCount"].get<std::int64_t>() : 0;
    from = std::max<std::int64_t>(0, from);
    to = std::min<std::int64_t>(to, static_cast<std::int64_t>(all.size()) - 1);
    json arr = json::array();
    for (std::int64_t i = from; i <= to; ++i) {
        const BlockAgg& a = all[static_cast<std::size_t>(i)];
        arr.push_back({{"blockIndex", a.blockIndex},
                       {"blockId", a.blockId},
                       {"startPage", a.blockIndex * ppb + 1},
                       {"endPage", std::min((a.blockIndex + 1) * ppb, pageCount)},
                       {"realPages", a.realPages},
                       {"usedPages", a.realPages - a.freePages},
                       {"freePages", a.freePages},
                       {"dominantObjectId",
                        a.dominantObjectId >= 0 ? json(a.dominantObjectId) : json(nullptr)},
                       {"sharedWithParent", a.sharedWithParent != 0}});
    }
    return json({{"blocks", std::move(arr)}}).dump();
}

std::string MapDb::blockJson(std::int64_t blockIndex, const LeafFilter& sel) const {
    const std::vector<BlockAgg>& all = blocksAgg();
    if (blockIndex < 0 || blockIndex >= static_cast<std::int64_t>(all.size())) {
        return R"({"error":"no such block"})";
    }
    const BlockAgg& a = all[static_cast<std::size_t>(blockIndex)];
    const std::int64_t ppb = manifest_->pagesPerBlock();
    json m = queryRows(db_, "SELECT pageCount FROM meta LIMIT 1");
    const std::int64_t pageCount =
        (!m.empty() && !m[0]["pageCount"].is_null()) ? m[0]["pageCount"].get<std::int64_t>() : 0;
    const std::int64_t startPage = a.blockIndex * ppb + 1;
    const std::int64_t endPage = std::min((a.blockIndex + 1) * ppb, pageCount);

    // Object mix with names.
    json mix = json::array();
    for (const auto& [oid, c] : a.objectMix) {
        json name = nullptr;
        if (oid >= 0) {
            json r = queryRows(db_, "SELECT name FROM objects WHERE id=?1", {oid});
            if (!r.empty()) name = r[0]["name"];
        }
        mix.push_back({{"objectId", oid >= 0 ? json(oid) : json(nullptr)},
                       {"name", name}, {"pages", c}});
    }
    json j = {{"blockIndex", a.blockIndex},
              {"blockId", a.blockId},
              {"objectName", a.blockId + ".bcv"},
              {"startPage", startPage}, {"endPage", endPage},
              {"realPages", a.realPages}, {"usedPages", a.realPages - a.freePages},
              {"freePages", a.freePages},
              {"sharedWithParent", a.sharedWithParent != 0},
              {"objectMix", std::move(mix)}};
    // The parent checkpoint's name (for shared-with-parent context).
    json pn = queryRows(db_,
        "SELECT name FROM manifest.databases WHERE id="
        "(SELECT parent FROM manifest.databases WHERE id="
        "(SELECT selectedDbId FROM manifest.meta))");
    j["parentName"] = (!pn.empty() && !pn[0]["name"].is_null()) ? pn[0]["name"] : json(nullptr);
    if (hasProfile_) {
        json p = queryRows(db_,
            "SELECT COALESCE(SUM(reads),0) AS reads, COALESCE(SUM(writes),0) AS writes "
            "FROM prof.page_access WHERE pageNumber BETWEEN ? AND ?" + sourceInClause(sel),
            {startPage, endPage});
        j["profile"] = p.empty() ? json({{"reads", 0}, {"writes", 0}}) : p[0];
    }
    return j.dump();
}

namespace {
// The `pages` predicate for a Tables-view structural group, or "" for an unknown
// key. Mirrors the b-tree tree's structural roots: Freelist, Lock-Byte, Pointer-map,
// and the catch-all "All other pages" (unowned pages of no other structural group).
const char* structuralGroupWhere(const std::string& key) {
    if (key == "freelist") return "pageType IN ('freelist-trunk','freelist-leaf')";
    if (key == "lockbyte") return "pageType='lock-byte'";
    if (key == "pointermap") return "pageType='pointer-map'";
    if (key == "other")
        return "objectId IS NULL AND "
               "pageType NOT IN ('freelist-trunk','freelist-leaf','lock-byte','pointer-map')";
    return "";
}
}  // namespace

std::string MapDb::structuralGroupsJson() const {
    struct Group { const char* key; const char* label; };
    static constexpr Group kGroups[] = {
        {"freelist", "Freelist"}, {"lockbyte", "Lock-Byte"},
        {"pointermap", "Pointer-map"}, {"other", "All other pages"}};
    json groups = json::array();
    for (const Group& g : kGroups) {
        json c = queryRows(db_, std::string("SELECT COUNT(*) AS n FROM pages WHERE ") +
                                    structuralGroupWhere(g.key));
        if (c[0]["n"].get<std::int64_t>() > 0)
            groups.push_back({{"key", g.key}, {"label", g.label}, {"pageCount", c[0]["n"]}});
    }
    return json({{"groups", std::move(groups)}}).dump();
}

std::string MapDb::structuralGroupPagesJson(const std::string& key, std::int64_t from,
                                            std::int64_t to, bool& tooLarge) const {
    tooLarge = (to - from + 1) > kPageRangeCap;
    if (tooLarge) return {};
    const std::string where = structuralGroupWhere(key);
    if (where.empty()) return json({{"pages", json::array()}}).dump();
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
                       ("SELECT pageNumber,pageType FROM pages WHERE " + where +
                        " ORDER BY pageNumber LIMIT ? OFFSET ?").c_str(),
                       -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, to - from + 1);
    sqlite3_bind_int64(stmt, 2, from);
    json pages = json::array();
    std::int64_t ordinal = from;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        pages.push_back({{"ordinal", ordinal++},
                         {"pageNumber", sqlite3_column_int64(stmt, 0)},
                         {"pageType",
                          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))}});
    }
    sqlite3_finalize(stmt);
    return json({{"pages", std::move(pages)}}).dump();
}

std::string MapDb::structuralGroupPageOrdinalJson(const std::string& key,
                                                  std::int64_t page) const {
    const std::string where = structuralGroupWhere(key);
    if (where.empty()) return json({{"ordinal", -1}}).dump();
    // The page must be in the group; its ordinal is how many group pages precede it.
    json owns = queryRows(db_, "SELECT 1 FROM pages WHERE pageNumber=?1 AND (" + where + ")", {page});
    if (owns.empty()) return json({{"ordinal", -1}}).dump();
    json n = queryRows(db_, "SELECT COUNT(*) AS n FROM pages WHERE (" + where + ") AND pageNumber<?1",
                       {page});
    return json({{"ordinal", n[0]["n"]}}).dump();
}

std::string MapDb::pageJson(std::int64_t pageNumber, const LeafFilter& sel) const {
    json rows = queryRows(db_, "SELECT * FROM pages WHERE pageNumber=?",
                          {pageNumber});
    if (rows.empty()) return {};
    json j = rows[0];
    j["pointers"] = queryRows(db_,
                              "SELECT toPage,kind FROM pointers WHERE fromPage=?",
                              {pageNumber});
    j["cells"] = queryRows(db_,
                           "SELECT cellIndex,rowid,leftChild,payloadBytes,localBytes,"
                           "overflowPage FROM cells WHERE pageNumber=? "
                           "ORDER BY cellIndex",
                           {pageNumber});
    j["ptrmap"] = queryRows(db_,
                            "SELECT targetPage,entryType,parentPage FROM ptrmap "
                            "WHERE pageNumber=? ORDER BY targetPage",
                            {pageNumber});
    if (hasProfile_) {
        json p = queryRows(
            db_,
            "SELECT COALESCE(SUM(reads),0) AS reads, COALESCE(SUM(writes),0) AS writes "
            "FROM prof.page_access WHERE pageNumber=?" + sourceInClause(sel),
            {pageNumber});
        j["profile"] = p.empty() ? json({{"reads", 0}, {"writes", 0}}) : p[0];
    }
    // The CBS block that holds this page (when a matching manifest is loaded).
    if (manifest_ && manifest_->match()) {
        const std::int64_t ppb = manifest_->pagesPerBlock();
        const std::int64_t bi = (pageNumber - 1) / ppb;
        const std::vector<BlockAgg>& all = blocksAgg();
        json bid = nullptr;
        if (bi >= 0 && bi < static_cast<std::int64_t>(all.size()))
            bid = all[static_cast<std::size_t>(bi)].blockId;
        const std::int64_t pageSize = manifest_->blockSize() / ppb;
        j["block"] = {{"blockIndex", bi}, {"blockId", bid},
                      {"inBlockOffset", ((pageNumber - 1) % ppb) * pageSize}};
    }
    return j.dump();
}

std::string MapDb::profilePagesJson(std::int64_t from, std::int64_t to,
                                    const LeafFilter& sel) const {
    if (!hasProfile_) return R"({"pages":[]})";
    json j = {{"pages", queryRows(
                            db_,
                            "SELECT pageNumber, SUM(reads) AS reads, SUM(writes) AS writes "
                            "FROM prof.page_access WHERE pageNumber BETWEEN ? AND ?" +
                                sourceInClause(sel) +
                                " GROUP BY pageNumber ORDER BY pageNumber",
                            {from, to})}};
    return j.dump();
}

std::string MapDb::profileSourcesJson() const {
    return json({{"sources",
                  queryRows(db_,
                            "SELECT sourceId, kind, sessionName, sessionId "
                            "FROM prof.sources ORDER BY sourceId")}})
        .dump();
}

namespace {
// A page's tree children are its b-tree children, its overflow pages, and (for a
// freelist trunk) its freelist-leaf pages. Interior freelist-next / ptrmap-parent
// edges are not tree edges.
constexpr const char* kChildKinds = "('child','overflow','freelist-leaf')";

// Page-detail columns surfaced on tree nodes (hover popover + subtree-size label).
constexpr const char* kDetailCols = "cellCount, freeBytes, subtreePageCount";

// Copies the page-detail columns from a query row onto a tree node.
void mergeDetails(json& node, const json& row) {
    for (const char* k : {"cellCount", "freeBytes", "subtreePageCount"}) {
        node[k] = row.contains(k) ? row[k] : json(nullptr);
    }
}
}  // namespace

std::string MapDb::pageType(std::int64_t page) const {
    json r = queryRows(db_, "SELECT pageType FROM pages WHERE pageNumber=?", {page});
    if (r.empty() || !r[0]["pageType"].is_string()) return {};
    return r[0]["pageType"].get<std::string>();
}

std::int64_t MapDb::overflowOwner(std::int64_t page) const {
    // Walk overflow pointers up to the first non-overflow page (the owner). One
    // recursive CTE; the depth bound guards against cycles in a malformed map.
    const std::string sql =
        "WITH RECURSIVE up(pg, depth) AS ("
        " SELECT ?1, 0"
        " UNION ALL"
        " SELECT p.fromPage, up.depth+1 FROM up JOIN pointers p "
        "  ON p.toPage=up.pg AND p.kind='overflow' WHERE up.depth<10000"
        ") SELECT up.pg FROM up JOIN pages ON pages.pageNumber=up.pg "
        "WHERE pages.pageType<>'overflow' ORDER BY up.depth LIMIT 1";
    json r = queryRows(db_, sql, {page});
    return r.empty() ? 0 : r[0]["pg"].get<std::int64_t>();
}

/**
 * Returns a json string with format 
 * {
 * 'cells' : [
 *      {
 *      'leftChild': 424242
 *      'rowCount': 42,
 *      'runCount': 1,
 *      'rowRuns': [
 *          {
 *          'startRowId': 0,
 *          'endRowId': 42,
 *          'rowCount': 42
 *          }
 *      ]
 *      }
 * ]
 * 'rightmost': {
 *      'leftChild': 424242
 *      'rowCount': 42,
 *      'runCount': 1,
 *      'rowRuns': [
 *          {
 *          'startRowId': 0,
 *          'endRowId': 42,
 *          'rowCount': 42
 *          }
 *      ]
 * }
 * }
 * 
 */
std::string MapDb::tableInteriorRowRunsJson(std::int64_t page) const {

    const std::string sql =
        R"sql_(SELECT c.cellIndex, 
            json_object('leftChild', prr.parentPageNumber, 
                        'rowCount', sum(prr.rowCount),
                        'runCount', COUNT(*),
                        'rowRuns', json_group_array(
                            json_object('startRowId', prr.startRowId,
                                        'endRowId', prr.endRowId,
                                        'rowCount', prr.rowCount)
                                )
                ) AS cellContents
            FROM page_row_runs prr 
            LEFT OUTER JOIN cells c ON c.leftChild = prr.parentPageNumber
            WHERE prr.parentPageNumber IN (SELECT toPage FROM pointers WHERE fromPage = ?1) OR 
            prr.parentPageNumber = (SELECT rightmostPointer FROM pages WHERE pageNumber = ?1)
            GROUP BY c.cellIndex
            ORDER BY c.cellIndex ASC)sql_";
    json cells = queryRows(db_, sql, {page});

    json out = {{"cells", json::array()}};
    for (const auto& cell : cells) {
        // cellContents is JSON text from SQLite's json_object(); parse it back to
        // a nested object so consumers get {pageNumber, rowCount, rowRuns:[...]}
        // rather than a doubly-encoded string.
        if (!cell["cellContents"].is_string()) continue;
        json contents = json::parse(cell["cellContents"].get<std::string>());
        if (cell["cellIndex"].is_number()) {
            out["cells"].push_back(std::move(contents));
        } else {
            out["rightmost"] = std::move(contents);
        }
    }

    return out.dump();
}

std::string MapDb::pageBtreeInfoJson(std::int64_t page) const {
    json out = json::object();
    json rows = queryRows(db_,
                          "SELECT o.name AS name, o.type AS type, p.pageType AS pageType "
                          "FROM pages p LEFT JOIN objects o ON o.id = p.objectId "
                          "WHERE p.pageNumber = ?",
                          {page});
    if (rows.empty()) return out.dump();
    const json& r = rows[0];
    if (r["name"].is_string())
        out["object"] = {{"name", r["name"]}, {"type", r["type"]}};
    // Row count only for table b-tree pages: page_row_runs covers both interior
    // (whole subtree) and leaf (its own rows); COALESCE gives 0 for an empty page.
    const std::string type = r["pageType"].is_string() ? r["pageType"].get<std::string>() : "";
    if (type == "table-leaf" || type == "table-interior") {
        json rc = queryRows(
            db_, "SELECT COALESCE(SUM(rowCount), 0) AS rowCount FROM page_row_runs "
                 "WHERE parentPageNumber = ?",
            {page});
        out["rowCount"] = rc[0]["rowCount"];
    }
    return out.dump();
}

std::string MapDb::pageRowidRunsJson(std::int64_t page, std::int64_t after, int limit) const {
    json out = {{"table", nullptr}, {"runs", json::array()},
               {"nextAfter", nullptr}, {"totalRowCount", 0}};
    limit = std::clamp(limit, 1, 5000);

    // The page whose subtree rows we select: the table page itself, or (for an
    // overflow page) its owner leaf. `runsPage` also carries the objectId we use to
    // name the table.
    json pt = queryRows(db_, "SELECT pageType FROM pages WHERE pageNumber=?", {page});
    if (pt.empty()) return out.dump();
    const std::string type = pt[0]["pageType"].is_string() ? pt[0]["pageType"].get<std::string>() : "";
    std::int64_t runsPage = 0;
    if (type == "table-leaf" || type == "table-interior") runsPage = page;
    else if (type == "overflow") runsPage = overflowOwner(page);
    if (runsPage <= 0) return out.dump();

    // The owning table's name (only tables have rowid runs).
    json nm = queryRows(db_,
                        "SELECT o.name AS name FROM pages p JOIN objects o ON o.id=p.objectId "
                        "WHERE p.pageNumber=? AND o.type='table'",
                        {runsPage});
    if (nm.empty() || !nm[0]["name"].is_string()) return out.dump();
    out["table"] = nm[0]["name"];

    // Total rows across all of this page's runs (for the "X of Y" display).
    json tot = queryRows(db_, "SELECT COALESCE(SUM(rowCount),0) AS n FROM page_row_runs "
                              "WHERE parentPageNumber=?", {runsPage});
    out["totalRowCount"] = tot[0]["n"];

    // One extra row tells us whether another batch follows (keyset by startRowId).
    json runs = queryRows(
        db_, "SELECT startRowId AS lo, endRowId AS hi FROM page_row_runs "
             "WHERE parentPageNumber=?1 AND startRowId>?2 ORDER BY startRowId LIMIT ?3",
        {runsPage, after, limit + 1});
    const bool more = static_cast<int>(runs.size()) > limit;
    if (more) runs.erase(runs.begin() + limit);
    json arr = json::array();
    for (const json& r : runs) arr.push_back({r["lo"], r["hi"]});
    out["runs"] = std::move(arr);
    if (more && !runs.empty()) out["nextAfter"] = runs.back()["lo"];
    return out.dump();
}

std::string MapDb::treeRootsJson() const {
    json roots = json::array();

    // sqlite_schema — page 1 is its b-tree root.
    {
        const std::string sql =
            "SELECT 1 AS page, pageType, " + std::string(kDetailCols) + ", "
            "EXISTS(SELECT 1 FROM pointers WHERE fromPage=1 AND kind IN " +
            std::string(kChildKinds) + ") AS hasChildren FROM pages WHERE pageNumber=1";
        json r = queryRows(db_, sql);
        if (!r.empty()) {
            json node = {{"kind", "page"}, {"label", "sqlite_schema"}, {"page", 1},
                         {"pageType", r[0]["pageType"]},
                         {"objectId", nullptr}, {"hasChildren", r[0]["hasChildren"]}};
            mergeDetails(node, r[0]);
            roots.push_back(std::move(node));
        }
    }

    // Each table is a grouping root node holding its table b-tree and (if any) its
    // indexes; an index groups under its owning table via objects.tableName. The
    // b-tree/index details are inlined here (schema-object cardinality is small);
    // only the pages *below* each b-tree root load lazily via treeChildren.
    const std::string btreeCols =
        "o.id AS objectId, o.name AS name, o.tableName AS tableName, o.rootPage AS page, "
        "p.pageType AS pageType, p.cellCount AS cellCount, p.freeBytes AS freeBytes, "
        "p.subtreePageCount AS subtreePageCount, "
        "EXISTS(SELECT 1 FROM pointers WHERE fromPage=o.rootPage AND kind IN " +
            std::string(kChildKinds) + ") AS hasChildren ";
    // rootPage=1 is sqlite_schema, already represented by the Page 1 root.
    json tables = queryRows(
        db_, "SELECT " + btreeCols +
                 "FROM objects o LEFT JOIN pages p ON p.pageNumber=o.rootPage "
                 "WHERE o.type='table' AND o.rootPage<>1 ORDER BY o.name");
    json indexes = queryRows(
        db_, "SELECT " + btreeCols +
                 "FROM objects o LEFT JOIN pages p ON p.pageNumber=o.rootPage "
                 "WHERE o.type='index' ORDER BY o.name");

    // A table/index b-tree root page node, labelled "<name> (table|index)".
    auto btreeNode = [&](const json& o, const char* suffix) {
        json node = {
            {"kind", "page"},
            {"label", o["name"].get<std::string>() + " (" + suffix + ")"},
            {"page", o["page"]}, {"pageType", o["pageType"]},
            {"objectId", o["objectId"]}, {"hasChildren", o["hasChildren"]},
        };
        mergeDetails(node, o);
        return node;
    };

    for (json& t : tables) {
        json idxNodes = json::array();
        for (json& ix : indexes)
            if (ix["tableName"] == t["name"]) idxNodes.push_back(btreeNode(ix, "index"));
        roots.push_back({
            {"kind", "table"}, {"label", t["name"]},
            {"page", nullptr}, {"pageType", nullptr},
            {"objectId", t["objectId"]}, {"hasChildren", true},
            // Null tableBtree covers virtual/no-rootpage tables (no b-tree page).
            {"tableBtree", t["page"].is_null() ? json(nullptr) : btreeNode(t, "table")},
            {"indexes", std::move(idxNodes)},
        });
    }

    // Freelist (virtual) — present when any trunk exists.
    json fl = queryRows(db_,
                        "SELECT EXISTS(SELECT 1 FROM pages WHERE pageType='freelist-trunk') AS ex");
    if (!fl.empty() && fl[0]["ex"].get<int>() != 0) {
        roots.push_back({{"kind", "freelist"}, {"label", "Freelist"}, {"page", nullptr},
                         {"pageType", "freelist-trunk"}, {"objectId", nullptr},
                         {"hasChildren", true}});
    }

    // Lock-byte page — only exists in databases larger than 1 GiB.
    json lb = queryRows(db_, "SELECT pageNumber AS page, " + std::string(kDetailCols) +
                                 " FROM pages WHERE pageType='lock-byte' LIMIT 1");
    if (!lb.empty()) {
        json node = {{"kind", "page"}, {"label", "Lock-Byte"}, {"page", lb[0]["page"]},
                     {"pageType", "lock-byte"}, {"objectId", nullptr}, {"hasChildren", false}};
        mergeDetails(node, lb[0]);
        roots.push_back(std::move(node));
    }

    // Pointer-map (virtual) — present when the pointer map exists (auto/incremental
    // vacuum). Its children are the pointer-map pages.
    json pm = queryRows(db_, "SELECT EXISTS(SELECT 1 FROM pages WHERE pageType='pointer-map') AS ex");
    if (!pm.empty() && pm[0]["ex"].get<int>() != 0) {
        roots.push_back({{"kind", "pointermap"}, {"label", "Pointer-map"}, {"page", nullptr},
                         {"pageType", "pointer-map"}, {"objectId", nullptr},
                         {"hasChildren", true}});
    }

    // All other pages (virtual) — only when at least one such page exists. Uses
    // the same "unowned + non-structural" definition as structuralGroupsJson
    // (objectId IS NULL): the map assigns an objectId to every page reachable from
    // an object b-tree (root/child/overflow), so objectId IS NULL is exactly the
    // set of unowned pages. The pages_object index serves it as a seek, versus the
    // old form (NOT IN the whole pointers table) which full-scanned `pages`.
    json other = queryRows(
        db_,
        "SELECT EXISTS(SELECT 1 FROM pages WHERE pageNumber>1 AND (" +
            std::string(structuralGroupWhere("other")) + ")) AS ex");
    if (!other.empty() && other[0]["ex"].get<int>() != 0) {
        roots.push_back({{"kind", "other"}, {"label", "All other pages"}, {"page", nullptr},
                         {"pageType", nullptr}, {"objectId", nullptr}, {"hasChildren", true}});
    }

    return json{{"roots", std::move(roots)}}.dump();
}

std::string MapDb::treeObjectOverviewJson(std::int64_t objectId) const {
    json o = queryRows(db_,
                       "SELECT id AS objectId, type, name, sql, pageCount, rootPage "
                       "FROM objects WHERE id=?",
                       {objectId});
    if (o.empty()) return {};
    json obj = std::move(o[0]);
    // Total rows in the table subtree — page_row_runs covers the root page
    // (whether it is a leaf or an interior page).
    json rc = queryRows(db_,
                        "SELECT SUM(rowCount) AS rowCount FROM page_row_runs "
                        "WHERE parentPageNumber=(SELECT rootPage FROM objects WHERE id=?)",
                        {objectId});
    obj["rowCount"] = (!rc.empty() && !rc[0]["rowCount"].is_null()) ? rc[0]["rowCount"] : json(nullptr);
    // Indexes owned by this table (empty for a non-table object). Filtered in C++
    // since the join key (tableName) is text and queryRows binds only integers.
    json allIdx = queryRows(
        db_, "SELECT name, tableName, pageCount, rootPage, sql FROM objects WHERE type='index' ORDER BY name");
    json idx = json::array();
    for (json& ix : allIdx)
        if (ix["tableName"] == obj["name"])
            idx.push_back({{"name", ix["name"]}, {"pageCount", ix["pageCount"]},
                           {"rootPage", ix["rootPage"]}, {"sql", ix["sql"]}});
    obj["indexes"] = std::move(idx);
    return json{{"overview", std::move(obj)}}.dump();
}

std::string MapDb::treePathJson(std::int64_t page) const {
    // Walk parent pointers from `page` up to a root in one recursive CTE. Each row
    // carries its own incoming edge kind (null for the root, which has no parent);
    // the depth bound guards against cycles in a malformed map.
    const std::string kinds = kChildKinds;
    const std::string sql =
        "WITH RECURSIVE anc(page, edgeKind, depth) AS ("
        " SELECT ?1, (SELECT kind FROM pointers WHERE toPage=?1 AND kind IN " + kinds + " LIMIT 1), 0"
        " UNION ALL"
        " SELECT p.fromPage,"
        "        (SELECT kind FROM pointers WHERE toPage=p.fromPage AND kind IN " + kinds + " LIMIT 1),"
        "        anc.depth+1"
        " FROM anc JOIN pointers p ON p.toPage=anc.page AND p.kind IN " + kinds +
        " WHERE anc.depth<10000"
        ") SELECT page, edgeKind FROM anc ORDER BY depth DESC";
    return json{{"path", queryRows(db_, sql, {page})}}.dump();
}

std::string MapDb::treeSearchJson(const std::string& query, int limit) const {
    json matches = json::array();
    // Only a non-empty digit string with no leading zero can prefix a page number
    // (no page number starts with 0); cap the length to keep the value in range.
    if (query.empty() || query.size() > 18 || query[0] == '0' ||
        !std::all_of(query.begin(), query.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; }))
        return json{{"matches", matches}}.dump();
    limit = std::clamp(limit, 1, 200);

    const std::int64_t base = std::stoll(query);
    json meta = queryRows(db_, "SELECT pageCount FROM meta LIMIT 1");
    const std::int64_t pageCount =
        (!meta.empty() && !meta[0]["pageCount"].is_null()) ? meta[0]["pageCount"].get<std::int64_t>() : 0;

    // Page numbers whose decimal string starts with `query`: the exact value, then
    // each prefix-extension range [base·10^k, base·10^k + 10^k − 1], ascending.
    std::vector<std::int64_t> candidates;
    if (base >= 1 && base <= pageCount) candidates.push_back(base);
    std::int64_t lo = base;
    while (static_cast<int>(candidates.size()) < limit && lo <= pageCount / 10) {
        lo *= 10;                                 // base·10^k
        const std::int64_t width = lo / base;      // 10^k
        const std::int64_t hi = std::min(pageCount, lo + width - 1);
        for (std::int64_t p = lo; p <= hi && static_cast<int>(candidates.size()) < limit; ++p)
            candidates.push_back(p);
    }
    if (candidates.empty()) return json{{"matches", matches}}.dump();

    // Fetch node details for the matched pages (one indexed lookup by PK).
    std::string inList;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i) inList += ',';
        inList += std::to_string(candidates[i]);
    }
    const std::string sql =
        "SELECT pageNumber AS page, pageType, objectId, cellCount, freeBytes, subtreePageCount, "
        "EXISTS(SELECT 1 FROM pointers WHERE fromPage=pages.pageNumber AND kind IN " +
            std::string(kChildKinds) + ") AS hasChildren "
        "FROM pages WHERE pageNumber IN (" + inList + ")";
    json rows = queryRows(db_, sql);
    std::map<std::int64_t, json> byPage;
    for (json& r : rows) {
        const std::int64_t p = r["page"].get<std::int64_t>();
        byPage[p] = std::move(r);
    }
    for (std::int64_t p : candidates) {
        auto it = byPage.find(p);
        if (it == byPage.end()) continue;
        it->second["label"] = "Page " + std::to_string(p);
        matches.push_back(std::move(it->second));
    }
    return json{{"matches", std::move(matches)}}.dump();
}

std::string MapDb::treeChildrenJson(std::int64_t page) const {
    const std::string sql =
        "SELECT ptr.toPage AS page, ptr.kind AS kind, p.pageType AS pageType, "
        "p.objectId AS objectId, p.cellCount AS cellCount, p.freeBytes AS freeBytes, "
        "p.subtreePageCount AS subtreePageCount, "
        "EXISTS(SELECT 1 FROM pointers c WHERE c.fromPage=ptr.toPage AND c.kind IN " +
            std::string(kChildKinds) + ") AS hasChildren "
        "FROM pointers ptr JOIN pages p ON p.pageNumber=ptr.toPage "
        "WHERE ptr.fromPage=? AND ptr.kind IN " + std::string(kChildKinds) + " "
        "ORDER BY CASE ptr.kind WHEN 'child' THEN 0 WHEN 'overflow' THEN 1 ELSE 2 END, ptr.toPage";
    return json{{"children", queryRows(db_, sql, {page})}}.dump();
}

std::string MapDb::treeFreelistJson(std::int64_t after, std::int64_t limit) const {
    const std::string sql =
        "SELECT pageNumber AS page, pageType, " + std::string(kDetailCols) + ", "
        "EXISTS(SELECT 1 FROM pointers WHERE fromPage=pages.pageNumber AND kind='freelist-leaf') "
        "AS hasChildren "
        "FROM pages WHERE pageType='freelist-trunk' AND pageNumber>? "
        "ORDER BY pageNumber LIMIT ?";
    return json{{"pages", queryRows(db_, sql, {after, limit})}}.dump();
}

std::string MapDb::treeOtherJson(std::int64_t after, std::int64_t limit) const {
    const std::string sql =
        "SELECT pageNumber AS page, pageType, objectId, " + std::string(kDetailCols) + ", "
        "EXISTS(SELECT 1 FROM pointers WHERE fromPage=pages.pageNumber AND kind IN " +
            std::string(kChildKinds) + ") AS hasChildren "
        "FROM pages "
        "WHERE pageNumber>1 AND pageNumber>? "
        "AND pageType NOT IN ('freelist-trunk','freelist-leaf','lock-byte','pointer-map') "
        "AND pageNumber NOT IN (SELECT rootPage FROM objects) "
        "AND pageNumber NOT IN (SELECT toPage FROM pointers) "
        "ORDER BY pageNumber LIMIT ?";
    return json{{"pages", queryRows(db_, sql, {after, limit})}}.dump();
}

std::string MapDb::treePointerMapJson(std::int64_t after, std::int64_t limit) const {
    // Pointer-map pages have no b-tree children of their own (they are flat arrays).
    const std::string sql =
        "SELECT pageNumber AS page, pageType, objectId, " + std::string(kDetailCols) + ", "
        "0 AS hasChildren "
        "FROM pages WHERE pageType='pointer-map' AND pageNumber>? "
        "ORDER BY pageNumber LIMIT ?";
    return json{{"pages", queryRows(db_, sql, {after, limit})}}.dump();
}
