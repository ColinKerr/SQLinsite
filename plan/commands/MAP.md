# Details for `sqlinsite map` command

```
sqlinsite map --db-file <db> --out-file <map.sqlite>
```

Reads a SQLite database file, parses its on-disk structure per the
[SQLite file format](https://sqlite.org/fileformat2.html), and writes the result
as an **indexed SQLite database** describing every page: its type, the schema
object that owns it, header fields, free space, the rows/keys it stores, and its
pointers to other pages.

The output is the data source for `sqlinsite visualize`, which queries it by
page-number range so it never loads the whole map into memory. A database may
have up to 4,294,967,294 pages, so the map is built and consumed **incrementally**
— never as one in-memory blob or JSON document.

## Approach

- **Raw page bytes come from the `SQLITE_DBPAGE` virtual table, read on demand.**
  We fetch one page at a time by number —
  `SELECT data FROM sqlite_dbpage('main') WHERE pgno=?`
  ([docs](https://www3.sqlite.org/matrix/dbpage.html)) — keeping the source
  connection open rather than loading the whole file into memory. An 8 GB database
  would otherwise need 8 GB of RSS and thrash on smaller machines; on-demand reads
  keep the builder's own footprint to a few hundred MB while SQLite's page cache
  and the OS file cache hold what's hot. Requires the amalgamation built with
  `SQLITE_ENABLE_DBPAGE_VTAB`.
- **We decode the page/cell *structure* ourselves**: header fields, cell offsets,
  table rowids, payload/overflow split, and overflow/child/freelist pointers — none
  exposed by `PRAGMA`s. **Index key *values* are not decoded or stored** by the map;
  the visualizer decodes them on demand from the source db (the `/content` endpoint)
  — persisting them was the bulk of map size and build time on index-heavy files.
- **Schema names come from SQLite.**
  `SELECT type, name, tbl_name, rootpage, sql FROM sqlite_schema` gives the
  authoritative `rootpage → object` mapping.
- **Object assignment uses the `DBSTAT` virtual table.** `SELECT pageno, name,
  pagetype FROM dbstat` walks every b-tree in C and names the object that owns each
  page (including overflow pages) — far cheaper than a C++ tree traversal that
  parsed every page just to follow pointers. The interior/leaf and table/index
  distinction comes from each page's own header byte (authoritative even for
  `WITHOUT ROWID` tables, whose rows live in index b-trees). Requires
  `SQLITE_ENABLE_DBSTAT_VTAB`.
- **Non-b-tree pages are classified structurally** — `DBSTAT` omits them — from the
  header (freelist trunk/leaf), computed intervals (pointer-map under auto-vacuum),
  the lock-byte page, else `unallocated`.
- **One streaming parse pass, then C++ roll-ups.** The builder reads each page once,
  writing `pages`/`cells`/`pointers`/`ptrmap`/`runs` as it goes, and while parsing
  it collects the compact edges it needs (leaf rowid runs, interior children, and
  the child/overflow/freelist-leaf adjacency). After the pass, the two derived
  tables are computed **in C++** from that state, not via whole-file recursive CTEs
  (which materialize huge temp b-trees and dominated build time on large files):
  `page_row_runs` by rolling leaf runs up the tree, and `subtreePageCount` by an
  iterative bottom-up sum. Per-page state is a few hundred MB even for a 2 M-page
  db; the *visualizer* scales regardless because it queries ranges.

### Fidelity

The map records the full page/cell **structure** of the file-format spec: the
100-byte database header; all b-tree page types (table/index, leaf/interior);
record varints/headers/serial types, table rowids and payload/overflow sizes;
overflow chains; freelist trunk/leaf pages; pointer-map pages (auto/incremental
vacuum); and the lock-byte page. Index key **values** and full per-cell payload
decoding are done on demand at visualize time from the source db, not persisted in
the map — so the source db must be supplied (`--db-file`) for the deepest per-page
inspection, while the Pages/Tables/Query/Tree views run from the map alone.

## Page numbering

The file format is **1-based** (page 1 = bytes `0 .. pageSize-1`). The `profile`
command's CSV `Page Number` is also 1-based and matches exactly, so `visualize`
joins a map to a profile CSV directly on the page number with no off-by-one.

## Output: SQLite schema

The DDL lives at [`commands/map.schema.sql`](./map.schema.sql) and is created
verbatim by `map`. Page type values: `table-leaf`, `table-interior`,
`index-leaf`, `index-interior`, `overflow`, `freelist-trunk`, `freelist-leaf`,
`pointer-map`, `lock-byte`, `unallocated`.

```sql
CREATE TABLE meta (              -- single row
  formatVersion INTEGER, path TEXT,
  pageSize INTEGER, pageCount INTEGER,
  textEncoding TEXT, writeVersion TEXT, readVersion TEXT,
  reservedBytesPerPage INTEGER, autoVacuum TEXT,
  freelistPageCount INTEGER, freelistTrunkPage INTEGER,
  schemaCookie INTEGER, sqliteVersionNumber INTEGER);

CREATE TABLE objects (
  id INTEGER PRIMARY KEY,
  type TEXT, name TEXT, tableName TEXT,
  rootPage INTEGER, sql TEXT, pageCount INTEGER);

CREATE TABLE pages (             -- one row per page; pageNumber is the rowid
  pageNumber INTEGER PRIMARY KEY,
  pageType TEXT, objectId INTEGER,
  freeBytes INTEGER, cellCount INTEGER,
  firstFreeblock INTEGER, cellContentStart INTEGER,
  fragmentedFreeBytes INTEGER, rightmostPointer INTEGER,
  parseError TEXT,
  subtreePageCount INTEGER);      -- pages in this page's subtree (self + child/overflow/freelist-leaf descendants)
CREATE INDEX pages_object ON pages(objectId);

CREATE TABLE cells (             -- ONLY table-interior cells (leftChild pointers)
  pageNumber INTEGER, cellIndex INTEGER,
  rowid INTEGER,                 -- always NULL now (kept for column compatibility)
  leftChild INTEGER,             -- the cell's left-child page
  payloadBytes INTEGER, localBytes INTEGER,
  overflowPage INTEGER,          -- table-leaf rows live in page_row_runs; index cells
  PRIMARY KEY (pageNumber, cellIndex)) WITHOUT ROWID;  -- + keys decoded on demand (/content)
CREATE INDEX cells_leftChild ON cells(leftChild);

CREATE TABLE pointers (
  fromPage INTEGER, toPage INTEGER, kind TEXT);  -- child|overflow|freelist-*|ptrmap-parent
CREATE INDEX pointers_from ON pointers(fromPage);
CREATE INDEX pointers_to   ON pointers(toPage);

CREATE TABLE page_row_runs (    -- contiguous rowid runs of each table page's subtree
  parentPageNumber INTEGER,     -- a table-interior OR table-leaf page
  startRowId INTEGER, endRowId INTEGER,
  rowCount INTEGER,             -- endRowId - startRowId + 1
  objectId INTEGER, isLeaf INTEGER);  -- drive the rowid → leaf-page lookup
CREATE INDEX page_row_runs_parent ON page_row_runs(parentPageNumber);
CREATE INDEX page_row_runs_leaf ON page_row_runs(objectId, startRowId) WHERE isLeaf=1;
                                -- rowid → table-leaf page (replaces cells_rowid)

CREATE TABLE ptrmap (            -- pointer-map entries (auto_vacuum)
  pageNumber INTEGER, targetPage INTEGER,
  entryType INTEGER, parentPage INTEGER,
  PRIMARY KEY (pageNumber, targetPage)) WITHOUT ROWID;

CREATE TABLE runs (              -- maximal contiguous spans of identical purpose
  startPage INTEGER, endPage INTEGER,
  pageType TEXT, objectId INTEGER);
CREATE INDEX runs_start ON runs(startPage);

CREATE TABLE type_counts (       -- pages per type, precomputed for the legend
  pageType TEXT PRIMARY KEY, count INTEGER);
```

`objects.pageCount` (pages per table/index) and `type_counts` (pages per page
type) are precomputed during the single pass so the visualizer's legend can show
counts without scanning the `pages` table.

The current map format version is **4** (`MapWriter::kFormatVersion`, written into `meta.formatVersion`). Bump it whenever the schema or semantics change incompatibly. (v3 dropped stored index keys; v4 stores only table-interior cells and moved rowid→leaf into `page_row_runs`.) `/api/meta` reports both the map's `meta.formatVersion` and the build's `expectedFormatVersion`; when loading the map the visualizer compares them and, if they differ (the map is **older or newer** than this build understands), shows an error message and asks the user to regenerate the map with `sqlinsite map` instead of rendering it.

### Why `runs`

A *run* is a maximal range `[startPage, endPage]` of consecutive pages with the
same `(pageType, objectId)` — i.e. blocks that "serve the same purpose". Runs are
typically far fewer than pages, and are what the visualizer draws and hovers when
zoomed out (one run can represent millions of pages). They are computed in the
single sequential pass and indexed by `startPage`; an overlap query is
`WHERE startPage <= :to AND endPage >= :from`.

### Why `page_row_runs`

Distinct from `runs` (page-number spans): `page_row_runs` stores, for **every**
table b-tree page — interior *and* leaf — the maximal contiguous **rowid** runs of
that page's subtree (a leaf is its own subtree; rowids aren't contiguous —
deletions leave gaps). It is computed **in C++ during the single parse pass**: each
table-leaf page's rowids are collapsed into runs as it is read, then interior pages'
runs are merged up the tree from their children (an earlier version used a
whole-file recursive CTE, which materialized a huge temp b-tree and dominated build
time on large databases). The visualizer can then answer "what rows does this page
cover" without descending the b-tree at request time. Because leaf pages are
included too, the Table Interior Cell control resolves every direct child with the
**same** `page_row_runs` lookup — the "last interior page before the leaves" works
exactly like any higher interior page, no leaf special-case. The `objectId`/`isLeaf`
columns (with the partial `page_row_runs_leaf` index) also make "which table-leaf
page holds rowid R" an O(log n) point lookup, replacing the dropped `cells(rowid)`
index. `subtreePageCount` is computed the same way — an iterative bottom-up sum over
the child/overflow/freelist-leaf edges collected during the pass, not a recursive
CTE.

### Indexing rationale

- `pages.pageNumber` is the rowid → O(log n) range scans for a viewport.
- `cells` (table-interior only) and `ptrmap` are keyed by page → fetched only when
  one page is inspected (interior rowid ranges).
- `pointers(fromPage)` for a page's outgoing links; `pointers(toPage)` for
  "what points here" (used by clickable links / back-navigation).
- `runs(startPage)` for zoomed-out level-of-detail queries.
- `page_row_runs(parentPageNumber)` to fetch one page's rowid runs; the partial
  `page_row_runs(objectId, startRowId) WHERE isLeaf=1` maps a result rowid straight
  to its table-leaf page for Query-view cell coloring (replaces `cells(rowid)`).

## Edge cases

- **Empty / fresh DB**: header + one b-tree page; valid output.
- **Non-SQLite / encrypted file**: error, exit 1, no output file written.
- **`reservedBytesPerPage` > 0**: usable size shrinks; free-byte math accounts.
- **WAL present**: maps the committed main database only (frames out of scope).
- **Corrupt page**: emitted as `unallocated` with a `parseError`, not aborting.

## Tests

- Build fixtures (single/multi-page tables, an index, an overflow row, an
  auto-vacuum DB with pointer-map pages, a post-delete DB with freelist pages),
  run `map`, then open the output with SQLite and assert:
  - `meta`/`objects` rows; `pages` count == `meta.pageCount`; page types.
  - object→page assignment via `pages.objectId`.
  - every `pointers.toPage` is a valid page number.
  - `runs` cover all pages with no gaps or overlaps and respect object/type
    boundaries.
  - `cells` holds only table-interior cells (their `leftChild`); table-leaf rows are
    recorded in `page_row_runs` (`isLeaf=1`) and rowid→leaf resolves through it.
  - `subtreePageCount` is non-null for every page and satisfies `parent = 1 + Σ
    children` over the child/overflow/freelist-leaf edges.
