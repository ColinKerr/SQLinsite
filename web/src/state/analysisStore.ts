import { create } from "zustand";
import { runAnalysisQuery } from "../core/api.ts";
import { formatSql } from "../core/formatSql.ts";
import type { AnalysisResult, ExplainResult } from "../core/types.ts";

export type AnalysisTab = "metrics" | "query";
export type AnalysisMode = "table" | "explain";

// A client-side recent-query entry. Analysis runs have no server-side side effects
// (no profiling, no stored history), so this history lives entirely in the store.
export interface AnalysisHistoryItem { id: number; sql: string; rowCount: number; }

const HISTORY_CAP = 50;

// State for the Analysis view: which sub-view is active, and the Query-Metrics
// editor + last result. Queries run on the server's unified read-only connection
// (primary + map/profile/manifest) — they do not profile the primary db.
export interface AnalysisState {
  tab: AnalysisTab;
  sql: string;
  running: boolean;
  mode: AnalysisMode;           // results pane shows the grid ("table") or Explain
  result: AnalysisResult | null;
  explain: ExplainResult | null;
  history: AnalysisHistoryItem[];

  setTab(t: AnalysisTab): void;
  setSql(s: string): void;
  format(): void;
  run(): Promise<void>;
  doExplain(): Promise<void>;
  loadHistory(id: number): Promise<void>;
  // Loads `sql` into the Query editor, switches to it, and runs (used by the
  // Predefined dashboard's "open SQL" links).
  openSql(sql: string): Promise<void>;
}

let nextHistoryId = 1;

export const useAnalysis = create<AnalysisState>((set, get) => ({
  tab: "metrics",
  sql: "SELECT * FROM map.meta;",
  running: false,
  mode: "table",
  result: null,
  explain: null,
  history: [],

  setTab: (tab) => set({ tab }),
  setSql: (sql) => set({ sql }),
  format: () => set({ sql: formatSql(get().sql) }),

  async run() {
    const sql = get().sql.trim();
    if (!sql || get().running) return;
    set({ running: true, mode: "table" });
    try {
      const result = await runAnalysisQuery(sql);
      set({ running: false, result });
      if (!result.error) {
        const item: AnalysisHistoryItem = { id: nextHistoryId++, sql, rowCount: result.rowCount ?? 0 };
        // Prepend, drop any earlier identical SQL, cap the list.
        set((s) => ({ history: [item, ...s.history.filter((h) => h.sql !== sql)].slice(0, HISTORY_CAP) }));
      }
    } catch (e) {
      set({ running: false, result: { error: e instanceof Error ? e.message : "query failed" } });
    }
  },

  async doExplain() {
    const sql = get().sql.trim();
    if (!sql || get().running) return;
    set({ running: true, mode: "explain" });
    try {
      // Both EXPLAIN variants run over the same unified analysis connection.
      const [qp, ex] = await Promise.all([
        runAnalysisQuery("EXPLAIN QUERY PLAN " + sql),
        runAnalysisQuery("EXPLAIN " + sql),
      ]);
      const explain: ExplainResult = qp.error || ex.error
        ? { error: qp.error || ex.error }
        : {
            queryPlan: { columns: (qp.columns ?? []).map((c) => c.name), rows: qp.rows ?? [] },
            explain: { columns: (ex.columns ?? []).map((c) => c.name), rows: ex.rows ?? [] },
          };
      set({ running: false, explain });
    } catch (e) {
      set({ running: false, explain: { error: e instanceof Error ? e.message : "explain failed" } });
    }
  },

  async loadHistory(id) {
    const item = get().history.find((h) => h.id === id);
    if (!item) return;
    set({ sql: item.sql });
    await get().run();
  },

  async openSql(sql) {
    set({ sql, tab: "query" });
    await get().run();
  },
}));
