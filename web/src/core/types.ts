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
  meta: { pageCount: number; pageSize: number; [k: string]: unknown };
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
export interface SchemaColumn { name: string; type: string; }
export interface SchemaIndex { name: string; pageCount: number; accessedPages: number; }
export interface SchemaTable {
  name: string; pageCount: number; accessedPages: number;
  columns: SchemaColumn[]; indexes: SchemaIndex[]; triggers: string[];
}
export interface SchemaView { name: string; columns: SchemaColumn[]; triggers: string[]; }
export interface Schema { tables: SchemaTable[]; views: SchemaView[]; }

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
  rowidMin?: number | null;
  rowidMax?: number | null;
}
export interface TreeRoot extends PageBasics {
  kind: "page" | "freelist" | "other";
  label: string;
  page: number | null;
  pageType: string | null;
  objectId: number | null;
  hasChildren: number | boolean;
}
export interface TreeChild extends PageBasics {
  page: number;
  kind: string; // edge kind: child | overflow | freelist-leaf
  pageType: string;
  objectId: number | null;
  hasChildren: number | boolean;
}
export interface TreePagesResponse { pages: TreeChild[]; }
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
  rowidCount?: number;
  rowidRanges?: [number, number][];
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
  ownerPage?: number; // for an overflow page: the leaf/interior page that owns its cell
  // Table-interior only: rowids covered by the rightmost-pointer child, and a
  // flag when subtree enumeration was capped (very large subtree).
  rightmostRowids?: { count: number; ranges: [number, number][] };
  rowidCapped?: boolean;
}
export interface PageDetail {
  pageNumber: number;
  pageType: string;
  objectId: number | null;
  freeBytes: number;
  cellCount: number;
  rowidMin: number | null;
  rowidMax: number | null;
  pointers?: Pointer[];
  profile?: { reads: number; writes: number };
  [k: string]: unknown;
}
