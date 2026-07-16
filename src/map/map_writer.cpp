#include "map/map_writer.hpp"

#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace {

// Mirrors plan/commands/map.schema.sql.
constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE meta (
  formatVersion INTEGER, path TEXT, pageSize INTEGER, pageCount INTEGER,
  textEncoding TEXT, writeVersion TEXT, readVersion TEXT,
  reservedBytesPerPage INTEGER, autoVacuum TEXT,
  freelistPageCount INTEGER, freelistTrunkPage INTEGER,
  schemaCookie INTEGER, sqliteVersionNumber INTEGER);
CREATE TABLE objects (
  id INTEGER PRIMARY KEY, type TEXT, name TEXT, tableName TEXT,
  rootPage INTEGER, sql TEXT, pageCount INTEGER);
CREATE TABLE pages (
  pageNumber INTEGER PRIMARY KEY, pageType TEXT, objectId INTEGER,
  freeBytes INTEGER, cellCount INTEGER,
  firstFreeblock INTEGER, cellContentStart INTEGER, fragmentedFreeBytes INTEGER,
  rightmostPointer INTEGER, parseError TEXT, subtreePageCount INTEGER);
CREATE INDEX pages_object ON pages(objectId);
CREATE TABLE cells (
  pageNumber INTEGER, cellIndex INTEGER, rowid INTEGER, leftChild INTEGER,
  payloadBytes INTEGER, localBytes INTEGER, overflowPage INTEGER, keyJson TEXT,
  PRIMARY KEY (pageNumber, cellIndex)) WITHOUT ROWID;
CREATE INDEX cells_rowid ON cells(rowid) WHERE rowid IS NOT NULL;
CREATE INDEX cells_leftChild ON cells(leftChild);
CREATE TABLE pointers (fromPage INTEGER, toPage INTEGER, kind TEXT);
CREATE INDEX pointers_from ON pointers(fromPage);
CREATE INDEX pointers_to ON pointers(toPage);
CREATE TABLE page_row_runs (
  parentPageNumber INTEGER, startRowId INTEGER, endRowId INTEGER, rowCount INTEGER);
CREATE INDEX page_row_runs_parent ON page_row_runs(parentPageNumber);
CREATE TABLE ptrmap (
  pageNumber INTEGER, targetPage INTEGER, entryType INTEGER, parentPage INTEGER,
  PRIMARY KEY (pageNumber, targetPage)) WITHOUT ROWID;
CREATE TABLE runs (
  startPage INTEGER, endPage INTEGER, pageType TEXT, objectId INTEGER);
CREATE INDEX runs_start ON runs(startPage);
CREATE TABLE type_counts (pageType TEXT PRIMARY KEY, count INTEGER);
)SQL";

[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    throw std::runtime_error("map: " + what + ": " +
                             (db ? sqlite3_errmsg(db) : "?"));
}

sqlite3_stmt* prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fail(db, "prepare");
    }
    return stmt;
}

void bindOptInt(sqlite3_stmt* s, int idx, const std::optional<std::int64_t>& v) {
    if (v) {
        sqlite3_bind_int64(s, idx, *v);
    } else {
        sqlite3_bind_null(s, idx);
    }
}

void bindOptInt(sqlite3_stmt* s, int idx, const std::optional<int>& v) {
    if (v) {
        sqlite3_bind_int64(s, idx, *v);
    } else {
        sqlite3_bind_null(s, idx);
    }
}

void runStep(sqlite3* db, sqlite3_stmt* s) {
    if (sqlite3_step(s) != SQLITE_DONE) {
        fail(db, "insert");
    }
    sqlite3_reset(s);
}

std::string textEncodingName(int code) {
    switch (code) {
        case 1: return "utf-8";
        case 2: return "utf-16le";
        case 3: return "utf-16be";
        default: return "unknown";
    }
}

std::string fileVersionName(int code) { return code == 2 ? "wal" : "legacy"; }

std::string autoVacuumName(const DbHeader& h) {
    if (h.largestRootBtreePage == 0) return "none";
    return h.incrementalVacuumFlag != 0 ? "incremental" : "full";
}

std::string keyJsonFor(const CellInfo& c) {
    if (c.key.empty()) return {};
    nlohmann::json arr = nlohmann::json::array();
    for (const sqlfmt::CellValue& v : c.key) {
        switch (v.type) {
            case sqlfmt::CellValue::Type::Null: arr.push_back(nullptr); break;
            case sqlfmt::CellValue::Type::Int: arr.push_back(v.intValue); break;
            case sqlfmt::CellValue::Type::Real: arr.push_back(v.realValue); break;
            case sqlfmt::CellValue::Type::Text:
                arr.push_back({{"text", v.text}, {"bytes", v.byteSize},
                               {"truncated", v.truncated}});
                break;
            case sqlfmt::CellValue::Type::Blob:
                arr.push_back({{"blob", v.byteSize}, {"truncated", v.truncated}});
                break;
        }
    }
    return arr.dump();
}

}  // namespace

