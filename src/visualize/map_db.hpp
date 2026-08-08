#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "visualize/manifest_db.hpp"
#include "visualize/profile_db.hpp"

struct sqlite3;

// A pool of read-only SQLite connections to the map, one lazily opened per calling
// (httplib worker) thread and reused. Because SQLITE_THREADSAFE=1 serializes every
// operation on a single connection, sharing one connection would serialize all map
// reads; per-thread connections let concurrent reads run in parallel (a read-only
// DB has no writer, so unlimited concurrent readers are safe). Converts implicitly
// to the calling thread's connection so query code can keep using it as a sqlite3*.
class ReadPool {
public:
    ReadPool() = default;
    ~ReadPool();
    ReadPool(const ReadPool&) = delete;
    ReadPool& operator=(const ReadPool&) = delete;

    // `onOpen` runs once on each newly opened connection (e.g. to build its private
    // TEMP profile table). Must be called before any use.
    void init(std::string path, std::function<void(sqlite3*)> onOpen);
    // The calling thread's connection (opened on first use); null on open failure.
    operator sqlite3*() const;

private:
    std::string path_;
    std::function<void(sqlite3*)> onOpen_;
    mutable std::mutex mu_;
    mutable std::unordered_map<std::thread::id, sqlite3*> conns_;
};

// One of an object's physical runs (same as a Pages-view run filtered by objectId),
// annotated with its position in the band's packed ordinal coordinate (the object's
// physical page-number gaps collapse in ordinal space, but each run is kept whole —
// no extra cross-gap coalescing, so runs match the Pages view exactly).
struct OrdinalRun {
    std::int64_t startOrdinal;
    std::int64_t endOrdinal;
    std::int64_t startPage;
    std::int64_t endPage;
    std::string pageType;
};

// One CBS block's static aggregation for the Block view: the map's pages that fall
// in the block (page P → block (P-1)/pagesPerBlock), joined to manifest.blocks. Built
// once per (map, selected manifest db) and cached.
struct BlockAgg {
    std::int64_t blockIndex = 0;
    std::string blockId;               // hex; cloud object name = blockId + ".bcv"
    int sharedWithParent = 0;
    std::int64_t realPages = 0;        // real db pages (the final block is partial)
    std::int64_t freePages = 0;        // freelist / unallocated pages
    std::int64_t dominantObjectId = -1;  // object owning the most pages (-1 = none)
    std::vector<std::pair<std::int64_t, std::int64_t>> objectMix;  // objectId → pages, desc
};

// Opens a `sqlinsite map` SQLite file read-only and answers the visualize
// query API. Optionally holds an in-memory profile table for overlays.
class MapDb {
public:
    // Throws std::runtime_error if the file is not a valid map.
    explicit MapDb(const std::string& mapPath);
    ~MapDb();

    MapDb(const MapDb&) = delete;
    MapDb& operator=(const MapDb&) = delete;

    // Imports a `sqlinsite profile` output db (--profile-file) into the shared
    // profile db as 'input' sources, for overlay queries.
    void loadProfile(const std::string& profileDbPath);

    // Records an interactive Query-view run's per-page profile as a 'query' source
    // (sessionName = the SQL, sessionId = queryId) in the shared profile db, so
    // every view can overlay it. Returns the new sourceId. Serialized by the caller
    // (the QueryEngine runs one query at a time).
    std::int64_t addQuerySource(const std::string& sql, int queryId,
                                const std::vector<ProfileDb::PageCount>& pages);

    // Parses a CBS manifest.bcv (--manifest-file) into the shared ManifestDb,
    // resolving which named db matches this map. Enables the Block view + block
    // metrics. `dbName` empty means auto-select (see ManifestDb). Call before serving.
    void loadManifest(const std::string& manifestPath, const std::string& dbName);
    bool hasManifest() const { return manifest_ != nullptr; }
    const ManifestDb* manifest() const { return manifest_.get(); }

