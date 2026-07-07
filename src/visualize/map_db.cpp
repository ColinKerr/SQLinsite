#include "visualize/map_db.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "visualize/profile_reader.hpp"
#include "visualize/run_coalesce.hpp"

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
std::string leafInClause(const std::vector<int>& sel) {
    if (sel.empty()) return {};
    std::string s = " AND leafId IN (";
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

MapDb::MapDb(const std::string& mapPath) {
    if (sqlite3_open_v2(mapPath.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        fail("cannot open map file: " + msg);
    }
    for (const char* t : {"meta", "objects", "pages", "runs", "type_counts"}) {
        if (!tableExists(db_, t)) {
            sqlite3_close(db_);
            db_ = nullptr;
            fail("not a sqlinsite map (missing table '" + std::string(t) + "')");
        }
    }
}

MapDb::~MapDb() { sqlite3_close(db_); }

void MapDb::loadProfile(const std::string& csvPath) {
    const ProfileAggregate agg = aggregateProfileFile(csvPath);
    leaves_ = agg.leaves;
    sqlite3_exec(db_,
                 "CREATE TEMP TABLE profile(leafId INTEGER, pageNumber INTEGER, "
                 "reads INTEGER, writes INTEGER)",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO profile VALUES (?,?,?,?)", -1, &ins,
                       nullptr);
    for (const LeafPageAccess& a : agg.leafPages) {
        sqlite3_bind_int(ins, 1, a.leafId);
        sqlite3_bind_int64(ins, 2, a.pageNumber);
        sqlite3_bind_int64(ins, 3, a.reads);
        sqlite3_bind_int64(ins, 4, a.writes);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "CREATE INDEX profile_page ON profile(pageNumber)", nullptr,
                 nullptr, nullptr);
    hasProfile_ = true;
}

std::string MapDb::metaJson() const {
    json meta = json::object();
    json rows = queryRows(db_, "SELECT * FROM meta LIMIT 1");
    if (!rows.empty()) meta = rows[0];

    // Sessions tree for the profile checkbox control: each session lists its
    // statement leaves (leafId + 0-based statement index), preserving CSV order.
    json sessions = json::array();
    json* current = nullptr;
    std::string currentName;
    for (const ProfileLeaf& leaf : leaves_) {
        if (current == nullptr || leaf.sessionName != currentName) {
            sessions.push_back({{"session", leaf.sessionName}, {"leaves", json::array()}});
            current = &sessions.back();
            currentName = leaf.sessionName;
        }
        (*current)["leaves"].push_back(
            {{"leafId", leaf.leafId}, {"statementIndex", leaf.statementIndex}});
    }

    json j = {
        {"meta", meta},
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
        {"sessions", sessions},
    };
    return j.dump();
}

std::map<std::string, MapObjStat> MapDb::objectStats() const {
    std::map<std::string, MapObjStat> out;
    const char* sql = hasProfile_
        ? "SELECT name, pageCount, "
          "(SELECT COUNT(*) FROM pages p WHERE p.objectId=objects.id "
          " AND p.pageNumber IN (SELECT DISTINCT pageNumber FROM profile)) AS accessed "
          "FROM objects"
        : "SELECT name, pageCount, 0 AS accessed FROM objects";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (name == nullptr) continue;
        out[name] = MapObjStat{sqlite3_column_int64(stmt, 1), sqlite3_column_int64(stmt, 2)};
    }
    sqlite3_finalize(stmt);
    return out;
}

std::unordered_map<std::int64_t, std::int64_t> MapDb::rowidLeafPages(
    const std::string& tableName) const {
    std::unordered_map<std::int64_t, std::int64_t> out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            db_,
            // Table-interior cells also carry a rowid (the divider key), so the
            // page type must be constrained to leaf pages — the interior page is
            // a routing node, not where the row's data lives.
            "SELECT c.rowid, c.pageNumber FROM cells c "
            "JOIN pages p ON p.pageNumber = c.pageNumber "
            "WHERE c.rowid IS NOT NULL AND p.pageType = 'table-leaf' AND p.objectId = "
            "(SELECT id FROM objects WHERE name=? AND type='table')",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_text(stmt, 1, tableName.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out[sqlite3_column_int64(stmt, 0)] = sqlite3_column_int64(stmt, 1);
    }
    sqlite3_finalize(stmt);
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

std::string MapDb::runsJson(std::int64_t from, std::int64_t to, bool profiled,
                            const LeafFilter& sel) const {
    if (!profiled || !hasProfile_) {
        json j = {{"runs", queryRows(db_,
                                     "SELECT startPage,endPage,pageType,objectId FROM runs "
                                     "WHERE startPage <= ? AND endPage >= ? ORDER BY startPage",
                                     {to, from})}};
        return j.dump();
    }

    // Profile-filtered: a run is a contiguous span of same (pageType, objectId)
    // pages that the selected leaves accessed at least once. Accessed pages are
    // bounded by the profile, so we coalesce them in C++ then keep the runs that
    // overlap [from, to].
    const std::string sql =
        "SELECT p.pageNumber, p.pageType, p.objectId FROM pages p "
        "WHERE p.pageNumber IN (SELECT DISTINCT pageNumber FROM profile WHERE 1=1" +
        leafInClause(sel) + ") ORDER BY p.pageNumber";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        fail(std::string("runs query failed: ") + sqlite3_errmsg(db_));
    }
    std::vector<PageMeta> pages;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PageMeta m;
        m.pageNumber = sqlite3_column_int64(stmt, 0);
        m.pageType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            m.objectId = sqlite3_column_int64(stmt, 2);
        }
        pages.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);

    json runs = json::array();
    for (const CoalescedRun& r : coalesceRuns(pages)) {
        if (r.startPage > to || r.endPage < from) continue;
        runs.push_back({{"startPage", r.startPage},
                        {"endPage", r.endPage},
                        {"pageType", r.pageType},
                        {"objectId", r.objectId ? json(*r.objectId) : json(nullptr)}});
    }
    return json({{"runs", std::move(runs)}}).dump();
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
                           "overflowPage,keyJson FROM cells WHERE pageNumber=? "
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
            "FROM profile WHERE pageNumber=?" + leafInClause(sel),
            {pageNumber});
        j["profile"] = p.empty() ? json({{"reads", 0}, {"writes", 0}}) : p[0];
    }
    return j.dump();
}

