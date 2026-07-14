#include "map/map_builder.hpp"

#include <map>
#include <stdexcept>
#include <vector>

#include <sqlite3.h>

#include "map/db_file.hpp"
#include "map/map_writer.hpp"
#include "map/page_parser.hpp"

std::string pageTypeName(PageType type) {
    switch (type) {
        case PageType::TableLeaf: return "table-leaf";
        case PageType::TableInterior: return "table-interior";
        case PageType::IndexLeaf: return "index-leaf";
        case PageType::IndexInterior: return "index-interior";
        case PageType::Overflow: return "overflow";
        case PageType::FreelistTrunk: return "freelist-trunk";
        case PageType::FreelistLeaf: return "freelist-leaf";
        case PageType::PointerMap: return "pointer-map";
        case PageType::LockByte: return "lock-byte";
        case PageType::Unallocated: return "unallocated";
    }
    return "unallocated";
}

namespace {

struct SchemaObject {
    std::string type, name, tableName, sql;
    std::int64_t rootPage = 0;
};

std::vector<SchemaObject> readSchema(const std::string& path) {
    std::vector<SchemaObject> objects;
    objects.push_back({"table", "sqlite_schema", "sqlite_schema", "", 1});

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        sqlite3_close(db);
        throw std::runtime_error("map: cannot open schema: " +
                                 std::string(sqlite3_errmsg(db)));
    }
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT type, name, tbl_name, rootpage, COALESCE(sql,'') "
        "FROM sqlite_schema WHERE rootpage IS NOT NULL AND rootpage > 0";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SchemaObject o;
            o.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            o.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            o.tableName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            o.rootPage = sqlite3_column_int64(stmt, 3);
            o.sql = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            objects.push_back(std::move(o));
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return objects;
}

std::vector<std::int64_t> pointerMapPages(const DbFile& db) {
    std::vector<std::int64_t> result;
    if (!db.autoVacuum()) return result;
    const std::int64_t perPage = db.usableSize() / 5;
    if (perPage <= 0) return result;
    const std::int64_t n = db.pageCount();
    for (std::int64_t p = 2; p <= n; p += perPage + 1) result.push_back(p);
    return result;
}

std::int64_t lockBytePage(const DbFile& db) {
    constexpr std::int64_t kLockByteOffset = 1073741824;  // 2^30
    if (static_cast<std::int64_t>(db.pageSize()) * db.pageCount() <=
        kLockByteOffset) {
        return 0;
    }
    return kLockByteOffset / db.pageSize() + 1;
}

// Per-page classification: type and owning object index (-1 == none).
struct Classification {
    std::vector<PageType> type;
    std::vector<std::int64_t> objectId;
};

Classification classify(const DbFile& db,
                        const std::vector<SchemaObject>& objects) {
    const std::int64_t n = db.pageCount();
    Classification cls;
    cls.type.assign(static_cast<std::size_t>(n), PageType::Unallocated);
    cls.objectId.assign(static_cast<std::size_t>(n), -1);
    std::vector<bool> assigned(static_cast<std::size_t>(n), false);

    auto set = [&](std::int64_t page, PageType t, std::int64_t obj) {
        const std::size_t idx = static_cast<std::size_t>(page - 1);
        cls.type[idx] = t;
        cls.objectId[idx] = obj;
        assigned[idx] = true;
    };

    for (std::int64_t p : pointerMapPages(db)) set(p, PageType::PointerMap, -1);
    if (const std::int64_t lb = lockBytePage(db); lb >= 1 && lb <= n) {
        set(lb, PageType::LockByte, -1);
    }

    auto followOverflow = [&](std::int64_t start, std::int64_t obj) {
        std::int64_t pg = start;
        int guard = 0;
        while (pg >= 1 && pg <= n && !assigned[static_cast<std::size_t>(pg - 1)] &&
               guard++ < n) {
            PageInfo ov = page_parser::parseOverflow(db, pg);
            set(pg, PageType::Overflow, obj);
            pg = ov.header.nextOverflowPage ? *ov.header.nextOverflowPage : 0;
        }
    };

    for (std::size_t oi = 0; oi < objects.size(); ++oi) {
        const std::int64_t root = objects[oi].rootPage;
        if (root < 1 || root > n) continue;
        std::vector<std::int64_t> stack{root};
        int guard = 0;
        while (!stack.empty() && guard++ < 4 * n + 16) {
            const std::int64_t pg = stack.back();
            stack.pop_back();
            if (pg < 1 || pg > n) continue;
            if (assigned[static_cast<std::size_t>(pg - 1)]) continue;

            PageInfo pi = page_parser::parseBtree(db, pg);
            set(pg, pi.type, static_cast<std::int64_t>(oi));
            for (const Pointer& ptr : pi.pointers) {
                if (ptr.kind == "child") {
                    stack.push_back(ptr.toPage);
                } else if (ptr.kind == "overflow") {
                    followOverflow(ptr.toPage, static_cast<std::int64_t>(oi));
                }
            }
        }
    }

    std::int64_t trunk = db.header().freelistTrunkPage;
    int trunkGuard = 0;
    while (trunk >= 1 && trunk <= n &&
           !assigned[static_cast<std::size_t>(trunk - 1)] && trunkGuard++ < n) {
        PageInfo t = page_parser::parseFreelistTrunk(db, trunk);
        const std::int64_t next =
            t.header.nextTrunkPage ? *t.header.nextTrunkPage : 0;
        set(trunk, PageType::FreelistTrunk, -1);
        for (const Pointer& ptr : t.pointers) {
            if (ptr.kind == "freelist-leaf" && ptr.toPage >= 1 && ptr.toPage <= n &&
                !assigned[static_cast<std::size_t>(ptr.toPage - 1)]) {
                set(ptr.toPage, PageType::FreelistLeaf, -1);
            }
        }
        trunk = next;
    }

    return cls;
}

