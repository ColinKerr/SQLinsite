# Details for `sqlinsite map` command

```
sqlinsite map --test-file <db> --out-file <map.sqlite>
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

- **Raw page bytes come from the `SQLITE_DBPAGE` virtual table.** We read each
  page's exact bytes through SQLite:
  `SELECT pgno, data FROM sqlite_dbpage('main') ORDER BY pgno`
  ([docs](https://www3.sqlite.org/matrix/dbpage.html)). Requires the amalgamation
  built with `SQLITE_ENABLE_DBPAGE_VTAB`.
- **We decode the bytes ourselves** (full fidelity): header fields, cells,
  rowids/keys, and overflow/child/freelist pointers — none exposed by `PRAGMA`s.
- **Schema names come from SQLite.**
  `SELECT type, name, tbl_name, rootpage, sql FROM sqlite_schema` gives the
  authoritative `rootpage → object` mapping.
- **Object assignment is by b-tree walk** from each `rootpage`, descending
  interior children and following overflow chains.
- **Remaining pages are classified structurally** (freelist from the header,
  pointer-map pages at computed intervals under auto-vacuum, the lock-byte page,
  else `unallocated`).
- **Streaming write.** The builder writes rows to the output database inside one
  transaction as it parses, rather than materializing every page in memory. The
  only per-page state retained is a compact `owner` array (object id per page)
  used to coalesce runs; truly pathological sizes are out of scope, but the
  *visualizer* scales regardless because it queries ranges.

### Full file-format fidelity

Everything in the file-format spec is decoded: the 100-byte database header; all
b-tree page types (table/index, leaf/interior); record varints, headers, serial
types and values (rowids, index keys, payload sizes); overflow chains; freelist
trunk/leaf pages; pointer-map pages (auto/incremental vacuum); and the lock-byte
page.

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

CREATE TABLE cells (             -- full per-cell detail
  pageNumber INTEGER, cellIndex INTEGER,
  rowid INTEGER,                 -- table-leaf cells only; NULL for interior/index cells
  leftChild INTEGER,
  payloadBytes INTEGER, localBytes INTEGER,
  overflowPage INTEGER, keyJson TEXT,   -- decoded index key values, JSON array
  PRIMARY KEY (pageNumber, cellIndex)) WITHOUT ROWID;
CREATE INDEX cells_rowid ON cells(rowid) WHERE rowid IS NOT NULL;  -- rowid → leaf page
CREATE INDEX cells_leftChild ON cells(leftChild);

CREATE TABLE pointers (
  fromPage INTEGER, toPage INTEGER, kind TEXT);  -- child|overflow|freelist-*|ptrmap-parent
CREATE INDEX pointers_from ON pointers(fromPage);
CREATE INDEX pointers_to   ON pointers(toPage);

CREATE TABLE page_row_runs (    -- contiguous rowid runs of each table page's subtree
  parentPageNumber INTEGER,     -- a table-interior OR table-leaf page
  startRowId INTEGER, endRowId INTEGER,
  rowCount INTEGER);            -- endRowId - startRowId + 1
CREATE INDEX page_row_runs_parent ON page_row_runs(parentPageNumber);

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

The current map format version is **2** (`MapWriter::kFormatVersion`, written into `meta.formatVersion`). Bump it whenever the schema or semantics change incompatibly. `/api/meta` reports both the map's `meta.formatVersion` and the build's `expectedFormatVersion`; when loading the map the visualizer compares them and, if they differ (the map is **older or newer** than this build understands), shows an error message and asks the user to regenerate the map with `sqlinsite map` instead of rendering it.

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
deletions leave gaps). It is computed once at build time (a finalization
`INSERT … SELECT` over `cells` + `pointers`) so the visualizer can answer "what
rows does this page cover" without descending the b-tree at request time. Because
leaf pages are included too, the Table Interior Cell control resolves every direct
child with the **same** `page_row_runs` lookup — the "last interior page before the
leaves" works exactly like any higher interior page, no leaf special-case.

### Indexing rationale

- `pages.pageNumber` is the rowid → O(log n) range scans for a viewport.
- `cells` and `ptrmap` are keyed by page → fetched only when one page is
  inspected (zoomed-in popup); `cells(rowid)` (partial, leaf cells) maps a result
  rowid straight to its leaf page for Query-view cell coloring.
- `pointers(fromPage)` for a page's outgoing links; `pointers(toPage)` for
  "what points here" (used by clickable links / back-navigation).
- `runs(startPage)` for zoomed-out level-of-detail queries.
- `page_row_runs(parentPageNumber)` to fetch one interior page's rowid runs.

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
  - `cells` rows exist for a known table/index page with expected rowids/keys.
