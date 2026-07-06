import { beforeEach, describe, expect, it, vi } from "vitest";
import { useQuery } from "./queryStore.ts";

// Build a rows-endpoint payload for [from,to).
function rowsSlice(from: number, to: number, total: number) {
  const rows: unknown[][] = [];
  const rowPages: (number | null)[][] = [];
  for (let i = from; i < Math.min(to, total); i++) {
    rows.push([i]);
    rowPages.push([2]);
  }
  return { columns: [{ name: "v", sourceTable: "Big", sourceColumn: "v" }], rows, rowPages, rowCount: total };
}

const TOTAL = 2500;

beforeEach(() => {
  useQuery.setState({
    sql: "SELECT v FROM Big", running: false, loadingMore: false,
    run: null, rows: null, error: null, resultsTab: "table", explain: null, history: [],
  });
  vi.stubGlobal("fetch", vi.fn(async (url: string, opts?: RequestInit) => {
    const u = String(url);
    if (opts?.method === "POST" && u === "/api/query/run") {
      return jsonResp({
        queryId: 7, columns: [{ name: "v", sourceTable: "Big", sourceColumn: "v" }],
        rowCount: TOTAL, truncated: true, pageCount: 11, accesses: 12, profile: { pages: [] },
      });
    }
    if (u.startsWith("/api/query/7/rows")) {
      const q = new URL("http://x" + u.slice(u.indexOf("?")));
      return jsonResp(rowsSlice(+q.searchParams.get("from")!, +q.searchParams.get("to")!, TOTAL));
    }
    if (u === "/api/query/history") return jsonResp({ history: [{ id: 7, sql: "SELECT v FROM Big", pageCount: 11, accesses: 12 }] });
    if (u === "/api/query/history/7") {
      return jsonResp({
        id: 7, sql: "SELECT v FROM Big", columns: [{ name: "v", sourceTable: "Big", sourceColumn: "v" }],
        rowCount: TOTAL, truncated: true, pageCount: 11, accesses: 12, profile: { pages: [] },
      });
    }
    return jsonResp({});
  }));
});

function jsonResp(body: unknown) {
  return { ok: true, json: async () => body } as Response;
}

describe("query store", () => {
  it("runCurrent loads the first row window and run summary", async () => {
    await useQuery.getState().runCurrent();
    const s = useQuery.getState();
    expect(s.run?.rowCount).toBe(TOTAL);
    expect(s.rows?.rows.length).toBe(1000); // ROW_WINDOW
    expect(s.resultsTab).toBe("table");
  });

  it("loadMoreRows appends the next window (incremental virtualization)", async () => {
    await useQuery.getState().runCurrent();
    await useQuery.getState().loadMoreRows();
    expect(useQuery.getState().rows?.rows.length).toBe(2000);
    await useQuery.getState().loadMoreRows();
    expect(useQuery.getState().rows?.rows.length).toBe(TOTAL); // clamped to total
    await useQuery.getState().loadMoreRows(); // no more to load
    expect(useQuery.getState().rows?.rows.length).toBe(TOTAL);
  });

  it("loadHistory restores sql + results without re-running", async () => {
    useQuery.setState({ sql: "SELECT 1" });
    await useQuery.getState().loadHistory(7);
    const s = useQuery.getState();
    expect(s.sql).toBe("SELECT v FROM Big");
    expect(s.run?.queryId).toBe(7);
    expect(s.rows?.rows.length).toBe(1000);
    // history restore must not POST /api/query/run
    const calls = (fetch as unknown as { mock: { calls: unknown[][] } }).mock.calls;
    expect(calls.some((c) => c[1] && (c[1] as RequestInit).method === "POST")).toBe(false);
  });
});
