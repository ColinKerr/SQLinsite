// Shapes returned by the visualize server API (see plan/commands/VISUALIZE.md).

export type Metric = "none" | "reads" | "writes" | "total";
export type View = "pages" | "tables" | "query" | "tree";

export interface ObjectInfo {
  id: number;
  type: string; // "table" | "index" | ...
  name: string;
  tableName: string | null;
  rootPage: number;
  pageCount: number;
  startPage: number;
  startLeafPage: number;
}

export interface Leaf {
  leafId: number;
  statementIndex: number;
}
export interface SessionInfo {
  session: string;
  leaves: Leaf[];
}

export interface Meta {
  meta: { pageCount: number; pageSize: number; formatVersion?: number; [k: string]: unknown };
  expectedFormatVersion: number; // format version this build understands
  objects: ObjectInfo[];
  typeCounts: { pageType: string; count: number }[];
  hasProfile: boolean;
  hasDb?: boolean;
  sessions: SessionInfo[];
}

export interface PageRow {
  pageNumber: number;
  pageType: string;
  objectId: number | null;
}
export interface PagesResponse {
  pages: PageRow[];
}

export interface Run {
  startPage: number;
  endPage: number;
  pageType: string;
  objectId: number | null;
}
export interface RunsResponse {
  runs: Run[];
}

export interface ObjectPageRow {
  ordinal: number;
  pageNumber: number;
  pageType: string;
}
export interface ObjectPagesResponse {
  pages: ObjectPageRow[];
}

export interface ProfilePage {
  pageNumber: number;
  reads: number;
  writes: number;
}
export interface ProfilePagesResponse {
  pages: ProfilePage[];
}

// ---- live query view -------------------------------------------------------
export interface QueryColumn {
  name: string;
  sourceTable: string | null;
  sourceColumn: string | null;
}
export interface RunSummary {
  queryId: number;
  columns: QueryColumn[];
  rowCount: number;
  truncated: boolean;
  pageCount: number;
  accesses: number;
  profile: { pages: ProfilePage[] };
  error?: string;
}
export interface RowsResponse {
  columns: QueryColumn[];
  rows: unknown[][];
  // Per row → per column → the pages holding that cell's bytes (leaf plus any
  // overflow pages). An empty array means the cell could not be resolved.
  rowPages: number[][][];
  rowCount: number;
}
// A batch of a table page's exact rowid runs (keyset-paginated by startRowId), for
// the Query view's "select this page's rows" node activation.
export interface PageRowidRuns {
  table: string | null;             // owning table, or null if the page has no rows
  runs: [number, number][];         // [lo, hi] inclusive rowid ranges
  nextAfter: number | null;         // cursor for the next batch, or null when done
  totalRowCount: number;            // rows across all of the page's runs
}
export interface ExplainResult {
  queryPlan?: { columns: string[]; rows: unknown[][] };
  explain?: { columns: string[]; rows: unknown[][] };
  error?: string;
}
export interface HistoryItem { id: number; sql: string; pageCount: number; accesses: number; }
export interface HistoryEntry {
  id: number; sql: string; columns: QueryColumn[];
  rowCount: number; truncated: boolean; pageCount: number; accesses: number;
  profile: { pages: ProfilePage[] };
}

export interface Pointer {
  toPage: number;
  kind: string;
  pageType?: string; // target page's type (for symbology), when known
}

