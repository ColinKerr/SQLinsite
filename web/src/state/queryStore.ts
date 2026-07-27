import { create } from "zustand";
import {
  cancelQuery, explainQuery, fetchHistory, fetchHistoryEntry, fetchPageRowidRuns, fetchRows, runQuery,
} from "../core/queryApi.ts";
import { pageRowsSql, qi } from "../core/pageQuery.ts";
import type {
  ExplainResult, HistoryItem, ProfilePage, QueryColumn, RowsResponse, RunSummary,
} from "../core/types.ts";

export type ResultsTab = "table" | "pages" | "tables" | "explain";

// A results-table cell the user clicked; drives the Cell Details panel.
export interface SelectedCell {
  rowIndex: number;
  colIndex: number;
  column: QueryColumn;
  value: unknown;
  pages: number[];
}

// Pagination state for a "select this page's rows" node query. The page's rows are
// selected exactly, one run-batch at a time; as the user scrolls past a batch the
// next batch is fetched, run, and appended.
interface NodeQuery {
  page: number;
  overflow: boolean;
  nextAfter: number | null;   // cursor for the next run batch (null = no more runs)
  batchQueryId: number;       // the current batch's query (for row windowing)
  batchLoaded: number;        // rows loaded from the current batch
}

// How many result rows to load into the table at once.
const ROW_WINDOW = 1000;
// How many rowid runs to request per batch (kept under SQLite's expression limits).
const RUN_BATCH = 500;

// The in-flight run's abort controller, so cancelRun() can abort the client fetch
// (the server side is interrupted separately via cancelQuery). Module-level because
// it's imperative plumbing, not rendered state.
let runAbort: AbortController | null = null;
const isAbort = (e: unknown) => e instanceof DOMException && e.name === "AbortError";

// Merges profile page lists (union by page number, summing reads/writes).
function mergeProfile(a: ProfilePage[], b: ProfilePage[]): ProfilePage[] {
  const byPage = new Map<number, ProfilePage>();
  for (const p of a) byPage.set(p.pageNumber, { ...p });
  for (const p of b) {
    const e = byPage.get(p.pageNumber);
    if (e) { e.reads += p.reads; e.writes += p.writes; }
    else byPage.set(p.pageNumber, { ...p });
  }
  return [...byPage.values()];
}

export interface QueryState {
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
  highlightPage: number | null;   // results cells with data on this page are emphasized
  nodeQuery: NodeQuery | null;    // in-progress "select page's rows" pagination

  refreshHistory(): Promise<void>;
  setSql(sql: string): void;
  setResultsTab(tab: ResultsTab): void;
  setSelectedCell(cell: SelectedCell | null): void;
  setHighlightPage(page: number | null): void;
  runCurrent(): Promise<void>;
  cancelRun(): void;
  runSql(sql: string): Promise<void>;
  runObjectQuery(table: string): Promise<void>;
  runPageQuery(page: number, overflow: boolean): Promise<void>;
  doExplain(): Promise<void>;
  loadHistory(id: number): Promise<void>;
  loadMoreRows(): Promise<void>;
}