MapWriter::MapWriter(const std::string& path) {
    std::remove(path.c_str());
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        fail(db_, "cannot create " + path);
    }
    sqlite3_exec(db_, "PRAGMA synchronous=OFF; PRAGMA journal_mode=MEMORY;",
                 nullptr, nullptr, nullptr);
    if (sqlite3_exec(db_, kSchemaSql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, "create schema");
    }
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);

    meta_ = prepare(db_,
        "INSERT INTO meta VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    object_ = prepare(db_, "INSERT INTO objects VALUES (?,?,?,?,?,?,?)");
    // subtreePageCount is filled by a finalization pass (writeSubtreeCounts).
    page_ = prepare(db_,
                    "INSERT INTO pages(pageNumber,pageType,objectId,freeBytes,cellCount,"
                    "firstFreeblock,cellContentStart,fragmentedFreeBytes,rightmostPointer,"
                    "parseError) VALUES (?,?,?,?,?,?,?,?,?,?)");
    cell_ = prepare(db_, "INSERT INTO cells VALUES (?,?,?,?,?,?,?,?)");
    pointer_ = prepare(db_, "INSERT INTO pointers VALUES (?,?,?)");
    ptrmap_ = prepare(db_, "INSERT INTO ptrmap VALUES (?,?,?,?)");
    run_ = prepare(db_, "INSERT INTO runs VALUES (?,?,?,?)");
    typeCount_ = prepare(db_, "INSERT INTO type_counts VALUES (?,?)");
}

MapWriter::~MapWriter() {
    for (sqlite3_stmt* s : {meta_, object_, page_, cell_, pointer_, ptrmap_,
                            run_, typeCount_}) {
        sqlite3_finalize(s);
    }
    if (db_ && !committed_) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    sqlite3_close(db_);
}

