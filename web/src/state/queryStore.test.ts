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

// A page node runs that page's exact rows, one run-batch at a time. Model two
// batches of a leaf page (total 4 rows): each batch is its own query with 2 rows.
describe("query store — page node queries", () => {
  const col = { name: "v", sourceTable: "T", sourceColumn: "v" };
  const rowsFor = (n: number) =>
    ({ columns: [col], rows: Array.from({ length: n }, (_, i) => [i]),
       rowPages: Array.from({ length: n }, () => [3]), rowCount: n });

  beforeEach(() => {
    let nextQueryId = 100;
    const rowsByQuery = new Map<number, number>(); // queryId → its row count
    useQuery.setState({
      sql: "", running: false, loadingMore: false, run: null, rows: null, error: null,
      resultsTab: "table", explain: null, history: [], highlightPage: null, nodeQuery: null,
    });
    vi.stubGlobal("fetch", vi.fn(async (url: string, opts?: RequestInit) => {
      const u = String(url);
      if (opts?.method === "POST" && u === "/api/query/run") {
        const id = nextQueryId++;
        rowsByQuery.set(id, 2);
        return jsonResp({ queryId: id, columns: [col], rowCount: 2, truncated: false,
                          pageCount: 1, accesses: 1, profile: { pages: [{ pageNumber: 3, reads: 1, writes: 0 }] } });
      }
      const runsM = u.match(/^\/api\/page\/3\/rowid-runs\?after=(\d+)/);
      if (runsM) {
        return +runsM[1] === 0
          ? jsonResp({ table: "T", runs: [[1, 2]], nextAfter: 5, totalRowCount: 4 })
          : jsonResp({ table: "T", runs: [[5, 6]], nextAfter: null, totalRowCount: 4 });
      }
      const rowsM = u.match(/^\/api\/query\/(\d+)\/rows\?from=(\d+)&to=/);
      if (rowsM) {
        const total = rowsByQuery.get(+rowsM[1]) ?? 0;
        const from = +rowsM[2];
        return jsonResp(rowsFor(Math.max(0, total - from)));
      }
      if (u === "/api/query/history") return jsonResp({ history: [] });
      return jsonResp({});
    }));
  });

  it("runPageQuery loads the first batch and reports the page's total row count", async () => {
    await useQuery.getState().runPageQuery(3, false);
    const s = useQuery.getState();
    expect(s.sql).toBe('SELECT * FROM "T" WHERE (rowid BETWEEN 1 AND 2);');
    expect(s.run?.rowCount).toBe(4);            // whole page, across both batches
    expect(s.rows?.rows.length).toBe(2);        // only the first batch is loaded
    expect(s.highlightPage).toBeNull();
    expect(s.nodeQuery?.nextAfter).toBe(5);
  });

  it("scrolling loads the next run batch and finalizes when exhausted", async () => {
    await useQuery.getState().runPageQuery(3, false);
    await useQuery.getState().loadMoreRows();    // current batch done → run next batch
    expect(useQuery.getState().rows?.rows.length).toBe(4);
    await useQuery.getState().loadMoreRows();     // no more runs → finalize
    const s = useQuery.getState();
    expect(s.nodeQuery).toBeNull();
    expect(s.rows?.rows.length).toBe(4);
    expect(s.run?.rowCount).toBe(4);
  });

  it("an overflow page highlights that page", async () => {
    await useQuery.getState().runPageQuery(3, true);
    expect(useQuery.getState().highlightPage).toBe(3);
  });
});