export const useQuery = create<QueryState>((set, get) => ({
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
  highlightPage: null,
  nodeQuery: null,

  async refreshHistory() {
    set({ history: await fetchHistory() });
  },
  setSql: (sql) => set({ sql }),
  setResultsTab: (resultsTab) => set({ resultsTab }),
  setSelectedCell: (selectedCell) => set({ selectedCell }),
  setHighlightPage: (highlightPage) => set({ highlightPage }),

  async runCurrent() {
    const sql = get().sql.trim();
    if (!sql || get().running) return;
    // A manual run is a plain single query — clear any node-query pagination/highlight.
    set({ running: true, error: null, selectedCell: null, nodeQuery: null, highlightPage: null });
    runAbort = new AbortController();
    try {
      const summary = await runQuery(sql, runAbort.signal);
      if (summary.cancelled) { set({ running: false }); return; }  // user pressed Cancel
      if (summary.error) { set({ running: false, error: summary.error }); return; }
      const rows = await fetchRows(summary.queryId, 0, ROW_WINDOW);
      set({ running: false, run: summary, rows, resultsTab: "table" });
      void get().refreshHistory();
    } catch (e) {
      if (isAbort(e)) { set({ running: false }); return; }  // cancel aborted the fetch
      set({ running: false, error: e instanceof Error ? e.message : "query failed" });
    } finally {
      runAbort = null;
    }
  },

  // Stops the in-flight run: abort the client fetch and interrupt the server query,
  // then drop the running state (leaving any prior results/error untouched).
  cancelRun() {
    if (!get().running) return;
    runAbort?.abort();
    void cancelQuery();
    set({ running: false });
  },
  async runSql(sql) {
    set({ sql });
    await get().runCurrent();
  },

  // Grouping node → the whole table (a plain query, ordinary row windowing).
  async runObjectQuery(table) {
    const sql = `SELECT * FROM ${qi(table)};`;
    set({ highlightPage: null });
    if (get().sql.trim() !== sql.trim() || !get().run) await get().runSql(sql);
  },

  // Table interior/leaf/overflow page → the page's exact rows, first run batch.
  async runPageQuery(page, overflow) {
    const batch = await fetchPageRowidRuns(page, 0, RUN_BATCH);
    if (!batch || !batch.table || batch.runs.length === 0) return;
    const sql = pageRowsSql(batch.table, batch.runs);
    // Same query already shown → just update the (overflow) highlight, no re-run.
    if (get().sql.trim() === sql.trim() && get().run) {
      set({ highlightPage: overflow ? page : null });
      return;
    }
    set({ sql, running: true, error: null, selectedCell: null, resultsTab: "table" });
    runAbort = new AbortController();
    try {
      const summary = await runQuery(sql, runAbort.signal);
      if (summary.cancelled) { set({ running: false }); return; }
      if (summary.error) { set({ running: false, error: summary.error }); return; }
      const rows = await fetchRows(summary.queryId, 0, ROW_WINDOW);
      set({
        running: false,
        run: { ...summary, rowCount: batch.totalRowCount },  // page total across all batches
        rows,
        highlightPage: overflow ? page : null,
        nodeQuery: {
          page, overflow, nextAfter: batch.nextAfter,
          batchQueryId: summary.queryId, batchLoaded: rows?.rows.length ?? 0,
        },
      });
      void get().refreshHistory();
    } catch (e) {
      if (isAbort(e)) { set({ running: false }); return; }
      set({ running: false, error: e instanceof Error ? e.message : "query failed" });
    } finally {
      runAbort = null;
    }
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
      highlightPage: null, nodeQuery: null,
      run: {
        queryId: entry.id, columns: entry.columns, rowCount: entry.rowCount,
        truncated: entry.truncated, pageCount: entry.pageCount, accesses: entry.accesses,
        profile: entry.profile, profileDeferred: entry.profileDeferred,
      },
    });
  },

  // Fetches the next window of rows and appends them (incremental virtualization).
  // For a node "select page's rows" query this also advances across run batches:
  // once the current batch's rows are exhausted the next batch is run and appended.
  async loadMoreRows() {
    const st = get();
    if (!st.run || !st.rows || st.loadingMore) return;
    const nq = st.nodeQuery;

    if (!nq) {
      if (st.rows.rows.length >= st.run.rowCount) return;
      set({ loadingMore: true });
      const more = await fetchRows(st.run.queryId, st.rows.rows.length, st.rows.rows.length + ROW_WINDOW);
      set((s) => ({
        loadingMore: false,
        rows: more && s.rows ? { ...s.rows, rows: [...s.rows.rows, ...more.rows], rowPages: [...s.rows.rowPages, ...more.rowPages] } : s.rows,
      }));
      return;
    }

    set({ loadingMore: true });
    // 1) More rows of the current run batch.
    const more = await fetchRows(nq.batchQueryId, nq.batchLoaded, nq.batchLoaded + ROW_WINDOW);
    if (more && more.rows.length > 0) {
      set((s) => ({
        loadingMore: false,
        nodeQuery: s.nodeQuery ? { ...s.nodeQuery, batchLoaded: nq.batchLoaded + more.rows.length } : null,
        rows: s.rows ? { ...s.rows, rows: [...s.rows.rows, ...more.rows], rowPages: [...s.rows.rowPages, ...more.rowPages] } : s.rows,
      }));
      return;
    }
    // 2) Current batch done → run the next run batch and append.
    if (nq.nextAfter != null) {
      const batch = await fetchPageRowidRuns(nq.page, nq.nextAfter, RUN_BATCH);
      if (batch && batch.table && batch.runs.length > 0) {
        const summary = await runQuery(pageRowsSql(batch.table, batch.runs));
        if (!summary.error) {
          const first = await fetchRows(summary.queryId, 0, ROW_WINDOW);
          set((s) => ({
            loadingMore: false,
            nodeQuery: { page: nq.page, overflow: nq.overflow, nextAfter: batch.nextAfter,
                         batchQueryId: summary.queryId, batchLoaded: first?.rows.length ?? 0 },
            rows: first && s.rows ? { ...s.rows, rows: [...s.rows.rows, ...first.rows], rowPages: [...s.rows.rowPages, ...first.rowPages] } : s.rows,
            run: s.run ? { ...s.run, accesses: s.run.accesses + summary.accesses,
                           // Merge exact page sets when inline; a batch with a deferred
                           // (large) profile has no pages, so fall back to summing its
                           // scalar pageCount (batches touch mostly-disjoint pages).
                           profile: { pages: mergeProfile(s.run.profile.pages, summary.profile.pages) },
                           pageCount: summary.profile.pages.length
                             ? mergeProfile(s.run.profile.pages, summary.profile.pages).length
                             : s.run.pageCount + summary.pageCount } : s.run,
          }));
          return;
        }
      }
    }
    // 3) Fully exhausted — stop and report the true loaded total.
    set((s) => ({ loadingMore: false, nodeQuery: null,
                  run: s.run && s.rows ? { ...s.run, rowCount: s.rows.rows.length } : s.run }));
  },
}));
