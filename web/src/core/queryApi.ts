import { getJson } from "./api.ts";
import type {
  ExplainResult, HistoryEntry, HistoryItem, PageRowidRuns, QueryProfile, RowsResponse, RunSummary,
} from "./types.ts";

async function postSql<T>(url: string, sql: string, signal?: AbortSignal): Promise<T> {
  const r = await fetch(url, {
    method: "POST", body: sql, headers: { "Content-Type": "text/plain" }, signal,
  });
  return (await r.json()) as T;
}

// `signal` lets a run be aborted client-side (see cancelQuery for the server side).
export const runQuery = (sql: string, signal?: AbortSignal) =>
  postSql<RunSummary>("/api/query/run", sql, signal);
// Interrupts the query currently executing on the server (fire-and-forget).
export const cancelQuery = () =>
  fetch("/api/query/cancel", { method: "POST" }).catch(() => {});
export const explainQuery = (sql: string) => postSql<ExplainResult>("/api/query/explain", sql);
export const fetchRows = (id: number, from: number, to: number) =>
  getJson<RowsResponse>(`/api/query/${id}/rows?from=${from}&to=${to}`);
export async function fetchHistory(): Promise<HistoryItem[]> {
  const d = await getJson<{ history: HistoryItem[] }>("/api/query/history");
  return d?.history ?? [];
}
export const fetchHistoryEntry = (id: number) =>
  getJson<HistoryEntry>(`/api/query/history/${id}`);
export const fetchPageRowidRuns = (page: number, after: number, limit: number) =>
  getJson<PageRowidRuns>(`/api/page/${page}/rowid-runs?after=${after}&limit=${limit}`);
// The full per-page profile of a run (fetched lazily for the map overlay when the
// run response deferred a large profile).
export const fetchQueryProfile = (id: number) =>
  getJson<QueryProfile>(`/api/query/${id}/profile`);
