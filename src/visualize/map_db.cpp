#include "visualize/map_db.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "map/map_writer.hpp"
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

MapDb::MapDb(const std::string& mapPath) : mapPath_(mapPath) {
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
        {"sessions", sessions},
    };
    return j.dump();
}

std::unordered_map<std::int64_t, std::int64_t> MapDb::leafPagesForRowids(
    const std::string& tableName, const std::vector<std::int64_t>& rowids) const {
    std::unordered_map<std::int64_t, std::int64_t> out;
    if (rowids.empty()) return out;

    // Use a short-lived private connection so the temp table + insert transaction
    // never touch the shared db_ (which other requests use concurrently). The main
    // db is read-only; temp storage is still writable.
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(mapPath_.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return out;
    }
    out.reserve(rowids.size());

    // Load the wanted rowids into a temp table, then resolve them all in one query.
    // The CROSS JOINs force the join order want → cells → pages, so each rowid is a
    // point lookup (cells_rowid, then pages' INTEGER PRIMARY KEY) rather than a
    // per-rowid scan of the table's pages.
    sqlite3_exec(db, "CREATE TEMP TABLE want(rowid INTEGER PRIMARY KEY)", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO want(rowid) VALUES(?1)", -1, &ins, nullptr) ==
        SQLITE_OK) {
        for (const std::int64_t rid : rowids) {
            sqlite3_bind_int64(ins, 1, rid);
            sqlite3_step(ins);
            sqlite3_reset(ins);
        }
    }
    sqlite3_finalize(ins);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    sqlite3_stmt* sel = nullptr;
    if (sqlite3_prepare_v2(
            db,
            "SELECT c.rowid, c.pageNumber FROM want w "
            "CROSS JOIN cells c ON c.rowid = w.rowid "
            "CROSS JOIN pages p ON p.pageNumber = c.pageNumber "
            "WHERE p.pageType = 'table-leaf' AND p.objectId = "
            "(SELECT id FROM objects WHERE name = ?1 AND type = 'table')",
            -1, &sel, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(sel, 1, tableName.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(sel) == SQLITE_ROW) {
            out[sqlite3_column_int64(sel, 0)] = sqlite3_column_int64(sel, 1);
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

    // All other pages (virtual) — only when at least one such page exists.
    json other = queryRows(
        db_,
        "SELECT EXISTS(SELECT 1 FROM pages WHERE pageNumber>1 "
        "AND pageType NOT IN ('freelist-trunk','freelist-leaf','lock-byte') "
        "AND pageNumber NOT IN (SELECT rootPage FROM objects) "
        "AND pageNumber NOT IN (SELECT toPage FROM pointers)) AS ex");
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
        "AND pageType NOT IN ('freelist-trunk','freelist-leaf','lock-byte') "
        "AND pageNumber NOT IN (SELECT rootPage FROM objects) "
        "AND pageNumber NOT IN (SELECT toPage FROM pointers) "
        "ORDER BY pageNumber LIMIT ?";
    return json{{"pages", queryRows(db_, sql, {after, limit})}}.dump();
}
