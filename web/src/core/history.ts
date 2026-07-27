// History navigation model: a stack of (view, node) positions the user has
// visited, persisted to localStorage and mirrored into the URL. See
// plan/commands/VISUALIZE/VISUALIZE.md ("History navigator").

import type { View } from "./types.ts";

// The selected "node" within a view: a Navigation-panel object (Pages/Tables) or
// a b-tree page (Tree). Query has no sub-node.
export type NavNode = { object: number } | { page: number } | null;

export interface NavEntry {
  view: View;
  node: NavNode;
  label: string; // precomputed for the Back/Forward dropdowns
}

const STORAGE_KEY = "sqlinsite:navhistory";
export const MAX_ENTRIES = 100;

const VIEW_LABEL: Record<View, string> = {
  pages: "Pages",
  tables: "Tables",
  query: "Query",
  tree: "Tree",
};

// Human label for the dropdowns, e.g. "Pages · T", "Tree · page 5", "Query".
export function entryLabel(view: View, node: NavNode, objectName?: string): string {
  const base = VIEW_LABEL[view] ?? view;
  if (node && "object" in node) return `${base} · ${objectName ?? `object ${node.object}`}`;
  if (node && "page" in node) return `${base} · page ${node.page}`;
  return base;
}

// Two entries address the same place (used for de-duping consecutive records).
export function sameEntry(a: NavEntry | undefined, b: NavEntry | undefined): boolean {
  if (!a || !b) return false;
  if (a.view !== b.view) return false;
  const na = a.node, nb = b.node;
  if (na === null || nb === null) return na === nb;
  if ("object" in na && "object" in nb) return na.object === nb.object;
  if ("page" in na && "page" in nb) return na.page === nb.page;
  return false;
}

// ---- URL <-> entry ---------------------------------------------------------

// The query string for an entry, e.g. "view=tree&page=5" / "view=pages&obj=3".
export function entryToParams(entry: NavEntry): string {
  const p = new URLSearchParams();
  p.set("view", entry.view);
  if (entry.node && "object" in entry.node) p.set("obj", String(entry.node.object));
  if (entry.node && "page" in entry.node) p.set("page", String(entry.node.page));
  return p.toString();
}

// Parse a query string back to an entry (label filled in by the caller once the
// object list is known); returns null if there is no `view` param.
export function paramsToEntry(search: string): { view: View; node: NavNode } | null {
  const p = new URLSearchParams(search);
  const view = p.get("view") as View | null;
  if (!view || !(view in VIEW_LABEL)) return null;
  let node: NavNode = null;
  const obj = p.get("obj");
  const page = p.get("page");
  if (obj != null && obj !== "") node = { object: Number(obj) };
  else if (page != null && page !== "") node = { page: Number(page) };
  return { view, node };
}

// ---- localStorage persistence ----------------------------------------------

export interface PersistedHistory { entries: NavEntry[]; index: number; }

export function loadHistory(): PersistedHistory | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as PersistedHistory;
    if (!Array.isArray(parsed.entries) || typeof parsed.index !== "number") return null;
    return parsed;
  } catch {
    return null;
  }
}

export function saveHistory(state: PersistedHistory): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
  } catch {
    // ignore quota / unavailable storage
  }
}
