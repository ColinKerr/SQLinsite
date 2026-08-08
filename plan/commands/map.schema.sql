-- Schema of the SQLite file produced by `sqlinsite map`.
-- Created verbatim by the map command; queried by `sqlinsite visualize`.
-- Page numbers are 1-based, matching SQLite and the profile CSV's Page Number.

CREATE TABLE meta (
  formatVersion        INTEGER,
  path                 TEXT,
  pageSize             INTEGER,
  pageCount            INTEGER,
  textEncoding         TEXT,     -- utf-8 | utf-16le | utf-16be
  writeVersion         TEXT,     -- legacy | wal
  readVersion          TEXT,
  reservedBytesPerPage INTEGER,
  autoVacuum           TEXT,     -- none | full | incremental
  freelistPageCount    INTEGER,
  freelistTrunkPage    INTEGER,  -- nullable
  schemaCookie         INTEGER,
  sqliteVersionNumber  INTEGER
);

CREATE TABLE objects (
  id         INTEGER PRIMARY KEY,
  type       TEXT,    -- table | index
  name       TEXT,
  tableName  TEXT,
  rootPage   INTEGER,
  sql        TEXT,
  pageCount  INTEGER  -- pages owned by this object
);

CREATE TABLE pages (
  pageNumber         INTEGER PRIMARY KEY,  -- rowid, 1-based
  pageType           TEXT,
  objectId           INTEGER,             -- objects.id, nullable
  freeBytes          INTEGER,
  cellCount          INTEGER,
  firstFreeblock     INTEGER,
  cellContentStart   INTEGER,
  fragmentedFreeBytes INTEGER,
  rightmostPointer   INTEGER,             -- nullable (interior pages)
  parseError         TEXT,                -- nullable
  subtreePageCount   INTEGER              -- pages in this page's subtree (incl. self)
);
CREATE INDEX pages_object ON pages(objectId);
-- Serves pageType filters (structural-groups counts, tree-roots existence checks).
CREATE INDEX pages_type ON pages(pageType);

-- ONLY table-interior cells are stored (their leftChild → interior rowid ranges).
-- Table-leaf rows are represented compactly by page_row_runs; index cells and their
-- key values are decoded on demand from the source db (/content), not persisted —
-- storing per-leaf cells + index keys was the bulk of map size + build time.
CREATE TABLE cells (
  pageNumber   INTEGER,
  cellIndex    INTEGER,
  rowid        INTEGER,  -- always NULL now (interior divider keys aren't real rowids)
  leftChild    INTEGER,  -- the cell's left-child page
  payloadBytes INTEGER,
  localBytes   INTEGER,
  overflowPage INTEGER,  -- nullable
  PRIMARY KEY (pageNumber, cellIndex)
) WITHOUT ROWID;
CREATE INDEX cells_leftChild ON cells(leftChild);

CREATE TABLE pointers (
  fromPage INTEGER,
  toPage   INTEGER,
  kind     TEXT  -- child | overflow | freelist-next | freelist-leaf | ptrmap-parent
);
CREATE INDEX pointers_from ON pointers(fromPage);
CREATE INDEX pointers_to   ON pointers(toPage);

-- Maximal contiguous rowid runs of each table b-tree page's subtree (rowids
-- aren't contiguous: deletions leave gaps). One row per run; every table-interior
-- AND table-leaf page is a parentPageNumber — a leaf is its own subtree — so a
-- child page's runs are looked up the same way whether it is interior or leaf.
-- Distinct from `runs` below, which describes page-number spans, not rowids.
CREATE TABLE page_row_runs (
  parentPageNumber INTEGER,  -- a table-interior or table-leaf page
  startRowId       INTEGER,
  endRowId         INTEGER,
  rowCount         INTEGER,  -- rows in the run = endRowId - startRowId + 1
  objectId         INTEGER,  -- owning table object (for the rowid → leaf lookup)
  isLeaf           INTEGER   -- 1 if parentPageNumber is a table-leaf page
);
CREATE INDEX page_row_runs_parent ON page_row_runs(parentPageNumber);
-- "Which table-leaf page holds rowid R": among an object's leaf runs (disjoint,
-- ascending) the one with the largest startRowId ≤ R and endRowId ≥ R. Replaces
-- the old cells_rowid index now that table-leaf cells aren't stored.
CREATE INDEX page_row_runs_leaf ON page_row_runs(objectId, startRowId) WHERE isLeaf=1;

CREATE TABLE ptrmap (
  pageNumber INTEGER,  -- the pointer-map page
  targetPage INTEGER,
  entryType  INTEGER,
  parentPage INTEGER,
  PRIMARY KEY (pageNumber, targetPage)
) WITHOUT ROWID;

CREATE TABLE runs (
  startPage INTEGER,  -- maximal contiguous span with identical (pageType, objectId)
  endPage   INTEGER,
  pageType  TEXT,
  objectId  INTEGER   -- nullable
);
CREATE INDEX runs_start ON runs(startPage);

CREATE TABLE type_counts (   -- precomputed page count per page type (for the legend)
  pageType TEXT PRIMARY KEY,
  count    INTEGER
);
