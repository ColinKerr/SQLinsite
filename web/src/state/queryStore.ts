import { create } from "zustand";
import {
  explainQuery, fetchHistory, fetchHistoryEntry, fetchRows, fetchSchema, runQuery,
} from "../core/queryApi.ts";
import type {
  ExplainResult, HistoryItem, QueryColumn, RowsResponse, RunSummary, Schema,
} from "../core/types.ts";

export type ResultsTab = "table" | "pages" | "tables" | "explain";

// A results-table cell the user clicked; drives the schema-panel details panel.
export interface SelectedCell {
  rowIndex: number;
  colIndex: number;
  column: QueryColumn;
  value: unknown;
  pages: number[];
}

// How many result rows to load into the table at once.
const ROW_WINDOW = 1000;

export interface QueryState {
  schema: Schema | null;
  sql: string;
  running: boolean;
  loadingMore: boolean;
  run: RunSummary | null;
  rows: RowsResponse | null;
  error: string | null;
  resultsTab: ResultsTab;
  explain: ExplainResult | null;
  history: HistoryItem[];
  selectedCell: SelectedCell | null;

  loadSchema(): Promise<void>;
  refreshHistory(): Promise<void>;
  setSql(sql: string): void;
  setResultsTab(tab: ResultsTab): void;
  setSelectedCell(cell: SelectedCell | null): void;
  runCurrent(): Promise<void>;
  runSql(sql: string): Promise<void>;
  doExplain(): Promise<void>;
  loadHistory(id: number): Promise<void>;
  loadMoreRows(): Promise<void>;
}

export const useQuery = create<QueryState>((set, get) => ({
  schema: null,
  sql: "SELECT * FROM sqlite_schema;",
  running: false,
  loadingMore: false,
  run: null,
  rows: null,
  error: null,
  resultsTab: "table",
  explain: null,
  history: [],
  selectedCell: null,

  async loadSchema() {
    set({ schema: await fetchSchema() });
  },
  async refreshHistory() {
    set({ history: await fetchHistory() });
  },
  setSql: (sql) => set({ sql }),
  setResultsTab: (resultsTab) => set({ resultsTab }),
  setSelectedCell: (selectedCell) => set({ selectedCell }),

  async runCurrent() {
    const sql = get().sql.trim();
    if (!sql || get().running) return;
    set({ running: true, error: null, selectedCell: null });
    const summary = await runQuery(sql);
    if (summary.error) {
      set({ running: false, error: summary.error });
      return;
    }
    const rows = await fetchRows(summary.queryId, 0, ROW_WINDOW);
    set({ running: false, run: summary, rows, resultsTab: "table" });
    void get().refreshHistory();
  },
  async runSql(sql) {
    set({ sql });
    await get().runCurrent();
  },
  async doExplain() {
    const sql = get().sql.trim();
    if (!sql) return;
    set({ explain: await explainQuery(sql), resultsTab: "explain" });
  },

  // Restore a past run from history without re-executing it: its SQL, results,
  // and profile were stored server-side.
  async loadHistory(id) {
    const entry = await fetchHistoryEntry(id);
    if (!entry) return;
    const rows = await fetchRows(id, 0, ROW_WINDOW);
    set({
      sql: entry.sql, error: null, resultsTab: "table", rows, selectedCell: null,
      run: {
        queryId: entry.id, columns: entry.columns, rowCount: entry.rowCount,
        truncated: entry.truncated, pageCount: entry.pageCount, accesses: entry.accesses,
        profile: entry.profile,
      },
    });
  },

  // Fetches the next window of rows and appends them (incremental virtualization).
  async loadMoreRows() {
    const { run, rows, loadingMore } = get();
    if (!run || !rows || loadingMore) return;
    if (rows.rows.length >= run.rowCount) return;
    set({ loadingMore: true });
    const from = rows.rows.length;
    const more = await fetchRows(run.queryId, from, from + ROW_WINDOW);
    if (more) {
      set({
        loadingMore: false,
        rows: {
          ...rows,
          rows: [...rows.rows, ...more.rows],
          rowPages: [...rows.rowPages, ...more.rowPages],
        },
      });
    } else {
      set({ loadingMore: false });
    }
  },
}));
