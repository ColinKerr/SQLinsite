import { create } from "zustand";
import { runAnalysisQuery } from "../core/api.ts";
import type { AnalysisResult } from "../core/types.ts";

export type AnalysisTab = "metrics" | "query";

// State for the Analysis view: which sub-view is active, and the Query-Metrics
// editor + last result. Queries run on the server's unified read-only connection
// (primary + map/profile/manifest) — they do not profile the primary db.
export interface AnalysisState {
  tab: AnalysisTab;
  sql: string;
  running: boolean;
  result: AnalysisResult | null;

  setTab(t: AnalysisTab): void;
  setSql(s: string): void;
  run(): Promise<void>;
  // Loads `sql` into the Query editor, switches to it, and runs (used by the
  // Predefined dashboard's "open SQL" links).
  openSql(sql: string): Promise<void>;
}

export const useAnalysis = create<AnalysisState>((set, get) => ({
  tab: "metrics",
  sql: "SELECT * FROM map.meta;",
  running: false,
  result: null,

  setTab: (tab) => set({ tab }),
  setSql: (sql) => set({ sql }),

  async run() {
    const sql = get().sql.trim();
    if (!sql || get().running) return;
    set({ running: true });
    try {
      set({ running: false, result: await runAnalysisQuery(sql) });
    } catch (e) {
      set({ running: false, result: { error: e instanceof Error ? e.message : "query failed" } });
    }
  },

  async openSql(sql) {
    set({ sql, tab: "query" });
    await get().run();
  },
}));