std::string MapDb::profilePagesJson(std::int64_t from, std::int64_t to,
                                    const LeafFilter& sel) const {
    if (!hasProfile_) return R"({"pages":[]})";
    json j = {{"pages", queryRows(
                            db_,
                            "SELECT pageNumber, SUM(reads) AS reads, SUM(writes) AS writes "
                            "FROM profile WHERE pageNumber BETWEEN ? AND ?" +
                                leafInClause(sel) +
                                " GROUP BY pageNumber ORDER BY pageNumber",
                            {from, to})}};
    return j.dump();
}

namespace {
// A page's tree children are its b-tree children, its overflow pages, and (for a
// freelist trunk) its freelist-leaf pages. Interior freelist-next / ptrmap-parent
// edges are not tree edges.
constexpr const char* kChildKinds = "('child','overflow','freelist-leaf')";
}  // namespace

std::string MapDb::pageType(std::int64_t page) const {
    json r = queryRows(db_, "SELECT pageType FROM pages WHERE pageNumber=?", {page});
    if (r.empty() || !r[0]["pageType"].is_string()) return {};
    return r[0]["pageType"].get<std::string>();
}

std::string MapDb::treeRootsJson() const {
    json roots = json::array();

    // Page 1.
    {
        const std::string sql =
            "SELECT 1 AS page, pageType, "
            "EXISTS(SELECT 1 FROM pointers WHERE fromPage=1 AND kind IN " +
            std::string(kChildKinds) + ") AS hasChildren FROM pages WHERE pageNumber=1";
        json r = queryRows(db_, sql);
        if (!r.empty()) {
            roots.push_back({{"kind", "page"}, {"label", "Page 1"}, {"page", 1},
                             {"pageType", r[0]["pageType"]},
                             {"objectId", nullptr}, {"hasChildren", r[0]["hasChildren"]}});
        }
    }

    // Each table / index b-tree (tables first).
    json objs = queryRows(
        db_,
        "SELECT o.id AS objectId, o.type AS type, o.name AS name, o.rootPage AS page, "
        "(SELECT pageType FROM pages WHERE pageNumber=o.rootPage) AS pageType, "
        "EXISTS(SELECT 1 FROM pointers WHERE fromPage=o.rootPage AND kind IN " +
            std::string(kChildKinds) + ") AS hasChildren "
        // rootPage=1 is sqlite_schema, already represented by the Page 1 root.
        "FROM objects o WHERE o.type IN ('table','index') AND o.rootPage<>1 "
        "ORDER BY CASE o.type WHEN 'table' THEN 0 ELSE 1 END, o.name");
    for (json& o : objs) {
        roots.push_back({
            {"kind", "page"},
            {"label", o["name"].get<std::string>() + " (" + o["type"].get<std::string>() + ")"},
            {"page", o["page"]}, {"pageType", o["pageType"]},
            {"objectId", o["objectId"]}, {"hasChildren", o["hasChildren"]},
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

    // All other pages (virtual).
    roots.push_back({{"kind", "other"}, {"label", "All other pages"}, {"page", nullptr},
                     {"pageType", nullptr}, {"objectId", nullptr}, {"hasChildren", true}});

    return json{{"roots", std::move(roots)}}.dump();
}

std::string MapDb::treeChildrenJson(std::int64_t page) const {
    const std::string sql =
        "SELECT ptr.toPage AS page, ptr.kind AS kind, p.pageType AS pageType, "
        "p.objectId AS objectId, "
        "EXISTS(SELECT 1 FROM pointers c WHERE c.fromPage=ptr.toPage AND c.kind IN " +
            std::string(kChildKinds) + ") AS hasChildren "
        "FROM pointers ptr JOIN pages p ON p.pageNumber=ptr.toPage "
        "WHERE ptr.fromPage=? AND ptr.kind IN " + std::string(kChildKinds) + " "
        "ORDER BY CASE ptr.kind WHEN 'child' THEN 0 WHEN 'overflow' THEN 1 ELSE 2 END, ptr.toPage";
    return json{{"children", queryRows(db_, sql, {page})}}.dump();
}

std::string MapDb::treeFreelistJson(std::int64_t after, std::int64_t limit) const {
    const std::string sql =
        "SELECT pageNumber AS page, pageType, "
        "EXISTS(SELECT 1 FROM pointers WHERE fromPage=pages.pageNumber AND kind='freelist-leaf') "
        "AS hasChildren "
        "FROM pages WHERE pageType='freelist-trunk' AND pageNumber>? "
        "ORDER BY pageNumber LIMIT ?";
    return json{{"pages", queryRows(db_, sql, {after, limit})}}.dump();
}

std::string MapDb::treeOtherJson(std::int64_t after, std::int64_t limit) const {
    const std::string sql =
        "SELECT pageNumber AS page, pageType, objectId, "
        "EXISTS(SELECT 1 FROM pointers WHERE fromPage=pages.pageNumber AND kind IN " +
            std::string(kChildKinds) + ") AS hasChildren "
        "FROM pages "
        "WHERE pageNumber>1 AND pageNumber>? "
        "AND pageType NOT IN ('freelist-trunk','freelist-leaf') "
        "AND pageNumber NOT IN (SELECT rootPage FROM objects) "
        "AND pageNumber NOT IN (SELECT toPage FROM pointers) "
        "ORDER BY pageNumber LIMIT ?";
    return json{{"pages", queryRows(db_, sql, {after, limit})}}.dump();
}