    // Temp-db file paths for the Analysis view's unified connection to ATTACH
    // (profile always present; manifest only with --manifest-file).
    std::string profileDbPath() const;
    std::string manifestDbPath() const;

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
    // Structural runs from the map (the pre-coalesced `runs` table) overlapping
    // [from, to]. The profile overlay is applied client-side.
    std::string runsJson(std::int64_t from, std::int64_t to) const;
    // A downsampled, object-colored overview of the whole file for the minimap:
    // the page space split into at most `buckets` contiguous spans, each labelled
    // with the object owning the most pages in it (objectId null = unowned). Derived
    // from the runs table in one pass and cached (the map is static). JSON:
    // {"pageCount":<n>, "buckets":[{"startPage","endPage","objectId"}...]}.
    std::string minimapJson(int buckets) const;
    // Pages owned by one object, by 0-based ordinal window [from, to] (Tables view).
    std::string objectPagesJson(std::int64_t objectId, std::int64_t from,
                                std::int64_t to, bool& tooLarge) const;
    // 0-based ordinal of `page` within its object's pages (ordered by pageNumber),
    // i.e. the block's position in the Tables-view band; -1 if the page isn't in the
    // object. JSON: {"ordinal":<n>}.
    std::string objectPageOrdinalJson(std::int64_t objectId, std::int64_t page) const;
    // An object's runs for the zoomed-out Tables LOD: the same runs as /api/runs
    // filtered by objectId, each carrying its page range plus its band ordinal range.
    // JSON: {"runs":[{startOrdinal,endOrdinal,startPage,endPage,pageType}...]}
    // overlapping the ordinal window [from,to]. Built once per object from the runs
    // table and cached (the map is static).
    std::string objectRunsJson(std::int64_t objectId, std::int64_t from, std::int64_t to) const;
    // Block view (requires a loaded, matching manifest). Rows for blocks in the
    // block-index window [from,to]: {blockIndex, blockId, startPage, endPage,
    // realPages, usedPages, freePages, dominantObjectId, sharedWithParent}. Built once
    // (blocksAgg) and cached. Empty {"blocks":[]} when no matching manifest.
    std::string blocksJson(std::int64_t from, std::int64_t to) const;
    // Full detail for one block: page range, object mix (with names), used/free,
    // blockId/object name, sharedWithParent, and profile read/write totals for `sel`.
    std::string blockJson(std::int64_t blockIndex, const LeafFilter& sel) const;
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
    // Every profile source (loaded 'input' + interactive 'query'), for the profile
    // selection tree. Refetched by the client after a run to pick up new sources.
    // JSON: {"sources":[{sourceId,kind,sessionName,sessionId}...]} ordered by id.
    std::string profileSourcesJson() const;

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
    // ATTACHes the shared profile db (`prof`) on a freshly opened pool connection
    // so its overlay queries can read every profile source (see ProfileDb).
    void onConnOpen(sqlite3* c) const;
    // Lazily builds + caches an object's ordinal-runs (see objectRunsJson).
    const std::vector<OrdinalRun>& objectRuns(std::int64_t objectId) const;
    // Lazily builds + caches the per-block aggregation for the selected manifest db.
    const std::vector<BlockAgg>& blocksAgg() const;

    // Declared before db_ so the read-pool connections (which ATTACH profile_ and
    // manifest_) are torn down before those temp files are deleted.
    ProfileDb profile_;                // unified store: loaded + interactive sources
    std::unique_ptr<ManifestDb> manifest_;  // present only with --manifest-file
    ReadPool db_;                      // per-thread read-only connections (see above)
    std::string mapPath_;              // for opening short-lived private connections
    bool hasProfile_ = false;
    bool hasDb_ = false;
    mutable std::mutex minimapMu_;     // guards the minimap cache across threads
    mutable std::string minimapCache_; // cached minimapJson (map is static)
    mutable int minimapCacheBuckets_ = -1;
    mutable std::mutex objRunsMu_;      // guards the per-object ordinal-run cache
    mutable std::unordered_map<std::int64_t, std::vector<OrdinalRun>> objRunsCache_;
    mutable std::mutex blocksMu_;       // guards the per-block aggregation cache
    mutable std::vector<BlockAgg> blocksCache_;
    mutable bool blocksBuilt_ = false;
};