// ---- page tree view --------------------------------------------------------
// Basic per-page details surfaced on tree nodes for the hover popover.
export interface PageBasics {
  cellCount?: number | null;
  freeBytes?: number | null;
  subtreePageCount?: number | null; // pages in this node's subtree (incl. self)
}
// A table/index b-tree root page, inlined under a table grouping node.
export interface TreeBtree extends PageBasics {
  kind: "page";
  label: string;             // "<name> (table)" or "<name> (index)"
  page: number | null;       // null only for virtual/no-rootpage tables
  pageType: string | null;
  objectId: number | null;
  hasChildren: number | boolean;
}
export interface TreeRoot extends PageBasics {
  kind: "page" | "table" | "freelist" | "other";
  label: string;
  page: number | null;
  pageType: string | null;
  objectId: number | null;
  hasChildren: number | boolean;
  // Present for kind === "table": the table's b-tree and its index b-trees.
  tableBtree?: TreeBtree | null;
  indexes?: TreeBtree[];
}
export interface TreeObjectIndex {
  name: string;
  pageCount: number | null;
  rootPage: number | null;
  sql: string | null; // CREATE INDEX statement (for the Index Overview)
}
export interface TreeObjectOverview {
  objectId: number;
  type: string;
  name: string;
  sql: string | null;
  pageCount: number | null;
  rootPage: number | null;
  rowCount: number | null;
  indexes: TreeObjectIndex[];
}
export interface TreeObjectResponse { overview: TreeObjectOverview; }
export interface TreeChild extends PageBasics {
  page: number;
  kind: string; // edge kind: child | overflow | freelist-leaf
  pageType: string;
  objectId: number | null;
  hasChildren: number | boolean;
}
export interface TreePagesResponse { pages: TreeChild[]; }
// A Node Search match: a page node (with a display label) for the search dropdown.
export interface TreeSearchMatch extends PageBasics {
  page: number;
  label: string;
  pageType: string | null;
  objectId: number | null;
  hasChildren: number | boolean;
}
export interface TreeSearchResponse { matches: TreeSearchMatch[]; }
export interface TreePathNode { page: number; edgeKind: string | null; }
export interface TreePathResponse { path: TreePathNode[]; }

// A slice of an overflowing value's bytes on one page (leaf or an overflow page).
export interface PageSegment { page: number; bytes: number; text?: string; }
export interface PageColumn {
  serialType: number;
  serialName: string;
  type: "null" | "int" | "real" | "text" | "blob";
  value: unknown;
  bytes?: number;
  truncated?: boolean;
  fromOverflow?: boolean;      // this column's bytes (partly) live in overflow pages
  segments?: PageSegment[];    // per-page byte/text breakdown when it overflows
}
export interface PageRegion {
  offset: number;
  length: number;
  kind: string; // db-header|page-header|cellptr-array|cell|free|reserved|overflow-header|payload|freelist-header|freelist-array|...
  cellIndex?: number;
}

export interface RowRun {
  startRowId: number;
  endRowId: number;
  rowCount: number;
}

export interface PageCell {
  cellIndex: number;
  offset: number;
  size: number;
  rowid?: number;
  leftChild?: number;
  overflowPage?: number;
  payloadBytes?: number;
  columns?: PageColumn[];
  // Table-interior only: the actual rowids in this divider cell's left-child
  // subtree — a count plus collapsed runs (rowids aren't contiguous: deletions
  // leave gaps). A run [s, e] with s === e is a single rowid.
  rowCount?: number;
  rowRuns?: RowRun[];
}
export interface PageContent {
  pageNumber: number;
  pageType: string;
  pageSize: number;
  usableSize: number;
  header: Record<string, unknown>;
  regions: PageRegion[];
  cells: PageCell[];
  pointers: Pointer[];
  object?: { name: string; type: string }; // the table/index b-tree this page belongs to
  rowCount?: number;                        // table-interior/table-leaf pages: rows in the subtree
  ownerPage?: number; // for an overflow page: the leaf/interior page that owns its cell
  // Table-interior only: rowids covered by the rightmost-pointer child, and a
  // flag when subtree enumeration was capped (very large subtree).
  rightmostRowRuns?: { rowCount: number; rowRuns: RowRun[] };
  rowidCapped?: boolean;
}
export interface PageDetail {
  pageNumber: number;
  pageType: string;
  objectId: number | null;
  freeBytes: number;
  cellCount: number;
  pointers?: Pointer[];
  profile?: { reads: number; writes: number };
  [k: string]: unknown;
}
