import { beforeEach, describe, expect, it, vi } from "vitest";
import { useAnalysis } from "./analysisStore.ts";

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

// Mocks POST /api/analysis/query: EXPLAIN* prefixes return plan/bytecode shapes,
// everything else returns a small result whose row count echoes the SQL length.
beforeEach(() => {
  useAnalysis.setState({
    sql: "SELECT * FROM map.meta;", running: false, mode: "table",
    result: null, explain: null, history: [],
  });
  vi.stubGlobal("fetch", vi.fn(async (url: string, opts?: RequestInit) => {
    const u = String(url);
    if (opts?.method === "POST" && u === "/api/analysis/query") {
      const sql = String(opts.body ?? "");
      if (sql.startsWith("EXPLAIN QUERY PLAN")) {
        return jsonResp({ columns: [{ name: "id" }, { name: "detail" }], rows: [[0, "SCAN meta"]], rowCount: 1 });
      }
      if (sql.startsWith("EXPLAIN")) {
        return jsonResp({ columns: [{ name: "addr" }, { name: "opcode" }], rows: [[0, "Init"]], rowCount: 1 });
      }
      return jsonResp({ columns: [{ name: "v" }], rows: [[1], [2]], rowCount: 2 });
    }
    return jsonResp({});
  }));
});

describe("analysis store", () => {
  it("run loads the result and records a client-side history entry", async () => {
    await useAnalysis.getState().run();
    const s = useAnalysis.getState();
    expect(s.mode).toBe("table");
    expect(s.result?.rowCount).toBe(2);
    expect(s.history).toHaveLength(1);
    expect(s.history[0]).toMatchObject({ sql: "SELECT * FROM map.meta;", rowCount: 2 });
  });

  it("re-running the same SQL dedupes and keeps history newest-first", async () => {
    await useAnalysis.getState().run();
    useAnalysis.setState({ sql: "SELECT 1;" });
    await useAnalysis.getState().run();
    await useAnalysis.getState().loadHistory(useAnalysis.getState().history.slice(-1)[0].id); // re-run first SQL
    const hist = useAnalysis.getState().history;
    expect(hist).toHaveLength(2);                       // no duplicate for the re-run SQL
    expect(hist[0].sql).toBe("SELECT * FROM map.meta;"); // most recent run first
  });

  it("doExplain populates both plan tables and switches to explain mode", async () => {
    await useAnalysis.getState().doExplain();
    const s = useAnalysis.getState();
    expect(s.mode).toBe("explain");
    expect(s.explain?.queryPlan?.columns).toEqual(["id", "detail"]);
    expect(s.explain?.explain?.rows).toEqual([[0, "Init"]]);
    expect(s.explain?.error).toBeUndefined();
    // Explain must not create a history entry (it's not a result run).
    expect(s.history).toHaveLength(0);
  });

  it("run surfaces server errors without adding history", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => jsonResp({ error: "no such table: nope" })));
    await useAnalysis.getState().run();
    const s = useAnalysis.getState();
    expect(s.result?.error).toBe("no such table: nope");
    expect(s.history).toHaveLength(0);
  });
});