PageInfo parseByType(const DbFile& db, std::int64_t page, PageType type) {
    switch (type) {
        case PageType::TableLeaf:
        case PageType::TableInterior:
        case PageType::IndexLeaf:
        case PageType::IndexInterior:
            return page_parser::parseBtree(db, page);
        case PageType::Overflow:
            return page_parser::parseOverflow(db, page);
        case PageType::FreelistTrunk:
            return page_parser::parseFreelistTrunk(db, page);
        case PageType::PointerMap:
            return page_parser::parsePointerMap(db, page);
        default:
            return page_parser::parseSimple(page, type);
    }
}

}  // namespace

void writeMap(const std::string& sourcePath, const std::string& outPath) {
    DbFile db = DbFile::open(sourcePath);
    const std::int64_t n = db.pageCount();
    const std::vector<SchemaObject> objects = readSchema(sourcePath);
    const Classification cls = classify(db, objects);

    MapWriter writer(outPath);

    std::map<std::string, std::int64_t> typeCounts;
    std::vector<std::int64_t> objectPageCount(objects.size(), 0);

    // Open run accumulator.
    std::int64_t runStart = 0, runEnd = 0;
    PageType runType = PageType::Unallocated;
    std::int64_t runObject = -1;
    auto flushRun = [&] {
        if (runStart != 0) {
            writer.writeRun(runStart, runEnd, pageTypeName(runType), runObject);
        }
    };

    for (std::int64_t page = 1; page <= n; ++page) {
        const std::size_t idx = static_cast<std::size_t>(page - 1);
        const PageType type = cls.type[idx];
        const std::int64_t obj = cls.objectId[idx];

        PageInfo pi = parseByType(db, page, type);
        pi.type = type;  // authoritative classification
        writer.writePage(pi, obj);

        typeCounts[pageTypeName(type)]++;
        if (obj >= 0) ++objectPageCount[static_cast<std::size_t>(obj)];

        if (runStart != 0 && type == runType && obj == runObject) {
            runEnd = page;
        } else {
            flushRun();
            runStart = page;
            runEnd = page;
            runType = type;
            runObject = obj;
        }
    }
    flushRun();

    for (std::size_t oi = 0; oi < objects.size(); ++oi) {
        const SchemaObject& o = objects[oi];
        writer.writeObject({static_cast<std::int64_t>(oi), o.type, o.name,
                            o.tableName, o.rootPage, o.sql,
                            objectPageCount[oi]});
    }
    for (const auto& [type, count] : typeCounts) {
        writer.writeTypeCount(type, count);
    }
    writer.writeMeta(db.header(), sourcePath, n);

    // Precompute per-interior-page rowid runs from the written cells/pointers.
    writer.writeRowRuns(n);

    writer.commit();
}
