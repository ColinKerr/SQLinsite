#include "map/map_builder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
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
// `btree[i]` marks a b-tree object page whose exact leaf/interior/table/index
// subtype is left for the main parse pass to fill in (see classify) — this
// avoids a second per-page dbpage read here just to sniff the header byte.
struct Classification {
    std::vector<PageType> type;
    std::vector<std::int64_t> objectId;
    std::vector<char> btree;
};

Classification classify(const DbFile& db,
                        const std::vector<SchemaObject>& objects,
                        const std::string& sourcePath) {
    const std::int64_t n = db.pageCount();
    Classification cls;
    cls.type.assign(static_cast<std::size_t>(n), PageType::Unallocated);
    cls.objectId.assign(static_cast<std::size_t>(n), -1);
    cls.btree.assign(static_cast<std::size_t>(n), 0);
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

    // Assign every b-tree page (table/index/overflow) to its object using the DBSTAT
    // virtual table, which walks all b-trees in C and names each page's object — far
    // cheaper than a C++ tree traversal that parses every page just to follow
    // pointers. DBSTAT omits freelist/pointer-map/lock-byte pages (handled above and
    // below). The interior/leaf/table/index distinction comes from each page's own
    // header byte (authoritative for WITHOUT ROWID tables); overflow pages are named
    // by DBSTAT directly.
    std::unordered_map<std::string, std::int64_t> nameToOi;
    for (std::size_t oi = 0; oi < objects.size(); ++oi)
        nameToOi.emplace(objects[oi].name, static_cast<std::int64_t>(oi));
    sqlite3* sdb = nullptr;
    if (sqlite3_open_v2(sourcePath.c_str(), &sdb, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(sdb, "SELECT pageno, name, pagetype FROM dbstat", -1, &st,
                               nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const std::int64_t pg = sqlite3_column_int64(st, 0);
                if (pg < 1 || pg > n || assigned[static_cast<std::size_t>(pg - 1)]) continue;
                const auto* nm = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
                const auto* pt = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
                auto it = nameToOi.find(nm ? nm : "");
                if (it == nameToOi.end()) continue;
                const bool overflow = pt && std::string(pt) == "overflow";
                if (overflow) {
                    set(pg, PageType::Overflow, it->second);
                } else {
                    // A b-tree object page: record its object now, but defer the
                    // exact leaf/interior/table/index subtype to the parse pass,
                    // which reads the page's header byte anyway (no extra read).
                    set(pg, PageType::Unallocated, it->second);
                    cls.btree[static_cast<std::size_t>(pg - 1)] = 1;
                }
            }
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(sdb);

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

using RowRun = std::pair<std::int64_t, std::int64_t>;  // inclusive [lo, hi]

// Collapses ascending, disjoint rowids/runs into maximal contiguous runs (rowids
// have gaps from deletions). Input runs must be sorted by lo; adjacent runs
// (prev.hi + 1 >= next.lo) are merged.
std::vector<RowRun> mergeRuns(std::vector<RowRun> runs) {
    std::sort(runs.begin(), runs.end());
    std::vector<RowRun> out;
    for (const RowRun& r : runs) {
        if (!out.empty() && r.first <= out.back().second + 1) {
            out.back().second = std::max(out.back().second, r.second);
        } else {
            out.push_back(r);
        }
    }
    return out;
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

// Env-gated phase timing (SQLINSITE_MAP_TIMING=1) — prints wall time per phase to
// stderr. Zero cost when disabled.
struct PhaseTimer {
    bool on = std::getenv("SQLINSITE_MAP_TIMING") != nullptr;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    void mark(const char* label) {
        if (!on) return;
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - last).count();
        std::fprintf(stderr, "[map-timing] %-22s %8.1f ms\n", label, ms);
        last = now;
    }
};

}  // namespace

void writeMap(const std::string& sourcePath, const std::string& outPath) {
    PhaseTimer timer;
    DbFile db = DbFile::open(sourcePath);
    const std::int64_t n = db.pageCount();
    timer.mark("open");
    const std::vector<SchemaObject> objects = readSchema(sourcePath);
    timer.mark("readSchema");
    Classification cls = classify(db, objects, sourcePath);
    timer.mark("classify");

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

    // page_row_runs inputs, collected during the single parse pass (see below):
    // each table-leaf page's own rowid runs, and each table-interior page's child
    // pages (in b-tree order). Interior subtree runs are merged from these after.
    std::unordered_map<std::int64_t, std::vector<RowRun>> pageRuns;   // leaf runs
    std::unordered_map<std::int64_t, std::vector<std::int64_t>> interiorChildren;

    // Subtree edges (child/overflow/freelist-leaf) of every page, for computing
    // subtreePageCount in C++ (below) instead of a whole-file recursive CTE.
    std::vector<std::vector<std::int64_t>> subtreeAdj(static_cast<std::size_t>(n) + 1);

    for (std::int64_t page = 1; page <= n; ++page) {
        const std::size_t idx = static_cast<std::size_t>(page - 1);
        const std::int64_t obj = cls.objectId[idx];

        PageType type;
        PageInfo pi;
        if (cls.btree[idx]) {
            // Deferred b-tree page: parseBtree reads the header byte and sets the
            // exact subtype (authoritative even for WITHOUT ROWID tables).
            pi = page_parser::parseBtree(db, page);
            type = pi.type;
            cls.type[idx] = type;  // fill in for the row-run/subtree passes below
        } else {
            type = cls.type[idx];
            pi = parseByType(db, page, type);
            pi.type = type;  // authoritative classification
        }
        writer.writePage(pi, obj);

        for (const Pointer& ptr : pi.pointers)
            if (ptr.kind == "child" || ptr.kind == "overflow" || ptr.kind == "freelist-leaf")
                subtreeAdj[static_cast<std::size_t>(page)].push_back(ptr.toPage);

        if (type == PageType::TableLeaf) {
            // Cells are in ascending rowid order; collapse them into runs.
            std::vector<RowRun> runs;
            for (const CellInfo& c : pi.cells) {
                if (!c.rowid) continue;
                const std::int64_t rid = *c.rowid;
                if (!runs.empty() && rid == runs.back().second + 1) runs.back().second = rid;
                else runs.push_back({rid, rid});
            }
            if (!runs.empty()) pageRuns.emplace(page, std::move(runs));
        } else if (type == PageType::TableInterior) {
            std::vector<std::int64_t>& kids = interiorChildren[page];
            for (const Pointer& ptr : pi.pointers)
                if (ptr.kind == "child") kids.push_back(ptr.toPage);
        }

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
    timer.mark("parse+write loop");

    // Roll leaf runs up to every table-interior page: an interior page's subtree
    // runs are the merged runs of all its descendant leaves. Computed bottom-up and
    // memoized in `pageRuns`; `visiting` guards against a malformed cyclic map.
    std::unordered_set<std::int64_t> visiting;
    std::function<const std::vector<RowRun>&(std::int64_t)> subtreeRuns =
        [&](std::int64_t pg) -> const std::vector<RowRun>& {
        auto it = pageRuns.find(pg);
        if (it != pageRuns.end()) return it->second;          // leaf, or already merged
        static const std::vector<RowRun> kEmpty;
        auto ci = interiorChildren.find(pg);
        if (ci == interiorChildren.end() || !visiting.insert(pg).second) return kEmpty;
        std::vector<RowRun> gathered;
        for (const std::int64_t child : ci->second) {
            const std::vector<RowRun>& cr = subtreeRuns(child);
            gathered.insert(gathered.end(), cr.begin(), cr.end());
        }
        visiting.erase(pg);
        return pageRuns.emplace(pg, mergeRuns(std::move(gathered))).first->second;
    };
    for (const auto& [interior, kids] : interiorChildren) {
        (void)kids;
        subtreeRuns(interior);
    }
    for (const auto& [pg, runs] : pageRuns) {
        const std::size_t idx = static_cast<std::size_t>(pg - 1);
        const std::int64_t obj = cls.objectId[idx];
        const bool isLeaf = cls.type[idx] == PageType::TableLeaf;
        for (const RowRun& r : runs) writer.writeRowRun(pg, r.first, r.second, obj, isLeaf);
    }
    timer.mark("rowruns rollup+write");

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
    timer.mark("objects+meta");

    // Each page's subtreePageCount = 1 + Σ its subtree children's counts (the tree's
    // child/overflow/freelist-leaf edges). Computed bottom-up with an explicit stack
    // (iterative post-order so a long overflow chain can't overflow the C++ stack);
    // `state` marks new/on-stack/done and guards against a malformed cyclic map.
    // Replaces a whole-file recursive CTE that thrashed on very large databases.
    std::vector<std::int64_t> subtreeCount(static_cast<std::size_t>(n) + 1, 0);
    std::vector<std::uint8_t> state(static_cast<std::size_t>(n) + 1, 0);  // 0 new,1 open,2 done
    std::vector<std::int64_t> stack;
    for (std::int64_t root = 1; root <= n; ++root) {
        if (state[static_cast<std::size_t>(root)] != 0) continue;
        stack.push_back(root);
        while (!stack.empty()) {
            const std::int64_t pg = stack.back();
            std::uint8_t& st = state[static_cast<std::size_t>(pg)];
            const std::vector<std::int64_t>& kids = subtreeAdj[static_cast<std::size_t>(pg)];
            if (st == 0) {
                st = 1;
                for (const std::int64_t to : kids)
                    if (to >= 1 && to <= n && state[static_cast<std::size_t>(to)] == 0)
                        stack.push_back(to);
            } else {
                if (st == 1) {
                    std::int64_t c = 1;
                    for (const std::int64_t to : kids)
                        if (to >= 1 && to <= n && state[static_cast<std::size_t>(to)] == 2)
                            c += subtreeCount[static_cast<std::size_t>(to)];
                    subtreeCount[static_cast<std::size_t>(pg)] = c;
                    st = 2;
                }
                stack.pop_back();
            }
        }
    }
    timer.mark("subtree count compute");
    for (std::int64_t pg = 1; pg <= n; ++pg)
        writer.writeSubtreeCount(pg, subtreeCount[static_cast<std::size_t>(pg)]);
    timer.mark("subtree count write");

    writer.commit();
    timer.mark("commit");
}
