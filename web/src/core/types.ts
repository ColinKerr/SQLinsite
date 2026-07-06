// Shapes returned by the visualize server API (see plan/commands/VISUALIZE.md).

export type Metric = "none" | "reads" | "writes" | "total";
export type View = "pages" | "tables" | "query";

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
