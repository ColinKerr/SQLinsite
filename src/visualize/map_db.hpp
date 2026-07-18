#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "visualize/profile_reader.hpp"

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

    // Reported in /api/meta so the front-end can enable the live Query view.
    void setHasDb(bool v) { hasDb_ = v; }

    // Resolves the table-leaf page holding each of `rowids` for the named table,
    // as a rowid → page map (rowids not in the map are simply absent). Done in one
    // batched query (a temp table of the wanted rowids CROSS JOIN'd against
    // cells/pages) so every rowid is a point lookup — no per-rowid scan and no
    // whole-table scan. Used to map only the rowids a query actually displays.
    std::unordered_map<std::int64_t, std::int64_t> leafPagesForRowids(
        const std::string& tableName, const std::vector<std::int64_t>& rowids) const;

    // Largest page range /api/pages will serialize; beyond this the client must
    // use runs instead.
    static constexpr std::int64_t kPageRangeCap = 2000000;

    // Selected profile leaves (session/statement ids). Empty means "all".
    using LeafFilter = std::vector<int>;

    std::string metaJson() const;
    // Sets tooLarge when (to-from+1) exceeds kPageRangeCap (response is empty).
    std::string pagesJson(std::int64_t from, std::int64_t to, bool& tooLarge) const;
    // Structural runs from the map; when profiled is set and a profile is loaded,
    // runs are recomputed to the contiguous spans accessed by the selected leaves.
    std::string runsJson(std::int64_t from, std::int64_t to, bool profiled,
                         const LeafFilter& sel) const;
    // Pages owned by one object, by 0-based ordinal window [from, to] (Tables view).
    std::string objectPagesJson(std::int64_t objectId, std::int64_t from,
                                std::int64_t to, bool& tooLarge) const;
    // 0-based ordinal of `page` within its object's pages (ordered by pageNumber),
    // i.e. the block's position in the Tables-view band; -1 if the page isn't in the
    // object. JSON: {"ordinal":<n>}.
    std::string objectPageOrdinalJson(std::int64_t objectId, std::int64_t page) const;
    // The Tables-view structural page groups that are present (pages not owned by a
    // schema object): Freelist, Lock-Byte, All other pages. Each carries its page
    // count. JSON: {"groups":[{"key","label","pageCount"}...]}.
    std::string structuralGroupsJson() const;
    // Pages of one structural group, by 0-based ordinal window [from, to] (ordered by
    // pageNumber) — the Tables-view band counterpart of objectPagesJson. `key` is one
    // of "freelist" | "lockbyte" | "pointermap" | "other"; unknown → empty list.
    std::string structuralGroupPagesJson(const std::string& key, std::int64_t from,
                                         std::int64_t to, bool& tooLarge) const;
    // 0-based ordinal of `page` within its structural group's band, or -1 if the page
    // isn't in that group (used to scroll a structural page node to its band).
    std::string structuralGroupPageOrdinalJson(const std::string& key, std::int64_t page) const;
    // Empty string if the page does not exist.
    std::string pageJson(std::int64_t pageNumber, const LeafFilter& sel) const;
    std::string profilePagesJson(std::int64_t from, std::int64_t to,
                                 const LeafFilter& sel) const;

    // The map's page-type string for a page, or "" if the page is unknown.
    std::string pageType(std::int64_t page) const;

    // For an overflow page, the leaf/interior page that owns its cell (walks the
    // overflow chain back to the first non-overflow page). 0 if none/not overflow.
    std::int64_t overflowOwner(std::int64_t page) const;

    // For a table-interior page: the rowids covered by each divider cell's left
    // child and by the rightmost-pointer child, as collapsed runs plus a count
    // (rowids aren't contiguous — deletions leave gaps). Resolved without descent:
    // an interior child's runs come from page_row_runs, a leaf child's from its
    // cells. Capped by run count. JSON:
    // {"cells":[{"cellIndex","count","ranges":[[lo,hi]...]}...],
    //  "rightmost":{"count","ranges"}, "capped":bool}.
    std::string tableInteriorRowRunsJson(std::int64_t page) const;

    // Page Detail header info for a page: the table/index b-tree it belongs to and
    // (for a table-interior or table-leaf page) the row count of its subtree.
    // JSON: {"object":{"name","type"}?, "rowCount":<int>?}.
    std::string pageBtreeInfoJson(std::int64_t page) const;

    // The exact rowid runs of a table page's rows, keyset-paginated by startRowId
    // (rowids aren't contiguous — deletions leave gaps — so the Query view selects
    // them exactly, a batch at a time). For a table-interior/leaf page the runs are
    // its subtree's; for an overflow page they are its owner leaf's. JSON:
    // {"table":<name>|null, "runs":[[lo,hi]...], "nextAfter":<startRowId>|null}.
    std::string pageRowidRunsJson(std::int64_t page, std::int64_t after, int limit) const;

    // Page Tree view (b-tree structure). All lazy/windowed so nothing enumerates
    // the whole file. Children follow the map's pointer graph.
    // Roots: Page 1, one grouping node per table (holding its table b-tree and,
    // inlined, its indexes), the Freelist, Lock-Byte, and All other pages.
    std::string treeRootsJson() const;
    // Overview of a schema object (table/index) for the tree's grouping node:
    // {overview:{objectId,type,name,sql,pageCount,rootPage,rowCount,
    //  indexes:[{name,pageCount,rootPage}]}}. Empty string if no such object.
    std::string treeObjectOverviewJson(std::int64_t objectId) const;
    // Child page nodes of `page` (b-tree children, overflow, freelist-leaf).
    std::string treeChildrenJson(std::int64_t page) const;
    // Freelist trunk pages (children of the Freelist root), keyset-paginated.
    std::string treeFreelistJson(std::int64_t after, std::int64_t limit) const;
    // Pointer-map pages (children of the Pointer-map root), keyset-paginated.
    std::string treePointerMapJson(std::int64_t after, std::int64_t limit) const;
    // Pages under "All other pages": not page 1, not a root, not freelist/pointer-map,
    // and with no incoming pointer. Keyset-paginated by page number.
    std::string treeOtherJson(std::int64_t after, std::int64_t limit) const;
    // Ancestor chain from a b-tree root down to `page`, so the tree can expand to
    // it: `{path:[{page, edgeKind}]}` root-first (edgeKind null for the root).
    std::string treePathJson(std::int64_t page) const;
    // Node-search matches for a page-number prefix (the digits `query`): pages
    // whose decimal number starts with `query`, within [1, pageCount], ordered
    // exact value first then prefix-extensions ascending, capped at `limit`. Each
    // match is a tree page node ({page,label,pageType,objectId,cellCount,
    // freeBytes,subtreePageCount,hasChildren}). JSON: {matches:[...]}.
    std::string treeSearchJson(const std::string& query, int limit) const;

private:
    sqlite3* db_ = nullptr;
    std::string mapPath_;              // for opening short-lived private connections
    bool hasProfile_ = false;
    bool hasDb_ = false;
    std::vector<ProfileLeaf> leaves_;  // profile session/statement manifest
};
