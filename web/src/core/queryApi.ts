import { getJson } from "./api.ts";
import type {
  ExplainResult, HistoryEntry, HistoryItem, RowsResponse, RunSummary, Schema,
} from "./types.ts";

async function postSql<T>(url: string, sql: string): Promise<T> {
  const r = await fetch(url, { method: "POST", body: sql, headers: { "Content-Type": "text/plain" } });
  return (await r.json()) as T;
}

export const fetchSchema = () => getJson<Schema>("/api/schema");
export const runQuery = (sql: string) => postSql<RunSummary>("/api/query/run", sql);
export const explainQuery = (sql: string) => postSql<ExplainResult>("/api/query/explain", sql);
export const fetchRows = (id: number, from: number, to: number) =>
  getJson<RowsResponse>(`/api/query/${id}/rows?from=${from}&to=${to}`);
export async function fetchHistory(): Promise<HistoryItem[]> {
  const d = await getJson<{ history: HistoryItem[] }>("/api/query/history");
  return d?.history ?? [];
}
export const fetchHistoryEntry = (id: number) =>
  getJson<HistoryEntry>(`/api/query/history/${id}`);