void MapWriter::writeMeta(const DbHeader& h, const std::string& sourcePath,
                          std::int64_t pageCount) {
    sqlite3_bind_int64(meta_, 1, kFormatVersion);  // formatVersion
    sqlite3_bind_text(meta_, 2, sourcePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(meta_, 3, h.pageSize);
    sqlite3_bind_int64(meta_, 4, pageCount);
    const std::string enc = textEncodingName(h.textEncoding);
    const std::string wv = fileVersionName(h.writeVersion);
    const std::string rv = fileVersionName(h.readVersion);
    const std::string av = autoVacuumName(h);
    sqlite3_bind_text(meta_, 5, enc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(meta_, 6, wv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(meta_, 7, rv.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(meta_, 8, h.reservedBytesPerPage);
    sqlite3_bind_text(meta_, 9, av.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(meta_, 10, h.freelistPageCount);
    if (h.freelistTrunkPage != 0) {
        sqlite3_bind_int64(meta_, 11, h.freelistTrunkPage);
    } else {
        sqlite3_bind_null(meta_, 11);
    }
    sqlite3_bind_int64(meta_, 12, h.schemaCookie);
    sqlite3_bind_int64(meta_, 13, h.sqliteVersionNumber);
    runStep(db_, meta_);
}

void MapWriter::writeObject(const ObjectRow& o) {
    sqlite3_bind_int64(object_, 1, o.id);
    sqlite3_bind_text(object_, 2, o.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(object_, 3, o.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(object_, 4, o.tableName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(object_, 5, o.rootPage);
    sqlite3_bind_text(object_, 6, o.sql.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(object_, 7, o.pageCount);
    runStep(db_, object_);
}

void MapWriter::writePage(const PageInfo& p, std::int64_t objectId) {
    sqlite3_bind_int64(page_, 1, p.pageNumber);
    const std::string type = pageTypeName(p.type);
    sqlite3_bind_text(page_, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    if (objectId >= 0) {
        sqlite3_bind_int64(page_, 3, objectId);
    } else {
        sqlite3_bind_null(page_, 3);
    }
    sqlite3_bind_int64(page_, 4, p.freeBytes);
    bindOptInt(page_, 5, p.header.cellCount);
    bindOptInt(page_, 6, p.header.firstFreeblock);
    bindOptInt(page_, 7, p.header.cellContentStart);
    bindOptInt(page_, 8, p.header.fragmentedFreeBytes);
    bindOptInt(page_, 9, p.header.rightmostPointer);
    if (p.parseError) {
        sqlite3_bind_text(page_, 10, p.parseError->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(page_, 10);
    }
    runStep(db_, page_);

    for (std::size_t i = 0; i < p.cells.size(); ++i) {
        const CellInfo& c = p.cells[i];
        sqlite3_bind_int64(cell_, 1, p.pageNumber);
        sqlite3_bind_int64(cell_, 2, static_cast<std::int64_t>(i));
        bindOptInt(cell_, 3, c.rowid);
        bindOptInt(cell_, 4, c.leftChild);
        sqlite3_bind_int64(cell_, 5, c.payloadBytes);
        sqlite3_bind_int64(cell_, 6, c.localBytes);
        bindOptInt(cell_, 7, c.overflowPage);
        const std::string keyJson = keyJsonFor(c);
        if (!keyJson.empty()) {
            sqlite3_bind_text(cell_, 8, keyJson.c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(cell_, 8);
        }
        runStep(db_, cell_);
    }

    for (const Pointer& ptr : p.pointers) {
        sqlite3_bind_int64(pointer_, 1, p.pageNumber);
        sqlite3_bind_int64(pointer_, 2, ptr.toPage);
        sqlite3_bind_text(pointer_, 3, ptr.kind.c_str(), -1, SQLITE_TRANSIENT);
        runStep(db_, pointer_);
    }

    for (const PtrmapEntry& e : p.ptrmapEntries) {
        sqlite3_bind_int64(ptrmap_, 1, p.pageNumber);
        sqlite3_bind_int64(ptrmap_, 2, e.targetPage);
        sqlite3_bind_int64(ptrmap_, 3, e.entryType);
        sqlite3_bind_int64(ptrmap_, 4, e.parentPage);
        runStep(db_, ptrmap_);
    }
}

void MapWriter::writeRun(std::int64_t startPage, std::int64_t endPage,
                         const std::string& pageType, std::int64_t objectId) {
    sqlite3_bind_int64(run_, 1, startPage);
    sqlite3_bind_int64(run_, 2, endPage);
    sqlite3_bind_text(run_, 3, pageType.c_str(), -1, SQLITE_TRANSIENT);
    if (objectId >= 0) {
        sqlite3_bind_int64(run_, 4, objectId);
    } else {
        sqlite3_bind_null(run_, 4);
    }
    runStep(db_, run_);
}

void MapWriter::writeTypeCount(const std::string& pageType, std::int64_t count) {
    sqlite3_bind_text(typeCount_, 1, pageType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(typeCount_, 2, count);
    runStep(db_, typeCount_);
}

void MapWriter::writeRowRuns(std::int64_t pageCount) {
    // For every table b-tree page — interior AND leaf — the maximal contiguous
    // rowid runs of the rows in its subtree. A table-leaf page is its own subtree
    // (just its own rowids); an interior page fans out to its descendant leaves.
    // Including leaf pages lets a consumer look up any child page uniformly (the
    // "last interior page before the leaves" resolves its leaf children with the
    // same query as any higher interior page). `reach` seeds every table page and
    // follows child pointers; the leaf descendants' rowids are collapsed into runs
    // by a gaps-and-islands GROUP BY. Runs are compact even when rowids have gaps.
    const std::string sql =
        "INSERT INTO page_row_runs(parentPageNumber, startRowId, endRowId, rowCount) "
        "WITH RECURSIVE "
        "reach(root, pg, depth) AS ("
        " SELECT pageNumber, pageNumber, 0 FROM pages WHERE pageType IN ('table-interior','table-leaf')"
        " UNION ALL"
        " SELECT reach.root, ptr.toPage, reach.depth+1 FROM reach"
        "  JOIN pointers ptr ON ptr.fromPage=reach.pg AND ptr.kind='child'"
        "  WHERE reach.depth < " + std::to_string(pageCount + 1) + "), "
        "leaf_rids(root, rid) AS ("
        " SELECT reach.root, c.rowid FROM reach"
        "  JOIN pages p ON p.pageNumber=reach.pg AND p.pageType='table-leaf'"
        "  JOIN cells c ON c.pageNumber=reach.pg WHERE c.rowid IS NOT NULL), "
        "isl AS ("
        " SELECT root, rid, rid - ROW_NUMBER() OVER (PARTITION BY root ORDER BY rid) AS grp"
        "  FROM leaf_rids) "
        "SELECT root, MIN(rid), MAX(rid), MAX(rid)-MIN(rid)+1 FROM isl GROUP BY root, grp";
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, "write page_row_runs");
    }
}

void MapWriter::writeSubtreeCounts(std::int64_t /*pageCount*/) {
    // For every page, the number of pages in its subtree (itself plus all pages
    // reachable through the tree's child/overflow/freelist-leaf edges — the same
    // edges treeChildren follows). `reach` seeds every page as a root and follows
    // those edges; UNION dedupes (root, page) pairs so the count is distinct pages
    // and the recursion terminates even on a malformed cyclic map. Counts are
    // aggregated once into a temp table (root primary-keyed) so the per-page UPDATE
    // is a single indexed lookup.
    const char* sql =
        "CREATE TEMP TABLE subtree_counts(root INTEGER PRIMARY KEY, c INTEGER);"
        "INSERT INTO subtree_counts(root, c) "
        "WITH RECURSIVE reach(root, pg) AS ("
        " SELECT pageNumber, pageNumber FROM pages"
        " UNION"
        " SELECT reach.root, ptr.toPage FROM reach"
        "  JOIN pointers ptr ON ptr.fromPage=reach.pg"
        "   AND ptr.kind IN ('child','overflow','freelist-leaf')) "
        "SELECT root, COUNT(*) FROM reach GROUP BY root;"
        "UPDATE pages SET subtreePageCount ="
        " (SELECT c FROM subtree_counts WHERE root=pages.pageNumber);"
        "DROP TABLE subtree_counts;";
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, "write subtreePageCount");
    }
}

void MapWriter::commit() {
    if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        fail(db_, "commit");
    }
    committed_ = true;
}
