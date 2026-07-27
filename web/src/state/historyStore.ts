import { create } from "zustand";
import {
  entryLabel, entryToParams, loadHistory, MAX_ENTRIES, type NavEntry, type NavNode,
  paramsToEntry, sameEntry, saveHistory,
} from "../core/history.ts";
import type { View } from "../core/types.ts";
import { useViz } from "./store.ts";
import { useTree } from "./treeStore.ts";

export interface HistoryState {
  entries: NavEntry[];
  index: number;
  applying: boolean; // true while re-applying an entry (suppresses recording)

  record(entry: NavEntry): void;
  back(): void;
  forward(): void;
  jumpTo(i: number): void;
  hydrate(): void;
  // Restore an entry from the URL on load (applies + records as current position).
  restoreFromUrl(): void;
}

// Applies an entry to the real app state. Guarded by `applying` so the resulting
// store mutations don't record a new history entry. Kept outside the store so it
// can await async navigation (revealPage) without holding the set() call.
async function applyEntry(entry: NavEntry): Promise<void> {
  useHistory.setState({ applying: true });
  try {
    if (entry.node && "object" in entry.node) {
      // Set the object before switching, so a freshly-mounting Page View canvas
      // (or the existing controller's subscription) scrolls to it.
      useViz.getState().setSelectedObject(entry.node.object);
    } else {
      useViz.getState().setSelectedObject(null);
    }
    useViz.getState().setView(entry.view);
    if (entry.node && "page" in entry.node) {
      await useTree.getState().revealPage(entry.node.page);
    }
  } finally {
    useHistory.setState({ applying: false });
  }
}

export const useHistory = create<HistoryState>((set, get) => ({
  entries: [],
  index: -1,
  applying: false,

  record(entry) {
    const s = get();
    if (s.applying) return;
    if (sameEntry(entry, s.entries[s.index])) return;
    const entries = s.entries.slice(0, s.index + 1);
    entries.push(entry);
    if (entries.length > MAX_ENTRIES) entries.splice(0, entries.length - MAX_ENTRIES);
    const index = entries.length - 1;
    set({ entries, index });
    persist(entries, index);
  },

  back() { get().jumpTo(get().index - 1); },
  forward() { get().jumpTo(get().index + 1); },

  jumpTo(i) {
    const s = get();
    if (i < 0 || i >= s.entries.length || i === s.index) return;
    set({ index: i });
    void applyEntry(s.entries[i]);
    persist(s.entries, i);
  },

  hydrate() {
    const p = loadHistory();
    if (p && p.entries.length) {
      const index = Math.min(Math.max(p.index, 0), p.entries.length - 1);
      set({ entries: p.entries, index });
    }
  },

  restoreFromUrl() {
    const parsed = paramsToEntry(location.search);
    if (parsed) {
      // URL names a position: record it as the current entry, then apply it
      // (record first, since apply sets the `applying` guard that suppresses it).
      const entry: NavEntry = { ...parsed, label: labelFor(parsed.view, parsed.node) };
      get().record(entry);
      void applyEntry(entry);
      return;
    }
    const s = get();
    if (s.index >= 0) {
      // Hydrated from localStorage: apply the saved current position.
      void applyEntry(s.entries[s.index]);
      persist(s.entries, s.index);
    } else {
      // Fresh start: seed the stack with the current (default) position so the
      // first navigation leaves something to go Back to.
      get().record(currentEntry());
    }
  },
}));

function persist(entries: NavEntry[], index: number): void {
  saveHistory({ entries, index });
  const cur = entries[index];
  if (cur) history.replaceState(null, "", "?" + entryToParams(cur));
}

// Build a label from the current object list (for object nodes) or the page.
function labelFor(view: View, node: NavNode): string {
  const name = node && "object" in node
    ? useViz.getState().objById.get(node.object)?.name
    : undefined;
  return entryLabel(view, node, name);
}

// Computes the current (view, node) from the source stores.
function currentEntry(): NavEntry {
  const viz = useViz.getState();
  const view = viz.view;
  let node: NavNode = null;
  if (view === "pages" || view === "tables") {
    if (viz.selectedObject != null) node = { object: viz.selectedObject };
  } else if (view === "tree") {
    const page = useTree.getState().selectedPage;
    if (page != null) node = { page };
  }
  return { view, node, label: labelFor(view, node) };
}

// Wires the history store to the source stores: any change to the active
// (view, node) records a new entry (unless we're mid-apply). Call once at startup.
let wired = false;
export function initHistory(): void {
  if (wired) return;
  wired = true;
  const onChange = () => useHistory.getState().record(currentEntry());
  useViz.subscribe(onChange);
  useTree.subscribe(onChange);
}
